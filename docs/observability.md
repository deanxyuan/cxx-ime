# 可观测性与 Benchmark

## 目标

先建立稳定的性能和行为基线。所有后续优化必须回答：

- 某个输入为什么慢
- 候选来自哪个 producer
- 扫描了多少索引项
- 是否命中缓存
- 是否被 deadline 截断
- 是否因 revision 过期被丢弃

## QueryTrace 数据模型

轻量 trace 结构，定义在 `shared/include/cxxime/query_trace.h`，包含状态字段（`deadline_exceeded`、`truncated`、`cache_hit`、`cancelled`）、计数字段和耗时字段。

### should_log() 采样策略

分级采样，避免普通查询全量写日志：

| 条件 | 采样率 | 说明 |
|------|--------|------|
| `deadline_exceeded` 或 `cancelled` | 100% | 异常查询强制记录 |
| `total_us >= 30ms` | 100% | 慢查询强制记录 |
| `!cache_hit && total_us >= 10ms` | 100% | 缓存未命中且较慢 |
| `truncated` | 1% | 被截断的查询（采样） |
| 其他正常查询 | 0.1% | 低频采样 |

采样函数 `should_sample(session_id, revision, rate)` 使用 mix64 哈希，同输入同结果（确定性）。

### 单一日志出口

Engine 只填充 `last_trace()` 字段，**不自动调用** `trace_.log()`。ServerApp 在 IPC 请求完成后作为唯一出口调用 `trace_.log()`。Tools/tests 使用 Engine 自包含模式时可显式调用。

详见 [查询预算与候选收集](query-control.md)。

要求：

- release 默认采样记录，debug 全量记录
- 字符串字段限制长度，例如 `raw_input` 最多 128 字节
- 采样率硬编码在 `should_log()` 中（非配置项）

### CXXIME_LOG 热路径日志

release 构建下 `CXXIME_LOG` 宏展开为空操作（`do {} while (0)`），避免热路径 `OutputDebugStringW` 开销：

```cpp
// shared/include/cxxime/logging.h
#if defined(_DEBUG) || defined(CXXIME_ENABLE_TRACE_LOG)
#define CXXIME_LOG(fmt, ...) cxxime::debug_log(fmt, __VA_ARGS__)
#else
#define CXXIME_LOG(fmt, ...) do {} while (0)
#endif
```

## 插桩点

### TSF

文件：`cxx-ime/tsf/src/text_service.cpp`

记录：

- key down/up 收到时间
- `_client.process_key()` 调用耗时
- candidate window `update/move/show/paint` 耗时
- 最新 accepted revision

### IPC Client/Server

文件：

- `cxx-ime/ipc/src/ipc_client.cc`
- `cxx-ime/server/src/server_app.cc`

记录：

- request/response 往返耗时
- IPC 命令类型
- session id、revision
- response 中 preedit/candidate 数量

### Engine

文件：

- `cxx-ime/engine/src/engine.cc`
- `cxx-ime/engine/src/pinyin_translator.cc`
- `cxx-ime/engine/src/dict.cc`

记录：

- processor 耗时
- syllabifier 路径数量
- `has_prefix()` 命中数量
- `lookup_by_ids()` 每条路径扫描量
- 最终候选数量

## 日志格式

JSONL trace 输出，便于脚本分析：

```json
{"q":42,"sid":1,"rev":18,"input":"sdf","page":0,"page_size":9,"paths":12,"live":4,"candidates":7,"exact_scan":0,"prefix_scan":256,"user_scan":0,"cache":false,"deadline":false,"cancelled":false,"truncated":false,"proc_us":50,"trans_us":9200,"lookup_us":8800,"merge_us":100,"total_us":9300}
```

## 异步日志队列

trace 写入通过 lock-free MPSC 队列异步化，不阻塞按键路径：

```
Engine::process_key() → trace_.log()
  → TraceQueue::try_push()   // 非阻塞，队列满时丢弃
  → writer_thread_func()     // 后台批量写入 JSONL
```

### MPSCQueue

lock-free 多生产者单消费者队列，定义在 `shared/include/cxxime/mpscq.h`，实现 `shared/src/mpscq.cc`。基于 atomic CAS 操作，源自 gRPC MPSCQ。

### TraceQueue

MPSCQueue 的使用层包装，增加长度限制：

- `kQueueCapacity = 256`：队列容量上限
- `size_` 原子计数器：每 push +1，每 pop -1（实时准确）
- 超过容量时 `try_push()` 返回 false 并递增 `dropped_` 计数
- `kBatchSize = 32`：writer 线程批量消费
- `kFlushInterval = 100ms`：刷新间隔
- mutex+cv 仅用于 writer 线程休眠唤醒（不保护队列数据）

### 日志轮转

- 单文件上限 64 MiB
- 保留 4 代（`.1` 到 `.4`）
- 日志目录总量上限 512 MiB，超过时按最后写入时间清理到 384 MiB
- `tsf-*` 临时日志保留 7 天

## query_bench 工具

新增命令行工具 `tools/query_bench/`，用于离线性能测试。

命令：

```cmd
query_bench.exe --data cxx-ime\data --input s,sd,sdf,sddf,bj,srf,shrf,zguo,nihaoshijie --repeat 1000
query_bench.exe --data cxx-ime\data --file cases.txt --json trace.jsonl --repeat 1000
query_bench.exe --data cxx-ime\data --input s,sd,sdf,sddf,bj,srf,shrf,zguo,nihaoshijie --repeat 1000 --trace-log
```

`--trace-log` 启用时输出 `log%` 列，显示 `should_log()` 触发率。

输出：

```text
Input                e2e_p50  e2e_p95  e2e_p99  max_us   qry_p50  qry_p95  cands  exact_p95  prefix_p95  user_p95  paths  trunc%  deadline%
s                       1200     1800     2200    25000      800     1200      7          0         128         0      4    0.0%      0.0%
sd                      2100     3200     4100    30000     1500     2500      7          0         250         0      4    0.0%      0.0%
```

## 回归输入集

基础集：

```text
s
sd
sdf
sddf
bj
srf
shrf
zguo
nihao
nihaoshijie
woxiangshuruyiduanhenchangdepinyin
```

行为集：

- 快速连续输入
- 连续 Backspace
- Shift 中英文切换后输入
- 无效尾部：`nihaoxxxq`
- 多 session 并发

## 验收

- 每个输入可输出 P50/P95/P99
- trace 能定位 `s` 和 `sddf` 的路径数、扫描量和候选来源
- 插桩开启后性能开销 P95 < 3%

## 性能基线

新功能开发或重构后，对比最新基线确认无回归。

详细数据和历史对比见 [性能基准数据](benchmark-data.md)。

### 重跑基准

```cmd
# 离线查询 benchmark（无需 server）
build\tools\query_bench\Release\query_bench.exe --data data --input s,sd,sdf,sddf,bj,srf,shrf,zguo,nihao,nihaoshijie --repeat 500

# IPC 端到端 benchmark（需先启动 server）
scripts\benchmark.bat

# 单元 benchmark
build\test\Release\benchmark_test.exe
```

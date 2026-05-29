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

`should_log()` 在 `deadline_exceeded` 时强制记录日志（不受采样率限制）。

详见 [查询预算与 Deadline](query-budget.md)。

要求：

- release 默认采样记录，debug 全量记录
- trace 不允许在热路径频繁堆分配（使用 thread_local 预分配缓冲区）
- 字符串字段限制长度，例如 `raw_input` 最多 128 字节
- 采样率可通过配置文件调整

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

新增 JSONL trace 输出，便于脚本分析

```json
{"q":42,"rev":18,"input":"sdf","total_us":9300,"paths":12,"live":4,"prefix_scan":256,"candidates":7,"deadline":false}
```

保留现有 `CXXIME_LOG` 人读日志，但 benchmark 和 CI 使用 JSONL

## query_bench 工具

新增命令行工具 `tools/query_bench/`，用于离线性能测试。

命令：

```cmd
query_bench.exe --data cxx-ime\data --input s,sd,sdf,sddf,bj,srf,shrf,zguo,nihaoshijie --repeat 1000
query_bench.exe --data cxx-ime\data --file cases.txt --json trace.jsonl --repeat 1000
```

输出：

```text

Input    p50  p95  p99  max_us  candidates scan_p95
s        1200     1800     2200    25000     7    128
sd       2100     3200     4100    30000     7    250
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

新功能开发或重构后，对比以下基线确认无回归。

### 查询延迟（query_bench，repeat=500，deadline=30ms）

| 输入 | 类型 | 路径数 | 候选 | e2e P50 | e2e P99 | 查询 P50 | 查询 P99 |
|------|------|--------|------|---------|---------|----------|----------|
| `s` | 单字母 | 8 | 7 | 789 us | 1100 us | 785 us | 921 us |
| `sd` | 双字母缩写 | 4 | 7 | 936 us | 1379 us | 199 us | 269 us |
| `sdf` | 三字母缩写 | 0 | 0 | 2889 us | 3902 us | 1924 us | 2291 us |
| `sddf` | 四字母缩写 | 0 | 0 | 6059 us | 7740 us | 2939 us | 3514 us |
| `bj` | 双字母缩写 | 8 | 7 | 2067 us | 2612 us | 118 us | 128 us |
| `srf` | 三字母缩写 | 0 | 0 | 2143 us | 2670 us | 1243 us | 1437 us |
| `shrf` | 四字母缩写 | 0 | 0 | 6462 us | 7720 us | 2979 us | 3501 us |
| `zguo` | 混合拼音 | 2 | 7 | 1912 us | 2387 us | 486 us | 566 us |
| `nihao` | 全拼 | 2 | 7 | 3814 us | 4488 us | 1500 us | 1705 us |
| `nihaoshijie` | 长输入 | 1 | 1 | 25911 us | 30351 us | 4139 us | 4985 us |

> 路径数=0 表示缩写无词典匹配（预期行为），查询耗时主要花在 Syllabifier 路径枚举。

### 重跑基准

```cmd
# 离线查询 benchmark（无需 server）
build\tools\query_bench\Release\query_bench.exe --data data --input s,sd,sdf,sddf,bj,srf,shrf,zguo,nihao,nihaoshijie --repeat 500

# IPC 端到端 benchmark（需先启动 server）
scripts\benchmark.bat

# 单元 benchmark
build\test\Release\benchmark_test.exe
```

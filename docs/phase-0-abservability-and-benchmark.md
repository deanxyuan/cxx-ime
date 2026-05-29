# Phase 0 详细设计：可观测性与 Benchmark

## 目标

先建立稳定的性能和行为基线。所有后续优化必须回答：

- 某个输入为什么慢
- 候选来自哪个 producer
- 扫描了多少索引项
- 是否命中缓存
- 是否被 deadline 截断
- 是否因 revision 过期被丢弃

## QueryTrace 数据模型

新增轻量 trace 结构，位于 engine 层，例如 `cxx-ime/engine/src/query_trace.h`

```cpp
struct QueryTrace {
    uint64_t query_id = 0;          // 单调递增，全局唯一
    uint32_t session_id = 0;
    uint64_t revision = 0;          // 词典版本号，用于检测过期
    std::string raw_input;          // 原始输入（最多 128 字节）
    int page_index = 0;
    int page_size = 0;

    int syllable_path_count = 0;
    int live_path_count = 0;
    int candidate_count = 0;

    uint32_t exact_scan_count = 0;
    uint32_t prefix_scan_count = 0;
    uint32_t user_scan_count = 0;

    bool cache_hit = false;
    bool deadline_exceeded = false;
    bool cancelled = false;
    bool truncated = false;

    int64_t processor_us = 0;
    int64_t translate_us = 0;
    int64_t lookup_us = 0;
    int64_t merge_us = 0;
    int64_t total_us = 0;

    std::array<int64_t, 8> producer_us = {};
};
```

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
query_bench.exe --data cxx-ime\data --file cases.txt --json trace.jsonlnihaoshijie --repeat 1000
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

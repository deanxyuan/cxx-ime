# Phase 0 任务计划：可观测性与 Benchmark

基于 `phase-0-abservability-and-benchmark.md` 设计文档，拆分为可独立实施的任务。

---

## 任务总览

| # | 任务 | 依赖 | 预估工作量 | 涉及文件 |
|---|------|------|-----------|---------|
| T1 | QueryTrace 数据结构 | 无 | 小 | `engine/include/cxxime/query_trace.h` (新建) |
| T2 | Engine 层插桩 | T1 | 中 | `engine/src/engine.cc`, `engine/src/pinyin_translator.cc`, `engine/src/dict.cc` |
| T3 | IPC 层插桩 | T1 | 小 | `ipc/src/ipc_client.cc`, `server/src/server_app.cc` |
| T4 | TSF 层插桩 | T1 | 小 | `tsf/src/text_service.cpp` |
| T5 | JSONL 日志输出 | T1 | 小 | `shared/include/cxxime/logging.h`, 新增 `shared/src/query_trace_log.cc` |
| T6 | query_bench 工具 | T2 | 中 | `tools/query_bench/main.cc` (新建), `tools/query_bench/CMakeLists.txt` (新建) |
| T7 | 回归输入集与 CI 集成 | T6 | 小 | `tools/query_bench/cases.txt`, `scripts/benchmark.bat` |
| T8 | 性能开销验证 | T2-T5 | 小 | 测试用例 |

---

## T1: QueryTrace 数据结构

**目标**：定义轻量 trace 结构，零堆分配。

**新建文件**：`engine/include/cxxime/query_trace.h`

```cpp
struct QueryTrace {
    uint64_t query_id = 0;
    uint32_t session_id = 0;
    uint64_t revision = 0;
    char raw_input[128] = {};       // 固定长度，避免 std::string
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

**要点**：
- `raw_input` 用 `char[128]` 而非 `std::string`，避免堆分配
- 所有字段 POD 类型，可直接 memcpy
- 使用 `QueryPerformanceCounter` 计时（已在 engine 中使用）

**验收**：头文件可编译，无动态分配。

---

## T2: Engine 层插桩

**目标**：在 Engine 热路径中记录 trace 数据。

**修改文件**：

| 文件 | 插桩内容 |
|------|---------|
| `engine/src/engine.cc` | `process_key()` 总耗时、processor 耗时 |
| `engine/src/pinyin_translator.cc` | translate 耗时、syllable path count、live path count、candidate count |
| `engine/src/dict.cc` | `has_prefix()` 命中数、`lookup_by_ids()` 扫描量（exact_scan_count, prefix_scan_count） |

**实现方式**：
- `Engine` 持有 `thread_local QueryTrace` 缓冲区
- `process_key()` 入口重置 trace、记录 query_id 和 raw_input
- 各阶段记录耗时（`QueryPerformanceCounter` 差值转微秒）
- `process_key()` 出口将 trace 传递给日志层

**注意**：
- release 默认采样（如每 128 次记录 1 次），debug 全量记录
- 采样率可通过 Config 或编译宏控制
- trace 不得在热路径上做堆分配

**验收**：
- debug 构建下每个 `process_key()` 调用后 trace 字段非零
- release 构建下采样率可控
- 开销 P95 < 3%（通过 T8 验证）

---

## T3: IPC 层插桩

**目标**：记录 IPC 往返耗时和命令信息。

**修改文件**：

| 文件 | 插桩内容 |
|------|---------|
| `ipc/src/ipc_client.cc` | request 发送时间、response 接收时间、往返耗时 |
| `server/src/server_app.cc` | IPC 命令类型、session_id、revision、response 中 preedit/candidate 数量 |

**实现方式**：
- IpcClient 发送前记录 `QPC`，收到响应后计算差值
- 将 session_id、command type 写入 trace
- Server 端在 response 构造时记录 preedit/candidate 数量

**验收**：IPC 往返耗时可从 JSONL 日志中读取。

---

## T4: TSF 层插桩

**目标**：记录按键事件到 IPC 调用的完整链路。

**修改文件**：`tsf/src/text_service.cpp`

**插桩内容**：
- `OnKeyDown` / `OnKeyUp` 收到时间
- `_client.process_key()` 调用耗时
- candidate window `update/move/show/paint` 耗时
- 最新 accepted revision

**实现方式**：
- 在 `TextService` 类中持有 `QueryTrace` 或精简版 trace
- 按键事件入口记录时间戳
- IPC 调用前后记录耗时
- 候选窗口操作记录耗时

**验收**：TSF 到 Engine 全链路耗时可追踪。

---

## T5: JSONL 日志输出

**目标**：将 trace 数据输出为 JSONL 格式，便于脚本分析。

**新建文件**：`shared/src/query_trace_log.cc`（或内联到 logging.h）

**日志格式**：
```json
{"q":42,"rev":18,"input":"sdf","total_us":9300,"paths":12,"live":4,"prefix_scan":256,"candidates":7,"deadline":false}
```

**实现方式**：
- `QueryTrace` 提供 `to_json()` 方法，使用 `snprintf` 格式化（不依赖 nlohmann/json）
- 输出到 `OutputDebugStringW`（debug）或文件（release，可配置）
- 保留现有 `CXXIME_LOG` 人读日志不变

**验收**：
- debug 构建下 DebugView 可见 JSONL 行
- JSONL 可被 Python/PowerShell 解析

---

## T6: query_bench 工具

**目标**：离线性能测试工具，可重复运行。

**新建文件**：
- `tools/query_bench/main.cc`
- `tools/query_bench/CMakeLists.txt`

**功能**：
```
query_bench.exe --data <data_dir> --input "s,sd,sdf,sddf,bj,srf,shrf,zguo,nihaoshijie" --repeat 1000
query_bench.exe --data <data_dir> --file cases.txt --repeat 1000
query_bench.exe --data <data_dir> --file cases.txt --json trace.jsonl --repeat 1000
```

**输出格式**：
```
Input    p50  p95  p99  max_us  candidates scan_p95
s        1200 1800 2200 25000   7          128
sd       2100 3200 4100 30000   7          250
```

**实现要点**：
- 使用 Engine 的 self-contained 初始化路径（`initialize(dict_path, config_path)`）
- 复用 `Dict`、`SpellingsIndex`、`Syllabifier` 的现有加载逻辑
- 每个输入重复 N 次，收集耗时统计
- `--json` 输出完整 JSONL trace（含 QueryTrace 所有字段）
- 输出 P50/P95/P99/max 统计

**验收**：
- 可在命令行独立运行，不依赖 TSF/Server
- 输出格式与设计文档一致
- `--json` 输出可被脚本解析

---

## T7: 回归输入集与 CI 集成

**目标**：建立可重复的回归测试输入集。

**新建文件**：
- `tools/query_bench/cases.txt` — 基础回归集
- `scripts/benchmark.bat` — CI 运行脚本

**基础回归集**（`cases.txt`）：
```
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

**行为集**（手动测试，不纳入 CI）：
- 快速连续输入
- 连续 Backspace
- Shift 中英文切换后输入
- 无效尾部：`nihaoxxxq`
- 多 session 并发

**benchmark.bat**：
```cmd
@echo off
query_bench.exe --data %~dp0..\data --file %~dp0..\tools\query_bench\cases.txt --repeat 100 --json benchmark-result.jsonl
```

**验收**：
- `benchmark.bat` 一键运行
- 输出 P50/P95/P99 统计表

---

## T8: 性能开销验证

**目标**：确认插桩开启后性能开销 P95 < 3%。

**方法**：
1. 用 `query_bench` 工具分别在 trace 开启/关闭状态下运行同一输入集
2. 对比 P50/P95/P99 差异
3. 确认开销 < 3%

**验收标准**：
- 所有输入的 P95 耗时增幅 < 3%
- trace 开启时无额外堆分配（通过 `_CrtMemCheckpoint` 验证）

---

## 实施顺序

```
T1 → T2 → T6 → T7 → T8
         ↘ T3 ↗
          ↘ T4 ↗
           ↘ T5 ↗
```

建议提交顺序：

1. `feature: add QueryTrace data structure` (T1)
2. `feature: instrument engine layer with QueryTrace` (T2)
3. `feature: add JSONL trace output` (T5)
4. `feature: instrument IPC layer` (T3)
5. `feature: instrument TSF layer` (T4)
6. `feature: add query_bench offline benchmark tool` (T6)
7. `test: add regression input set and benchmark script` (T7)
8. `test: verify instrumentation overhead < 3%` (T8)

---

## 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| 热路径计时开销过大 | 影响用户体验 | release 采样记录，QPC 差值 < 100ns |
| thread_local 在 DLL 中行为异常 | TSF DLL 中 trace 丢失 | 使用 `__declspec(thread)` 或进程级缓冲区 |
| query_bench 无法复现线上延迟 | benchmark 结果不具参考性 | 工具使用与线上相同的词典和配置 |
| JSONL 日志文件膨胀 | 磁盘空间 | 限制日志文件大小，自动轮转 |

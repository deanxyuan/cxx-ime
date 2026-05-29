# 查询预算与 Deadline 机制

## 概述

候选查询可能因路径枚举、词典扫描等操作消耗大量时间。`QueryBudget` 机制提供两种控制手段：

1. **Deadline**：基于时间的截止限制，超时立即返回当前结果
2. **Scan Budget**：基于扫描条目数的限制，防止单次查询扫描过多索引项

两者通过 `QueryBudget` 统一结构传递，在查询管道的多个检查点执行。

## 数据结构

### QueryBudget

```cpp
// shared/include/cxxime/query_budget.h
struct QueryBudget {
    int64_t deadline_us = 0;        // 截止时间（微秒），0 = 无限制
    uint32_t max_exact_scan = 512;  // 精确匹配最大扫描条目数
    uint32_t max_prefix_scan = 2048;// 前缀匹配最大扫描条目数
    uint32_t max_user_scan = 512;   // 用户词典最大扫描条目数
    int64_t start_qpc = 0;          // 查询起始时间戳（Engine 自动设置）

    bool expired() const;           // 判断是否已超时
};
```

`expired()` 通过 `steady_clock` 计算从 `start_qpc` 到当前时间的差值，与 `deadline_us` 比较。

### QueryTrace

```cpp
// shared/include/cxxime/query_trace.h
struct QueryTrace {
    // ... 计数字段 ...
    bool cache_hit = false;         // 是否命中缓存（预留，尚未实现）
    bool deadline_exceeded = false; // 是否触发 deadline
    bool cancelled = false;         // 是否被取消（预留，尚未实现）
    bool truncated = false;         // 是否被截断（scan budget 或 deadline）
    // ... 耗时字段 ...
};
```

`deadline_exceeded` 和 `truncated` 在查询管道中被设置。`should_log()` 方法在 `deadline_exceeded` 时强制记录日志。

## 检查点分布

```
Engine::process_key()
  │ 设置 budget_.start_qpc
  │ 运行 Processor
  │ 检查 deadline → 超时则跳过 translate
  ▼
PinyinTranslator::translate(budget, trace)
  │ 检查 deadline → < 10ms 则跳过 Syllabifier
  │ Syllabifier 路径枚举
  │ 检查 deadline → 每条路径 has_prefix 前检查
  │ has_prefix 路径过滤
  │ 检查 deadline → 每条路径 lookup 前检查
  ▼
Dict::lookup_by_ids(budget, trace)
  │ 精确匹配扫描：每 64 条检查 deadline + max_exact_scan
  │ 前缀匹配扫描：每 64 条检查 deadline + max_prefix_scan
  ▼
返回候选结果
```

共 5 个检查点，覆盖从引擎入口到词典扫描的完整路径。

### 各检查点行为

| 检查点 | 位置 | 超时行为 | 截断行为 |
|--------|------|----------|----------|
| Engine 入口 | `engine.cc` | 跳过 translate，返回空候选 | — |
| Translator 入口 | `pinyin_translator.cc` | `deadline_us < 10ms` 跳过 Syllabifier | — |
| has_prefix 前 | `pinyin_translator.cc` | 跳过当前路径及后续路径 | — |
| lookup 前 | `pinyin_translator.cc` | 跳过当前路径及后续路径 | — |
| 精确扫描中 | `dict.cc` 每 64 条 | 中断扫描 | 设置 `truncated` |
| 前缀扫描中 | `dict.cc` 每 64 条 | 中断扫描 | 设置 `truncated` |

## 使用方式

### Engine 自动设置起始时间

`Engine::process_key()` 在每次按键处理开始时自动记录 `budget_.start_qpc`，无需手动设置。

### 外部设置 Deadline

通过 `Engine::set_query_budget()` 设置：

```cpp
QueryBudget budget;
budget.deadline_us = 30000;  // 30ms
budget.max_exact_scan = 512;
engine.set_query_budget(budget);
```

### 当前使用场景

| 场景 | Deadline | 说明 |
|------|----------|------|
| 生产环境（Server） | 无（`deadline_us = 0`） | Server 未调用 `set_query_budget()` |
| query_bench 工具 | 30ms（默认） | `--deadline-ms=30` |
| 单元测试 | 按测试需要设置 | 测试 deadline 超时行为 |

## Scan Budget 限制

除 deadline 外，`Dict::lookup_by_ids()` 还独立检查扫描条目数上限：

| 限制 | 默认值 | 作用 |
|------|--------|------|
| `max_exact_scan` | 512 | 精确匹配扫描超过此数则中断 |
| `max_prefix_scan` | 2048 | 前缀匹配扫描超过此数则中断 |
| `max_user_scan` | 512 | 用户词典扫描超过此数则中断 |

扫描限制与 deadline 独立生效，任一触发都会设置 `trace->truncated = true`。

## 与候选查询管道的集成

详见 [候选词选词算法](candidate-selection.md)。Deadline 检查点嵌入在四步流程的每一步之间：

1. **拼写图构建** — `deadline_us < 10ms` 时跳过 Syllabifier（因其内部不检查 deadline）
2. **路径枚举** — Syllabifier 内部通过枚举上限（10000 条）控制，不受 deadline 影响
3. **路径过滤** — 每条路径 `has_prefix` 前检查 deadline
4. **候选查找** — 每条路径 `lookup_by_ids` 前检查 + 扫描中每 64 条检查

## 日志与观测

`QueryTrace` 在 `deadline_exceeded` 时触发强制日志输出（不受采样率限制）。JSONL 格式：

```json
{"q":42,"input":"sdf","total_us":9300,"deadline":false,"truncated":false}
```

详见 [可观测性设计](observability.md)。

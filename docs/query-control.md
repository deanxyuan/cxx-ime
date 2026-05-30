# 查询预算与候选收集策略

## 概述

候选查询可能因路径枚举、词典扫描等操作消耗大量时间。查询预算机制提供三种控制手段：

1. **Deadline**：基于时间的截止限制，超时立即返回当前结果
2. **Scan Budget**：基于扫描条目数的限制，防止单次查询扫描过多索引项
3. **Top-K 收集**：固定容量候选收集器，扫描过程中限制候选数量，避免全量排序

三者通过 `QueryBudget` 统一结构传递，在查询管道的多个检查点执行。

## 数据结构

### QueryBudget

```cpp
// shared/include/cxxime/query_budget.h
struct QueryBudget {
    int64_t deadline_us = 0;            // 截止时间（微秒），0 = 无限制
    uint32_t max_exact_scan = 512;      // 精确匹配最大扫描条目数
    uint32_t max_prefix_scan = 2048;    // 前缀匹配最大扫描条目数
    uint32_t max_user_scan = 512;       // 用户词典最大扫描条目数（预留，尚未接入扫描循环）
    uint32_t max_results_before_merge = 64;  // TopK 容量（每次 lookup 的候选收集上限）
    uint32_t topk = 0;                  // translator 层合并容量上限（预留，当前未使用）

    int64_t start_qpc = 0;             // 查询起始时间戳（Engine 自动设置）

    bool expired() const;               // 判断是否已超时
};
```

`expired()` 通过 `steady_clock` 计算从 `start_qpc` 到当前时间的差值，与 `deadline_us` 比较。

### TopKCollector

```cpp
// engine/include/cxxime/topk_collector.h
class TopKCollector {
public:
    explicit TopKCollector(size_t k);
    void offer(Candidate&& c);           // O(K) 线性扫描最小值替换
    size_t size() const;
    bool full() const;
    std::vector<Candidate> finish();     // 排序后 move 出去
};
```

固定容量收集器，扫描过程中限制候选数量。K 通常 < 100，采用"小数组 + 替换最小值"策略，避免堆结构的额外开销。

### QueryTrace

```cpp
// shared/include/cxxime/query_trace.h
struct QueryTrace {
    // ... 计数字段 ...
    bool cache_hit = false;         // 是否命中缓存（预留）
    bool deadline_exceeded = false; // 是否触发 deadline
    bool cancelled = false;         // 是否被取消（预留）
    bool truncated = false;         // 是否被截断（scan budget / TopK / deadline）
    // ... 耗时字段 ...
};
```

`deadline_exceeded` 和 `truncated` 在查询管道中被设置。`should_log()` 方法在 `deadline_exceeded` 时强制记录日志。

## 动态预算：make_budget()

Engine 层在每次查询时根据输入长度自动创建预算，继承外部 deadline：

```cpp
// engine.cc
QueryBudget effective_budget = make_budget(pinyin.size(), page_size);
effective_budget.deadline_us = budget_.deadline_us;
effective_budget.start_qpc = budget_.start_qpc;
```

分档策略：

| 输入长度 | max_exact_scan | max_prefix_scan | max_results_before_merge | topk |
|----------|---------------|-----------------|--------------------------|------|
| 1 | 128 | 512 | 32 | page_size |
| 2 | 256 | 1024 | 48 | page_size |
| 3–4 | 512 | 2048 | 64 | page_size |
| 5+ | 1024 | 4096 | 96 | page_size |

短输入（如 `s`）展开为多条路径，扫描量大但用户只需首屏候选，因此预算更严格。

## 检查点分布

```
Engine::process_key()
  │ 创建 effective_budget = make_budget(input_len, page_size)
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
  │ 候选 → seen 去重 → TopKCollector.offer()
  ▼
返回 collector.finish()（已排序，大小 ≤ limit）
```

共 6 个检查点，覆盖从引擎入口到词典扫描的完整路径。

### 各检查点行为

| 检查点 | 位置 | 超时行为 | 截断行为 |
|--------|------|----------|----------|
| Engine 入口 | `engine.cc` | 跳过 translate，返回空候选 | — |
| Translator 入口 | `pinyin_translator.cc` | `deadline_us < 10ms` 跳过 Syllabifier | — |
| has_prefix 前 | `pinyin_translator.cc` | 跳过当前路径及后续路径 | — |
| lookup 前 | `pinyin_translator.cc` | 跳过当前路径及后续路径 | — |
| 精确扫描中 | `dict.cc` 每 64 条 | 中断扫描 | 设置 `truncated` |
| 前缀扫描中 | `dict.cc` 每 64 条 | 中断扫描 | 设置 `truncated` |

## 扫描流程

`Dict::lookup_by_ids()` 的完整流程：

```
TopKCollector collector(min(limit, max_results_before_merge))
seen_set（容量上限 = max_results_before_merge * 2）

精确匹配扫描循环：
  每 64 条检查 deadline
  超过 max_exact_scan → truncated, break
  候选 → seen 去重 → collector.offer()

前缀匹配扫描循环：
  每 64 条检查 deadline
  超过 max_prefix_scan → truncated, break
  候选 → seen 去重 → collector.offer()

return collector.finish()  // 已排序，大小 ≤ limit
```

### 去重策略

`unordered_set<string> seen` 对通过去重检查的候选插入，容量上限 `max_results_before_merge * 2`。流程：先检查 `seen` 容量是否已满，未满则插入 text 并调用 `collector.offer()`；已满则设置 `truncated = true` 并跳过后续候选。

## truncated 语义

`truncated=true` 表示返回结果不是完整候选全集，而是按策略有意截断。它不代表质量差，表示查询按低时延策略停止继续搜索。

### 触发条件

```
truncated = true 当且仅当以下任一条件成立：
1. exact scan 达到 make_budget() 分配的 max_exact_scan
2. prefix scan 达到 make_budget() 分配的 max_prefix_scan
3. seen set（去重表）达到 max_results_before_merge * 2 容量上限
4. deadline_exceeded（时间预算耗尽）
```

### 不触发场景

- 查询本身无 live path（空结果，不是截断）
- posting list 扫描完且候选数 ≤ page_size

## 使用方式

### Engine 自动设置起始时间

`Engine::process_key()` 在每次按键处理开始时自动记录 `budget_.start_qpc`，无需手动设置。

### 外部设置 Deadline

通过 `Engine::set_query_budget()` 设置：

```cpp
QueryBudget budget;
budget.deadline_us = 30000;  // 30ms
engine.set_query_budget(budget);
```

Scan budget 和 TopK 由 `make_budget()` 自动管理，无需手动设置。

### 当前使用场景

| 场景 | Deadline | 说明 |
|------|----------|------|
| 生产环境（Server） | 无（`deadline_us = 0`） | Server 未调用 `set_query_budget()` |
| query_bench 工具 | 30ms（默认） | `--deadline-ms=30` |
| 单元测试 | 按测试需要设置 | 测试 deadline 超时行为 |

## 与候选查询管道的集成

详见 [候选词选词算法](candidate-selection.md)。Deadline 检查点嵌入在四步流程的每一步之间：

1. **拼写图构建** — `deadline_us < 10ms` 时跳过 Syllabifier（因其内部不检查 deadline）
2. **路径枚举** — Syllabifier 内部通过枚举上限（10000 条）控制，不受 deadline 影响
3. **路径过滤** — 每条路径 `has_prefix` 前检查 deadline
4. **候选查找** — 每条路径 `lookup_by_ids` 前检查 + 扫描中每 64 条检查，结果进入 TopKCollector

## 日志与观测

`QueryTrace` 在 `deadline_exceeded` 时触发强制日志输出（不受采样率限制）。JSONL 格式：

```json
{"q":42,"input":"sdf","total_us":9300,"deadline":false,"truncated":false,"exact_scan":0,"prefix_scan":256}
```

详见 [可观测性设计](observability.md)。

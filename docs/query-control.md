# 查询预算与候选收集策略

## 概述

候选查询可能因路径枚举、词典扫描等操作消耗大量时间。查询预算机制提供三种控制手段：

1. **Deadline**：基于时间的截止限制，超时立即返回当前结果
2. **Scan Budget**：基于扫描条目数的限制，防止单次查询扫描过多索引项
3. **Top-K 收集**：固定容量候选收集器，扫描过程中限制候选数量，避免全量排序

三者通过 `QueryBudget` 统一结构传递，在查询管道的多个检查点执行。

## 数据结构

### QueryDeadline

```cpp
// shared/include/cxxime/query_budget.h
struct QueryDeadline {
    using Clock = std::chrono::steady_clock;
    bool enabled = false;               // deadline_ms=0 时为 false
    Clock::time_point expires_at{};     // 过期时间点
    uint32_t check_interval = 64;       // 每 N 个 posting/路径检查一次

    static QueryDeadline from_now(uint32_t deadline_ms);
    bool expired() const;               // enabled && now >= expires_at
};
```

每次查询通过 `from_now()` 创建独立的 deadline，避免复用过期的 `expires_at`。

### QueryBudget

```cpp
// shared/include/cxxime/query_budget.h
struct QueryBudget {
    uint32_t max_exact_scan = 512;      // 精确匹配最大扫描条目数
    uint32_t max_prefix_scan = 2048;    // 前缀匹配最大扫描条目数
    uint32_t max_user_scan = 256;       // 用户词索引最大扫描条目数
    uint32_t max_results_before_merge = 64;  // TopK 容量（每次 lookup 的候选收集上限）
    uint32_t topk = 0;                  // translator 层合并容量上限（预留，当前未使用）
    QueryDeadline deadline;             // 时间预算
};
```

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

Engine 层在每次查询时根据输入长度自动创建预算，设置 per-query deadline：

```cpp
// engine.cc
QueryDeadline per_query_deadline = QueryDeadline::from_now(query_deadline_ms_);
QueryBudget effective_budget = make_budget(pinyin.size(), page_size);
effective_budget.deadline = per_query_deadline;
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
  │ 创建 per_query_deadline = QueryDeadline::from_now(query_deadline_ms_)
  │ 创建 effective_budget = make_budget(input_len, page_size)
  │ effective_budget.deadline = per_query_deadline
  │ 运行 Processor
  │ 检查 deadline → 超时则跳过 translate
  ▼
PinyinTranslator::translate(budget, trace)
  │ 检查 deadline → 超时则跳过 Syllabifier
  │ Syllabifier::segment(deadline) — 内部 DFS 每 check_interval 路径检查
  │ 检查 deadline → 每条路径 has_prefix 前检查
  │ has_prefix 路径过滤
  │ 检查 deadline → 每条路径 lookup 前检查
  ▼
Dict::lookup_by_ids(budget, trace)
  │ 进入循环前检查 deadline（捕获上游已耗尽预算）
  │ 精确匹配扫描：每 check_interval 条检查 deadline + max_exact_scan
  │ 前缀匹配扫描：每 check_interval 条检查 deadline + max_prefix_scan
  │ 候选 → seen 去重 → TopKCollector.offer()
  ▼
返回 collector.finish()（已排序，大小 ≤ limit）
```

共 8 个检查点，覆盖从引擎入口到词典扫描的完整路径。

### 各检查点行为

| 检查点 | 位置 | 超时行为 | 截断行为 |
|--------|------|----------|----------|
| Engine 入口 | `engine.cc` | 跳过 translate，返回空候选 | 设置 `deadline_exceeded` + `truncated` |
| Syllabifier DFS | `syllabifier.cc` 每 check_interval 路径 | 中断路径枚举，返回已生成路径 | 设置 `deadline_exceeded` + `truncated` |
| has_prefix 前 | `pinyin_translator.cc` | 跳过当前路径及后续路径 | 设置 `deadline_exceeded` + `truncated` |
| lookup 前 | `pinyin_translator.cc` | 跳过当前路径及后续路径 | 设置 `deadline_exceeded` + `truncated` |
| Dict 循环前 | `dict.cc` | 跳过扫描 | 设置 `deadline_exceeded` + `truncated` |
| 精确扫描中 | `dict.cc` 每 check_interval 条 | 中断扫描 | 设置 `deadline_exceeded` + `truncated` |
| 前缀扫描中 | `dict.cc` 每 check_interval 条 | 中断扫描 | 设置 `deadline_exceeded` + `truncated` |
| 用户词索引扫描中 | `dict.cc` lookup_user_* 方法 | 中断扫描 | 设置 `deadline_exceeded` + `truncated` |

> deadline 到期时**同时设置** `deadline_exceeded=true` 和 `truncated=true`。

## 扫描流程

`Dict::lookup_by_ids()` 的完整流程：

```
TopKCollector collector(min(limit, max_results_before_merge))
seen_set（容量上限 = max_results_before_merge * 2）

进入循环前：检查 deadline（捕获上游已耗尽预算）

精确匹配扫描循环：
  每 check_interval 条检查 deadline
  超过 max_exact_scan → truncated, break
  候选 → seen 去重 → collector.offer()

前缀匹配扫描循环：
  每 check_interval 条检查 deadline
  超过 max_prefix_scan → truncated, break
  候选 → seen 去重 → collector.offer()

return collector.finish()  // 已排序，大小 ≤ limit
```

### 去重策略

候选首屏规模很小（≤128），采用小数组线性去重替代 `unordered_set`：

```cpp
// pinyin_translator.cc / dict.cc
static bool contains_text(const std::vector<Candidate>& items, const std::string& text);
static bool contains_ids(const std::vector<std::vector<uint32_t>>& items,
                         const std::vector<uint32_t>& ids);
```

流程：遍历已收集候选，逐个比较 text/ids。候选数通常 < 100，线性扫描比 hash set 更快（无哈希计算、无堆分配、cache 友好）。

### QueryScratch（查询复用缓冲区）

每 Engine 持有一份 `QueryScratch`，随 session 复用，避免 `translate()` 每次查询堆分配临时容器：

```cpp
// engine/include/cxxime/query_scratch.h
struct QueryScratch {
    std::vector<std::vector<uint32_t>> id_sequences;
    std::vector<std::vector<uint32_t>> live_ids;
    std::vector<Candidate> merged_candidates;
    std::vector<Candidate> temp_candidates;
    std::vector<uint32_t> seen_hashes;
    std::vector<uint32_t> path_ids;

    void reset_for_query();   // clear() 所有 vector
    void trim_if_large();     // capacity > 256 时 shrink_to_fit()
};
```

Engine 在 `process_key()` 开始时调用 `scratch_.reset_for_query()`，传入 `translator_.translate()`。单个 vector capacity 超过 256 时在查询结束后 shrink。

## truncated 语义

`truncated=true` 表示返回结果不是完整候选全集，而是按策略有意截断。它不代表质量差，表示查询按低时延策略停止继续搜索。

### 触发条件

```
truncated = true 当且仅当以下任一条件成立：
1. exact scan 达到 make_budget() 分配的 max_exact_scan
2. prefix scan 达到 make_budget() 分配的 max_prefix_scan
3. user scan 达到 max_user_scan（默认 256）
4. seen set（去重表）达到 max_results_before_merge * 2 容量上限
5. deadline_exceeded（时间预算耗尽）
```

### 不触发场景

- 查询本身无 live path（空结果，不是截断）
- posting list 扫描完且候选数 ≤ page_size

## 使用方式

### Engine 自动创建 Deadline

`Engine::process_key()` 在每次按键处理开始时自动创建 `QueryDeadline::from_now(query_deadline_ms_)`，无需手动设置。

### 外部设置 Deadline

通过 `Engine::set_query_deadline_ms()` 设置：

```cpp
engine.set_query_deadline_ms(30);   // 30ms（默认）
engine.set_query_deadline_ms(0);    // 关闭 deadline（调试/离线验证）
```

Scan budget 和 TopK 由 `make_budget()` 自动管理，无需手动设置。Deadline 通过 `QueryDeadline::from_now()` 在每次查询时创建独立预算。

### 当前使用场景

| 场景 | Deadline | 说明 |
|------|----------|------|
| 生产环境（Server） | 30ms（默认） | `query_deadline_ms_ = 30` |
| query_bench 工具 | 30ms（默认） | `--deadline-ms` 参数可调 |
| 单元测试 | 按测试需要设置 | 测试 deadline 超时行为 |

## 与候选查询管道的集成

详见 [候选词选词算法](candidate-selection.md)。Deadline 检查点嵌入在四步流程的每一步之间：

1. **拼写图构建** — Syllabifier 内部 DFS 每 `check_interval` 路径检查 deadline
2. **路径枚举** — Syllabifier 到期时返回已生成路径，标记 `deadline_exceeded` + `truncated`
3. **路径过滤** — 每条路径 `has_prefix` 前检查 deadline
4. **候选查找** — 循环前检查 + 每条路径 `lookup_by_ids` 前检查 + 扫描中每 `check_interval` 条检查

## 日志与观测

`QueryTrace` 在 `deadline_exceeded` 时触发强制日志输出（不受采样率限制）。JSONL 格式：

```json
{"q":42,"input":"sdf","total_us":9300,"deadline":false,"truncated":false,"exact_scan":0,"prefix_scan":256}
```

详见 [可观测性设计](observability.md)。

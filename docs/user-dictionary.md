# 用户词典：内存多路索引

## 概述

用户词典存储用户选词频率记录，用于个性化候选排序。数据量极小（几百到几千条，< 1MB），全部驻留内存，通过 TSV 文件持久化。

查询路径：用户词按 `code` 建立 `exact`、`prefix`、`abbr`、`mixed` 四路查询索引，另有 `text` 反查表和按 code 排序的二分数组，消除全表线性扫描。索引只保存 entry id（`uint32_t`），避免 `vector` 扩容导致指针失效。

## 数据结构

```cpp
// engine/include/cxxime/dict.h
using UserEntryId = uint32_t;

struct UserEntry {
    std::string text;       // 候选文本，如 "输入法"
    std::string code;       // 原始输入码，如 "shurufa" 或 "srf"
    std::string syllables;  // 可选冒号形式，如 "shu:ru:fa"
    std::string abbr_code;  // 缩写，如 "srf"
    std::vector<std::string> mixed_keys;  // 缓存的 mixed keys，用于 bucket 重排
    int frequency = 1;
    uint64_t sequence = 0;  // 递增序号，用于调频排序
    bool deleted = false;   // 软删除标记
};

struct UserBucket {
    std::vector<UserEntryId> ids;
};
```

### 索引结构

| 索引 | 类型 | 用途 |
|------|------|------|
| `user_exact_index_` | `unordered_map<string, UserBucket>` | 精确匹配（完整 code） |
| `user_prefix_index_` | `unordered_map<string, UserBucket>` | 前缀匹配（长度 1..6） |
| `user_abbr_index_` | `unordered_map<string, UserBucket>` | 缩写匹配（首字母组合） |
| `user_mixed_index_` | `unordered_map<string, UserBucket>` | 混合匹配（声母增强简拼 / 首音节展开 / 前两音节展开 / 长词首字母码） |
| `user_text_index_` | `unordered_map<string, size_t>` | 文本反查（O(1)） |
| `user_code_sorted_` | `vector<UserEntryId>` | 按 code 字节序排序，用于长前缀二分查找 |

版本追踪：

```cpp
uint64_t user_dict_version_ = 0;  // 每次更新递增，用于 cache 失效
uint64_t user_sequence_ = 0;      // 全局递增序号
```

## 索引 Key 生成

用户词插入或加载时生成以下 key：

| 索引 | 生成规则 | 示例 |
|------|---------|------|
| `exact` | 原始 code | `shurufa`、`srf` |
| `prefix` | exact 的长度 1..6 前缀 | `s`、`sh`、`shu` |
| `abbr` | 每个音节首字母（需 syllables） | `shu:ru:fa` → `srf` |
| `mixed` | `generate_mixed_keys()` 统一生成（需 syllables） | 见下表 |

Mixed code 码型（每词最多 8 个，去重后截断）：

| 码型 | 规则 | 示例 |
|------|------|------|
| 声母增强简拼 | zh/ch/sh 取双字母声母，其余取首字母 | `shu:ru:fa` → `shrf` |
| 长词首字母码 | 5+ 音节，每音节取首字母 | `zhong:hua:ren:min:gong:he:guo` → `zhrmghg` |
| 首音节展开 | 第一音节全拼 + 后续首字母 | `shu:ru:fa` → `shurf` |
| 前两音节展开 | 前两音节全拼 + 后续首字母（3+ 音节） | `bei:jing:da:xue` → `beijidx` |
| 首字母+第二音节 | 第一首字母 + 第二音节全拼 + 后续首字母（3+ 音节） | `bei:jing:da:xue` → `bjingdx` |
| 首音节前两字母 | 第一音节前两字母 + 后续首字母 | `shu:ru:fa` → `shrf` |

mixed bucket 裁剪到 `kMaxUserBucketSize = 64`，按 (frequency desc, sequence desc, id asc) 排序。

`prefix` 只保存长度 1..6 的短前缀，服务短输入快速路径。更长的前缀使用 `user_code_sorted_` 二分范围查询。

## 操作映射

| 操作 | 实现 | 复杂度 |
|------|------|--------|
| 精确匹配（`lookup_by_syllables`） | `user_exact_index_[code]` → 扫描 bucket | O(bucket_size) |
| 前缀匹配（`lookup`） | `user_prefix_index_[prefix]` 或 `user_code_sorted_` 二分 | O(bucket_size) 或 O(log n) |
| 缩写/混合匹配（短输入快速路径） | `user_abbr_index_` + `user_mixed_index_` | O(bucket_size) |
| 文本反查（`reverse_lookup`） | `user_text_index_.find(text)` | O(1) |
| 计数（`count`） | 索引 bucket 大小求和或二分范围计数 | O(1) 或 O(log n) |
| 更新频率（`update_frequency`） | 查找 → 更新/插入 → 维护索引 | O(1) + 索引维护 |

## 查询 API

### 内部索引查询方法

```cpp
struct UserLookupStats {
    uint32_t scan_count = 0;       // 实际检查的索引项数
    bool truncated = false;        // 达到 max_user_scan 上限
    bool scan_budget_truncated = false; // scan 预算耗尽
    bool deadline_exceeded = false;
};

std::vector<Candidate> lookup_user_exact(const std::string& code, int limit,
    const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const;

std::vector<Candidate> lookup_user_prefix(const std::string& prefix, int limit,
    const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const;

std::vector<Candidate> lookup_user_short(const std::string& key, int limit,
    const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const;
```

### 公共查询集成

| 方法 | 用户词查询方式 |
|------|--------------|
| `lookup_by_syllables()` | `lookup_user_exact(concat_code)` 补充用户词 |
| `lookup()` | `lookup_user_prefix(code_prefix)` 补充用户词 |
| `count()` | 索引 bucket 大小求和或二分范围计数 |
| `reverse_lookup()` | `user_text_index_` O(1) 反查 |
| 短输入快速路径 | `lookup_user_short()` 查询 exact/prefix/abbr/mixed |

### 扫描预算

用户词查询遵守 `QueryBudget::max_user_scan`（默认 256）。达到上限时设置 `truncated=true`，不再扫描更多索引项。`user_scan_count` 只反映命中 bucket 或二分范围内的实际检查次数，不随用户词总量增长。

## 评分公式

用户词得分由 `score_user_match()`（`dict.cc`）计算：

```
score = base + bounded_frequency + recent_bonus

bounded_frequency = clamp(frequency, 1, 50000)
recent_bonus      = (delta <= 1000) ? 1000 - delta : 0，delta = user_sequence_ - entry.sequence
```

`base` 按匹配类型与评分档位（`UserScoringProfile::kPinyin` / `kWubi`）分层：

| 匹配情形 | base | 说明 |
|---------|------|------|
| 精确匹配（或 key 长度 == 码长） | `kExactBase = 200000000` | 学过的全码词强力置顶 |
| 缩写 / 混合键匹配 | `kPatternBase = 120000000` | 简拼、首字母混合键命中 |
| 前缀匹配，key ≤ 2 字符 | `kWeakPrefixBase = 4000` | 短前缀不干扰系统词序 |
| 前缀匹配，key + 1 ≥ 码长 | `kNearPrefixBase = 800000` | 接近完整的前缀显著提升 |
| 前缀匹配，key × 2 ≥ 码长 | `kMidPrefixBase = 160000` | 中等长度前缀适度提升 |
| 其余前缀匹配 | `kWeakPrefixBase = 4000` | — |
| 五笔档（kWubi）前缀匹配 | `kWeakPrefixBase = 4000` | 五笔前缀一律弱加分，保护系统简码词序 |

`recent_bonus` 奖励最近使用的词条：最近使用的 entry bonus 接近 1000，超过 1000 次更新前的 entry bonus 为 0。

## 更新流程

### load_user_dict

在写锁下执行：

1. 清空 `user_entries_` 和全部索引
2. 读取 TSV（支持 3 列和 4 列格式）
3. 规范化 code、frequency、syllables
4. 逐条 append `UserEntry`
5. 调用 `rebuild_user_indexes_locked()`
6. `user_dict_version_++`

### update_frequency

`update_frequency(text, code, syllables)` 在写锁下执行：

1. 通过 `user_text_index_` 查找文本
2. 已存在且 code 不变：增加 frequency，更新 sequence，`re_sort_user_buckets_()` 重排受影响 bucket
3. 已存在但 code 变化：从旧索引移除，更新 entry，插入新索引
4. 已存在但 syllables 新增：从旧索引移除，更新 entry，插入新索引（生成 abbr/mixed keys）
5. 不存在：append 新 entry，插入全部索引
6. 对 `user_code_sorted_` 重排序
7. `user_dirty_ = true`，`user_dict_version_++`

旧签名 `update_frequency(text, code)` 委托新签名（syllables 为空）。

## Cache 失效

包含用户词候选的缓存均以 `user_dict_version_` 判定新鲜度：

- **长拼音查询页缓存**（`PinyinTranslator` 的 `QueryCacheEntry`）：缓存键包含 `user_dict_version`，版本不一致的条目不会命中，由 LRU 自然淘汰
- **Session recent cache**：同 session 内保留，但版本变化后扫描时过滤——候选 text 已不存在或被标记 `deleted`（`has_user_entry()` 返回 false）时跳过
- 系统短码缓存 `pinyin.topn.bin` 只含系统词，不受用户词版本影响

## 并发策略

继续使用 `std::shared_mutex user_mutex_`：

- 查询持有 `shared_lock`
- 加载、调频、插入、保存持有 `unique_lock`
- 索引保存 entry id，不保存指针
- 不从 `user_entries_` 中物理删除元素；删除使用 `deleted=true` 标记

该策略保证同步查询不需要跨线程协调，也不会因 `vector` reallocation 破坏索引。

## 文件格式

TSV（Tab-Separated Values），支持 3 列（向后兼容）和 4 列格式：

```
text<TAB>code<TAB>frequency                   （3 列，旧格式）
text<TAB>code<TAB>frequency<TAB>syllables     （4 列，新格式）
```

读取时同时接受两种格式。保存时使用 4 列格式，无 syllables 时省略第 4 列。

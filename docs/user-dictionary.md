# 用户词典：内存多路索引

## 概述

用户词典存储用户选词频率记录，用于个性化候选排序。数据量极小（几百到几千条，< 1MB），全部驻留内存，通过 TSV 文件持久化。

查询路径：用户词按 `code` 建立 `prefix`、`abbr`、`mixed` 和 `text` 四路内存索引，消除全表线性扫描。索引只保存 entry id（`uint32_t`），避免 `vector` 扩容导致指针失效。

## 数据结构

```cpp
// engine/include/cxxime/dict.h
using UserEntryId = uint32_t;

struct UserEntry {
    std::string text;       // 候选文本，如 "输入法"
    std::string code;       // 原始输入码，如 "shurufa" 或 "srf"
    std::string syllables;  // 可选冒号形式，如 "shu:ru:fa"
    std::string abbr_code;  // 缩写，如 "srf"
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
| `user_mixed_index_` | `unordered_map<string, UserBucket>` | 混合匹配（全拼首字+首字母） |
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
| `mixed` | 第一音节全拼 + 后续首字母（需 syllables） | `shu:ru:fa` → `shurf` |

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

```
score = user_boost + frequency + recent_bonus

user_boost   = 50000（用户词固定加分，低于系统词精确匹配的 100000）
recent_bonus = min(1000, max(0, 1000 - (user_sequence_ - entry.sequence)))
```

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
2. 已存在且 code 不变：增加 frequency，更新 sequence
3. 已存在但 code 变化：从旧索引移除，更新 entry，插入新索引
4. 不存在：append 新 entry，插入全部索引
5. 对 `user_code_sorted_` 重排序
6. `user_dirty_ = true`，`user_dict_version_++`

旧签名 `update_frequency(text, code)` 委托新签名（syllables 为空）。

## Cache 失效

任何包含用户词候选的 cache 都记录 `user_dict_version`：

```cpp
struct CachedCandidatePage {
    uint64_t user_dict_version = 0;
    CandidatePage page;
};
```

失效规则：

- `user_dict_version` 不一致时丢弃该 cache
- session recent cache 在同 session 内保留，但候选 text 已不存在或被标记 `deleted` 时过滤
- 系统 `topn_cache.bin` 不受用户词版本影响

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

## 修改文件

| 文件 | 改动 |
|------|------|
| `engine/include/cxxime/dict.h` | 扩展 UserEntry、用户索引、版本号、内部查询 API |
| `engine/src/dict.cc` | 索引构建、增量更新、bucket 排序、索引查询 |
| `engine/src/pinyin_translator.cc` | 短输入快速路径接入用户词索引 |
| `engine/src/engine.cc` | 选词和提交时调用带 syllables 的调频接口 |
| `shared/include/cxxime/query_budget.h` | 新增 `max_user_scan` 字段 |
| `shared/include/cxxime/candidate.h` | 新增 `CachedCandidatePage` 结构体 |
| `test/dict_test.cc` | 用户词索引测试（12 项） |

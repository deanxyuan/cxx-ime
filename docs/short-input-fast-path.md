# 短输入快速路径

## 概述

短输入（1–6 小写字母，如 `s`、`sd`、`bj`、`srf`、`nihao`）是生产输入法中最常见的场景。标准查询管道需要 Syllabifier 路径枚举 + 多次 dict scan，延迟在毫秒级。快速路径在 Syllabifier 之前插入三层内存查询，命中时完全跳过路径枚举和词典扫描，将延迟降至个位数微秒。

## 架构

```
Engine::process_key()
  │
  ▼
PinyinTranslator::translate()
  │
  ├─ is_short_key(pinyin)?  ──否──→ 标准管道 (Syllabifier + Dict)
  │       │
  │      是
  │       ▼
  │  lookup_short_fast(key, limit)
  │       │
  │       ├─ 1. Session Recent Cache (内存, 每 session)
  │       │     最近用户选词/提交的候选, LRU 淘汰
  │       │
  │       ├─ 2. User Dict Short Index (内存多路索引)
  │       │     用户词 exact/prefix/abbr/mixed 索引查询
  │       │
  │       └─ 3. ShortCodeCache (pinyin.topn.bin, 预构建)
  │             离线生成的 Top-N 候选索引, 二分查找
  │       │
  │       ▼
  │  候选足够? ──是──→ 返回缓存页 (cache_hit=true, scan=0)
  │       │
  │      否 (回退)
  │       ▼
  └─ 标准管道 (Syllabifier + Dict), 用缓存候选 seed TopKCollector
```

## Short Code Cache

### 二进制格式 (`pinyin.topn.bin`)

与 `pinyin.dict.bin` 放在同一目录，文件整体读入堆内存（不使用 mmap）。

```
┌─────────────────────────────────────┐
│ ShortCacheHeader (36 bytes)         │
│   magic: "CXTOPN\x01\0"            │
│   version: 1                        │
│   key_count                         │
│   candidate_count                   │
│   string_data_size                  │
│   keys_offset → ShortKeyEntry[]     │
│   candidates_offset → ShortCand[]   │
│   strings_offset → packed strings   │
├─────────────────────────────────────┤
│ ShortKeyEntry[key_count] (16 bytes) │  ← 按 key 字节序排序, 二分查找
│   candidate_offset                  │
│   candidate_count                   │
│   key_offset → string data          │
│   key_len                           │
│   flags (exact/abbr/mixed/prefix)   │
├─────────────────────────────────────┤
│ ShortCandidateEntry[cand_count]     │  (24 bytes)
│   text_offset, text_len             │
│   comment_offset, comment_len       │
│   frequency, score                  │
├─────────────────────────────────────┤
│ string data (packed)                │
│   keys + candidate text + comments  │
└─────────────────────────────────────┘
```

### Key 生成策略

对每条词典记录（syllable_ids 如 `shu:ru:fa`）生成以下 key：

| key 类型 | 生成规则 | 示例 |
|----------|---------|------|
| exact_code | 完整拼音拼接 | `shurufa` |
| abbr_code | 每个音节首字母 | `srf` |
| mixed_code | `generate_mixed_keys()` 统一生成（每条最多 8 个，最长 16 字符） | `shrf`, `shurf`, `zhrmghg` |
| prefix_code | 对以上 key 取长度 1..6 的前缀 | `s`, `sr`, `sh`, `shu`, ... |

mixed code 码型：声母增强简拼（`shrf`）、首音节展开（`shurf`）、前两音节展开（`beijidx`）、长词首字母码（`zhrmghg`）等。exact/abbr/prefix 受 `max_short_key_len = 6` 限制，mixed code 受 `max_code_len = 16` 限制。每个 key 最多保留 64 个候选。

### 排序规则

```
score = frequency
      + exact_complete_bonus   (100000)
      + exact_prefix_bonus     (50000)
      + mixed_bonus            (10000)
      + abbr_bonus             (5000)
```

同分时按 `match_type_priority desc → frequency desc → text_length asc → text lexicographic asc` 稳定排序。

### 构建脚本

```
scripts/build_short_cache.py --input data/pinyin.dict.db --output data/pinyin.topn.bin
```

数据流：`pinyin.dict.db` (SQLite) → 生成 key/candidate 对 → 按 key 字节序排序 → 写入二进制文件。

### C++ 类

```cpp
// engine/include/cxxime/short_code_cache.h
class ShortCodeCache {
public:
    bool load(const std::string& path);   // CreateFileA + ReadFile, 堆加载
    void unload();
    bool is_loaded() const;
    std::vector<Candidate> lookup(const std::string& key, int limit,
                                   QueryTrace* trace = nullptr) const;
    // lookup: 二分查找 keys_[], 命中时设置 trace->cache_hit = true
};
```

`Dict::open_dict()` 在加载 `pinyin.dict.bin` 后自动尝试加载同目录的 `pinyin.topn.bin`。缺失时允许启动（回退 bounded lookup），Release 打包必须包含该文件。

## Session Recent Cache

Engine 持有每 session 的最近候选缓存，记录用户选词和提交行为：

```cpp
// engine/include/cxxime/translator.h
struct RecentCandidate {
    std::string key;
    Candidate candidate;
    uint64_t sequence = 0;    // 递增序号, 用于 LRU 淘汰
};
```

### 策略

| 参数 | 值 | 说明 |
|------|-----|------|
| key 长度范围 | 1..6 | 只记录短输入的候选 |
| 每 key 最多保留 | 8 | `kMaxRecentPerKey` |
| 每 session 最多 | 128 key | `kMaxRecentKeys`, 超出时淘汰 sequence 最小的 |
| 存储 | 仅内存 | 不写盘, session 结束即丢弃 |

### 更新时机

- `Engine::select_candidate()` — 用户选词时
- `Engine::process_key()` COMMITTED 分支 — 用户提交时（空格、数字选词后直接上屏）

### 查询顺序

`lookup_short_fast()` 内部：
1. Session Recent Cache（最高优先级, 用户个性化）
2. User Dict Short Index（用户词多路索引: exact → prefix → abbr → mixed）
3. ShortCodeCache（预构建 Top-N）
4. 按 text 去重, 合并为候选列表

## Translator 集成

### 快速路径入口

```cpp
// pinyin_translator.cc — translate() 开头
const int need = offset + fetch_limit + 1;
if (is_short_key(pinyin)) {
    fast = lookup_short_fast(pinyin, need, trace);
    if (fast.hit && fast.candidates.size() > offset) {
        // 缓存足够 → 直接返回分页结果
        // 设置 cache_hit=true, exact_scan=0, prefix_scan=0, deadline_exceeded=false
        return page;
    }
    // 缓存不足 → seed TopKCollector, 继续标准管道
}
```

### Short Key Gate 条件

```cpp
bool is_short_key(const std::string& pinyin) {
    // 全部小写字母, 长度 1..6, 非空
}
```

### 翻页行为

- `page_index == 0`：缓存通常足够，直接返回
- `page_index > 0`：同样尝试缓存；缓存不足时回退标准管道，缓存候选 seed 到 TopKCollector 避免重复查询

### 合并规则

1. Session recent 候选优先
2. User dict short index 候选（按 `score_user_match()` 分层评分：精确/缩写/混合键高基数，前缀按接近程度分档，详见 [用户词典设计](user-dictionary.md)）
3. ShortCodeCache 候选按构建时 score 排序
4. 标准管道 (bounded dict lookup) 只用于补足缺失候选
5. 按 `Candidate.text` 去重
6. 缓存候选超过当前页时设置 `truncated=true`

### 用户词版本过滤

Session recent cache 中的用户词候选在以下情况被过滤：
- `user_dict_version` 发生变化（用户词典被重新加载或更新）
- 候选 text 在当前用户词典中已不存在或被标记 `deleted`（`has_user_entry()` 返回 false）

过滤在 `lookup_short_fast()` 扫描 recent 缓存时执行，确保 stale 用户词不会出现在候选列表中。

## Trace 语义

### cache_hit

| 场景 | cache_hit |
|------|-----------|
| session recent 返回 ≥1 候选 | true |
| ShortCodeCache 返回 ≥1 候选 | true |
| short key gate 未通过 | 不设置 |
| cache miss, 仅 bounded lookup 返回 | 不设置 |

### 纯 cache 命中时的 trace 值

```
cache_hit = true
exact_scan_count = 0
prefix_scan_count = 0
user_scan_count = 0        (用户词索引命中空 bucket 或无用户词)
deadline_exceeded = false
syllable_path_count = 0    (Syllabifier 未调用)
live_path_count = 0
```

### truncated

| 场景 | truncated |
|------|-----------|
| 缓存候选 > page_size, 截断 | true |
| 缓存不足, 回退 bounded lookup 时触发 scan budget / deadline | true |

## 文件清单

| 文件 | 职责 |
|------|------|
| `engine/src/short_code_cache_format.h` | 二进制格式结构体定义 |
| `engine/include/cxxime/short_code_cache.h` | ShortCodeCache 类声明 + ShortKeyFlag 枚举 |
| `engine/src/short_code_cache.cc` | 加载、校验、二分查询、create_test_cache |
| `scripts/build_short_cache.py` | 离线构建 pinyin.topn.bin |
| `engine/include/cxxime/dict.h` | ShortCodeCache 成员 + getter, 用户词索引结构 |
| `engine/src/dict.cc` | open_dict() 加载 topn.bin, 用户词索引构建与查询 |
| `engine/include/cxxime/translator.h` | RecentCandidate 结构体, update_recent(), is_short_key() |
| `engine/src/pinyin_translator.cc` | 快速路径入口, 合并逻辑, session recent 管理, 用户词版本过滤 |
| `engine/src/engine.cc` | select/commit 时更新 recent cache |
| `test/short_cache_test.cc` | 单元测试 (9 cases) |

## 长输入的查询页缓存

作为短输入快速路径的姊妹机制，`PinyinTranslator` 为 >6 字符的长拼音查询提供了独立的查询页缓存：

- **触发条件**：`input.size() > 6`，短输入走 `lookup_short_fast` 不触发此缓存
- **容量**：LRU 64 条（`kMaxQueryCacheEntries = 64`），`sequence` 递增序号实现淘汰
- **失效**：`user_dict_version` 变化后全部缓存自动失效
- **非缓存场景**：deadline 命中或 deadline_exceeded 时不写入

详细机制见 [候选词选词算法 — 长输入查询页缓存](candidate-selection.md#长输入查询页缓存)。

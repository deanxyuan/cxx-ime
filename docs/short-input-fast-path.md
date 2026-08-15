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
  ├─ is_indexable_key(pinyin)?  ──否──→ 标准管道 (Syllabifier + Dict)
  │       │
  │      是
  │       ▼
  │  lookup_indexed_fast(key, limit)
  │       │
  │       ├─ 1. Session Recent Cache (内存, 每 session)
  │       │     最近用户选词/提交的候选, LRU 淘汰
  │       │
  │       ├─ 2. User Dict Short Index (内存多路索引)
  │       │     用户词 exact/prefix/abbr/mixed 索引查询
  │       │
  │       └─ 3. ShortCodeCache (pinyin.topn.bin, DAT-16)
  │             Darts-clone 双数组 Trie 查找, O(k)
  │       │
  │       ▼
  │  complete_index_hit? ──是──→ 返回缓存页 (cache_hit=true, scan=0)
  │       │
  │      否 (回退)
  │       ▼
  └─ 标准管道 (Syllabifier + Dict), 用缓存候选 seed TopKCollector
```

## Short Code Cache

### 二进制格式 (`pinyin.topn.bin`, CXTOPN v2 DAT-16)

与 `pinyin.dict.bin` 放在同一目录，文件整体读入堆内存。格式为 **DAT-16**（magic `CXTOPN\x02\x00`，version=2，layout=2）。键查找基于 **Darts-clone** 双数组 Trie（O(k)），候选条目内联存储（16 字节/条）。

```
┌────────────────────────────────────────────┐
│ ShortCacheHeader (80 bytes)                │
│   magic[8]      = "CXTOPN\x02\0"          │
│   version       = 2                        │
│   header_size   = 80                       │
│   layout        = 2 (kDat16)               │
│   file_size                                │
│   key_count                               │
│   code_index_count    → Darts units[]      │
│   posting_list_count  → ShortPostingList[] │
│   posting_count       → ShortCandidateEntry[]│
│   candidate_count     = 0 (内联模式)       │
│   key_string_size     = 0 (Trie 隐式键)   │
│   candidate_string_size                    │
│   code_index_offset      (→ Darts units)  │
│   posting_lists_offset   (→ ShortPostingList[])│
│   postings_offset        (→ ShortCandidateEntry[])│
│   candidates_offset      (未使用)         │
│   key_strings_offset     (未使用)         │
│   candidate_strings_offset (→ packed text) │
├────────────────────────────────────────────┤
│ Darts units[code_index_count] (uint32)     │ ← Darts-clone Double Array Trie
│   键索引，支持 O(k) exactMatchSearch       │
├────────────────────────────────────────────┤
│ ShortPostingList[posting_list_count]       │ ← 8 bytes each
│   posting_offset  (uint32)                 │
│   posting_count   (uint16)                 │
│   flags           (uint16)                 │
│     kShortPostingPrefixComplete = 0x0001   │
├────────────────────────────────────────────┤
│ ShortCandidateEntry[posting_count]         │ ← 16 bytes each (inline)
│   text_offset, text_length  (uint32 each)  │
│   frequency, score          (int32 each)   │
├────────────────────────────────────────────┤
│ candidate_strings (packed UTF-8)            │
└────────────────────────────────────────────┘
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
score = frequency  (最大 100,000,000 基数)
      + exact_complete_bonus   (100,000,000)
      + exact_prefix_bonus     (80,000,000)
      + abbr_complete_bonus    (60,000,000)
      + mixed_complete_bonus   (50,000,000)
      + abbr_prefix_bonus      (30,000,000)
      + mixed_prefix_bonus     (20,000,000)
```

同分时按 `match_type_priority desc → frequency desc → text_length asc → text lexicographic asc` 稳定排序。每个 key 最多保留 64 个候选。

### 构建流程

```cmd
# 1. Python 生成候选键与评分
python scripts/build_pinyin_topn.py --input data/pinyin.dict.db --output data/pinyin.topn.intermediate.bin

# 2. topn_builder 转换中间文件为 DAT-16（构建 Darts-clone Trie）
build\tools\topn_index\Release\topn_builder.exe --input data/pinyin.topn.intermediate.bin --output data/pinyin.topn.bin --format dat16
```

`prepare_dictionary_bundle.py` 自动完成上述两步，产出最终 `pinyin.topn.bin`。

数据流：`pinyin.dict.db` (SQLite) → `build_pinyin_topn.py`（键生成 + 评分排序）→ `topn_builder --format dat16`（Darts-clone Trie 构建 + 写入 DAT-16）。键隐式存储于 Trie 结构中，候选文本共享字符串池并内联写入。

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
    // lookup: Darts-clone Double Array Trie 遍历 key,
    //   命中时设置 trace->cache_hit = true 并返回候选
};
```

`parse_short_cache()` 验证 header 为 `CXTOPN\x02\x00`，layout 必须为 `kDat16`（=2）。`lookup()` 通过 `darts_offset()` 解码 trie 单元，沿 key 字符遍历 Darts 状态转移，到达叶节点后读取 `ShortPostingList`，返回内联 `ShortCandidateEntry` 数组。

`Dict::open_dict()` 在加载 `pinyin.dict.bin` 后自动尝试加载同目录的 `pinyin.topn.bin`。在 `open_bundle()`（Server 路径）中，Top-N 文件缺失是致命错误；独立模式下缺失时静默回退。

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

`lookup_indexed_fast()` 内部：
1. Session Recent Cache（最高优先级, 用户个性化）
2. User Dict Short Index（用户词多路索引: exact → prefix → abbr → mixed）
3. ShortCodeCache（DAT-16 Top-N，Darts trie 查找）
4. 按 text 去重, 合并为候选列表

## Translator 集成

### 快速路径入口

```cpp
// pinyin_translator.cc — translate() 开头
const int need = offset + fetch_limit + 1;
if (is_indexable_key(pinyin)) {
    fast = lookup_indexed_fast(pinyin, need, trace);
    if (fast.complete_index_hit && fast.candidates.size() > offset) {
        // 缓存足够 → 直接返回分页结果
        // 设置 cache_hit=true, exact_scan=0, prefix_scan=0, deadline_exceeded=false
        return page;
    }
    // 缓存不足 → seed TopKCollector, 继续标准管道
}
```

### Indexable Key Gate 条件

```cpp
bool is_indexable_key(const std::string& pinyin) {
    // 全部小写字母, 长度 1..6, 非空
}
```

### 翻页行为

- `page_index == 0`：缓存通常足够，直接返回
- `page_index > 0`：同样尝试缓存；缓存不足时回退标准管道，缓存候选 seed 到 TopKCollector 避免重复查询

### 合并规则

1. Session recent 候选优先
2. User dict short index 候选（按 `score_user_match()` 分层评分：精确/缩写/混合键高基数，前缀按接近程度分档，详见 [用户词库与候选偏好](user-dictionary.md)）
3. ShortCodeCache 候选按构建时 score 排序
4. 标准管道 (bounded dict lookup) 只用于补足缺失候选
5. 按 `Candidate.text` 去重
6. 缓存候选超过当前页时设置 `truncated=true`

### 用户词版本过滤

Session recent cache 中的用户词候选在以下情况被过滤：
- `user_dict_version` 发生变化（用户词库被重新加载或更新）
- 候选 text 在当前用户词库中已不存在或被标记 `deleted`（`has_user_entry()` 返回 false）

过滤在 `lookup_indexed_fast()` 扫描 recent 缓存时执行，确保 stale 用户词不会出现在候选列表中。

## Trace 语义

### cache_hit

| 场景 | cache_hit |
|------|-----------|
| session recent 返回 ≥1 候选 | true |
| ShortCodeCache 返回 ≥1 候选 | true |
| indexable key gate 未通过 | 不设置 |
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
| `engine/src/short_code_cache_format.h` | DAT-16 二进制格式结构体定义（ShortCacheHeader 80B, ShortPostingList 8B, ShortCandidateEntry 16B） |
| `engine/include/cxxime/short_code_cache.h` | ShortCodeCache 类声明 |
| `engine/src/short_code_cache.cc` | 加载、校验 DAT-16 header, Darts trie 查找（darts_offset 解码）, create_test_cache |
| `tools/topn_index/topn_index_format.h` | Top-N 三种布局枚举（kFlat16/kDat16/kDat8）及格式结构体 |
| `tools/topn_index/index_writer.cc` | write_index(): Darts::DoubleArray::build() 构建 trie, 写入 DAT-16 |
| `tools/topn_index/index_reader.cc` | IndexReader: DAT trie 遍历查找, 候选读取 |
| `tools/topn_index/intermediate_reader.cc` | IntermediateReader: 读取 Top-N 中间文件格式 |
| `tools/topn_index/main.cc` | topn_builder 入口: 中间文件 → DAT-16 转换 |
| `tools/topn_index/benchmark.cc` | topn_benchmark 工具: 跨格式延迟/QPS 对比 |
| `third_party/darts-clone/include/darts.h` | Darts-clone Double Array Trie 库（构建 + 查找） |
| `scripts/build_pinyin_topn.py` | 离线生成 Top-N 候选键与评分 |
| `scripts/benchmark_topn.ps1` | 跨格式 benchmark 脚本 |
| `engine/include/cxxime/dict.h` | ShortCodeCache 成员 + getter, 用户词索引结构 |
| `engine/src/dict.cc` | open_dict() / open_bundle() 加载 topn.bin, 用户词索引构建与查询 |
| `engine/include/cxxime/translator.h` | RecentCandidate 结构体, update_recent(), is_short_key() |
| `engine/src/pinyin_translator.cc` | 快速路径入口, 合并逻辑, session recent 管理, 用户词版本过滤 |
| `engine/src/engine.cc` | select/commit 时更新 recent cache |
| `test/short_cache_test.cc` | ShortCodeCache 单元测试 (16 cases) |
| `test/util/topn_test_data.h` / `.cc` | Top-N 测试数据辅助函数 |

## 长输入的查询页缓存

作为短输入快速路径的姊妹机制，`PinyinTranslator` 为 >6 字符的长拼音查询提供了独立的查询页缓存：

- **触发条件**：`input.size() > 6`，短输入走 `lookup_short_fast` 不触发此缓存
- **容量**：LRU 64 条（`kMaxQueryCacheEntries = 64`），`sequence` 递增序号实现淘汰
- **失效**：`user_dict_version` 变化后全部缓存自动失效
- **非缓存场景**：deadline 命中或 deadline_exceeded 时不写入

详细机制见 [候选词选词算法 — 长输入查询页缓存](candidate-selection.md#长输入查询页缓存)。

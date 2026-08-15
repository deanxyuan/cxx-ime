# 候选词选词算法

## 整体流程

```
用户输入 → TSF → IPC → Engine::process_key → PinyinProcessor → PinyinTranslator → 候选词列表
```

`PinyinTranslator::translate()` 是核心，分四步：

### 1. 拼写图构建（Syllabifier）

将输入字符串（如 `"sdf"`）构建为拼写图。图中每个节点是输入位置（0, 1, 2, 3），边是输入片段→完整拼音音节的映射。

```
输入: s d f
      │ │ │
      ▼ ▼ ▼
     s→shui  (缩写, 消耗1字符, 信度-0.693)
     s→sa    (缩写, 消耗1字符, 信度-1.2)
     ...
     d→dian  (缩写, 消耗1字符)
     d→da    (缩写, 消耗1字符)
     ...
     f→fei   (缩写, 消耗1字符)
     f→fa    (缩写, 消耗1字符)
```

每个位置的边来自 `SpellingsIndex::prefix_search()`（Patricia trie 前缀搜索）。三种拼写类型：

| 类型 | 示例 | 说明 |
|------|------|------|
| 正常拼写 | `"shu"→"shu"` | 输入=音节，完全匹配 |
| 模糊拼写 | `"zong"→"zhong"` | 模糊音纠正（z/zh 不分） |
| 缩写 | `"s"→"shui"` | 声母缩写，消耗 1 字符 |

### 2. 路径枚举（enumerate_paths）

从图中枚举从位置 0 到最远可达位置的所有音节路径。

```
图:   0──s→1──d→2──f→3
       ├─sha→1
       ├─shai→1
       └─...×35

路径: ["shui","dian","fei"]
      ["sa","da","fa"]
      ["shi","da","feng"]
      ...
```

**关键优化：**

- **信度排序**：DFS 前将每条边的候选音节按信度降序排列。正常拼写（信度 0）优先，缩写中信度越高（越常见）越优先。这确保好路径在截断前被枚举到。

- **枚举上限**：最多枚举 256 条路径（`kMaxPaths = 256`）。超过则 DFS 提前退出。translator 只取前 64 条路径（`PinyinTranslator::kMaxPaths = 64`），256 条已留出 4 倍余量。密集缩写图（如 11 字符全拼产生 154 条边）在无上限时可生成 10,000+ 条路径，降至 256 后同一输入 <1ms 完成。

- **提前退出**：DFS 每层检查 `results.size() >= kMaxPaths`，达成即返回。避免穷举 5^N 条路径后才发现超限。

### 3. 路径过滤（has_prefix）

将每条路径的音节转为整数 ID 序列，查询 ID 索引（`id_index_`）：

```
["shui","dian","fei"] → [id(shui), id(dian), id(fei)]
```

二分查找 + 前缀匹配。只有词典中存在对应 N 元音节的路径保留（`live_ids`）。不存在的路径被丢弃（如 `["sa","da","fa"]` ——不存在这个三字词）。

### 4. 候选查找与排序（lookup_by_ids + sort）

对保留的每条路径，在二进制词典中查找所有匹配词条：

```
id_index_ 二分查找: "shui:dian:fei" → entries[117200..117500]
  扫出: 水电费(freq=117200), 税费点(freq=85000), ...
id_index_ 前缀匹配: "shui:dian" → entries[...]
  扫出: 水电(freq=150000), 水点(freq=90000), ...
```

同时查询用户词库索引（`lookup_user_exact` / `lookup_user_prefix`），补充个性化候选。用户词候选按 `score_user_match()` 分层评分（精确/缩写高基数，前缀按接近程度分档，详见 [用户词库与候选偏好](user-dictionary.md)），插入到结果中。

所有路径的结果合并去重，按频率降序排列，分页返回。

### 完整示例：输入 `"sdf"`

```
1. 拼写图: s(35候选)×d(~20候选)×f(~10候选)
2. 枚举: 8050 条路径 (按信度排序)
   5634: shui:dian:fei  (信度之和最高的一批排在前面)
3. 过滤: has_prefix 筛掉不存在的三字词
4. 查找+排序:
   [0] 士大夫  124450  (来自 shi:da:fu)
   [1] 斯蒂芬  117510  (来自 si:di:fen)
   [2] 史蒂夫  117465  (来自 shi:di:fu)
   [3] 水电费  117200  (来自 shui:dian:fei)
   ...
```

## 用户词典查询

用户词典在查询管道中通过多路索引（exact / prefix / abbr / mixed）查询，与系统词典结果合并：

| 查询方法 | 触发条件 | 索引 |
|----------|---------|------|
| `lookup_user_exact` | `lookup_by_syllables()` 精确匹配 | `user_exact_index_` |
| `lookup_user_prefix` | `lookup()` 前缀匹配 | `user_prefix_index_`（code ≤ 6）或 `user_code_sorted_` 二分（code > 6） |
| `lookup_user_short` | 短输入快速路径 | exact → prefix → abbr → mixed |

### lookup_user_prefix 两路查询

`Dict::lookup_user_prefix()` 根据 key 长度走不同的索引路径，两路都在收集后按 `score_user_match()` 评分降序排列，再根据 `max_user_scan` 截断：

- **code ≤ 6**：通过 `user_prefix_index_` 哈希桶定位，桶内遍历全部命中条目，评分排序后截断
- **code > 6**：对 `user_code_sorted_`（按 code 字典序排序的 vector）做 `lower_bound` 二分定位起始位置，线性扫描前缀匹配条目，评分排序后截断

### 用户词评分

用户词评分通过 `score_user_match()` 函数（`dict.cc`）计算，按匹配类型和打分档位分档：

```cpp
// 基础分（按匹配类型）
kExactBase     = 200000000    // 完全匹配（key == code）
kPatternBase   = 120000000    // 缩写/混合匹配
kNearPrefixBase = 800000     // 前缀：key_len + 1 >= code_len
kMidPrefixBase  = 160000     // 前缀：key_len * 2 >= code_len
kWeakPrefixBase = 4000       // 前缀：其余情况，含 key_len <= 2

// 频率加成（bounded）
frequency = clamp(1, frequency, 50000)

// 近期使用加成
recent_bonus = (current_sequence - entry_sequence <= 1000)
             ? 1000 - delta
             : 0

// 最终分
score = base + bounded_frequency + recent_bonus
```

打分分两档：

| 档位 | 条件 | 行为 |
|------|------|------|
| `kPinyin` | 拼音模式（默认） | 完整分档：exact → pattern → nearPrefix → midPrefix → weakPrefix |
| `kWubi` | 五笔模式 | 所有前缀命中一律 `kWeakPrefixBase`（4000），避免用户词干扰五笔码长精确匹配 |

用户词候选通过 `candidate.origin = kUser` 标识，评分远高于系统词典前缀匹配（系统前缀无加分），保证用户选过的词优先于系统未选过的同码词。

### 扫描预算

用户词查询遵守 `QueryBudget::max_user_scan`（默认 256）。`user_scan_count` 只反映命中 bucket 或二分范围内的实际检查次数，不随用户词总量增长。

### 候选偏好应用

启用候选学习后，选中的候选会记录为候选偏好（`origin = kLearned`）。翻译结果合并后，`apply_candidate_preferences()` 按输入码应用偏好：命中项获得 `kPreferenceBaseScore` 级别的超高加分（高于普通用户词），并在分页前完成排序，保证学过的候选稳定置前且不产生重复项。候选偏好独立于用户词库，清空偏好即恢复系统排序。

详见 [用户词库与候选偏好](user-dictionary.md)。

## Candidate 结构

```cpp
// shared/include/cxxime/candidate.h
struct Candidate {
    std::string text;                          // 候选词文本
    std::string comment;                       // 注释（五笔编码、拼音等）
    int frequency = 0;                         // 评分/频率（降序排列依据）
    CandidateSource source = kPinyin;           // 词典来源：kPinyin / kWubi
    std::string code;                          // 码表编码（如 "shurufa"）
    std::string syllables;                     // 音节序列（如 "shu:ru:fa"）
    CandidateOrigin origin = kSystem;           // 候选来源：kSystem / kUser / kLearned / kCache / kComposed
};
```

| 字段 | 说明 |
|------|------|
| `code` | 紧凑码表编码，系统词来自 `syllable_ids → compact_syllable_code()`，用户词存 `user_entry.code` |
| `syllables` | 冒号分隔的音节字符串（如 `"shu:ru:fa"`），系统词从二进制词典字符串表读取，用户词存 `user_entry.syllables` |
| `origin` | `kSystem`（系统词典）、`kUser`（用户词库）、`kLearned`（候选偏好命中）、`kCache`（查询页缓存命中）、`kComposed`（组合候选） |
| `source` | `kPinyin`（拼音模式查询）、`kWubi`（五笔模式查询） |

`origin` 和 `source` 用于日志追踪和调试，不影响排序逻辑（排序仅依赖 `frequency`）。

## 长输入查询页缓存

> 作为短输入快速路径的姊妹机制，用于 >6 字符的长拼音查询。

标准管道在 `PinyinTranslator::translate()` 返回页面前，将结果存入 LRU 缓存：

```cpp
// pinyin_translator.cc — translate() 返回前
if (!deadline_hit && !(trace && trace->deadline_exceeded))
    store_query_cache(pinyin, page_index, page_size, page);
```

| 属性 | 值 |
|------|-----|
| 触发条件 | `input.size() > 6`（短输入走 `lookup_short_fast`，不触发此缓存） |
| 容量 | LRU 64 条（`kMaxQueryCacheEntries = 64`），`sequence` 递增序号实现淘汰 |
| 命中条件 | input + page_index + page_size + `user_dict_version` 全匹配 |
| 失效 | `user_dict_version` 变化（用户词典增删改）后全部缓存自动失效 |
| 非缓存场景 | deadline 命中或 deadline_exceeded 时不写入缓存，避免缓存过期/不完整结果 |

`lookup_query_cache()` 命中时设置 `trace->cache_hit = true` 并清零 trace 计数字段（exact/prefix/user_scan = 0），语义与短输入快速路径的 cache hit 一致。

## 性能特征

| 输入长度 | 路径数 | 延迟 | 说明 |
|---------|--------|------|------|
| 1 字符 | ~10 | < 1ms | 单个音节候选少 |
| 2 字符 | ~200 | < 1ms | 正常输入 |
| 3 字符（缩写） | 封顶 256 | < 1ms | 如 sdf，每位置 10~35 候选，DFS 上限截断 |
| 4 字符（全拼） | 封顶 256 | < 1ms | 如 shui，多数位置只有精确匹配 |
| 11 字符（全拼） | 封顶 256 | < 1ms | 如 nihaoshijie，154 条边密集图，DFS 上限截断 |

## Pipeline 架构

```
Syllabifier(拼写图) → Segmentor(切分) → Translator(查词+排序)
```

- **Syllabifier**：BFS 构建音节图，基于 Patricia trie 前缀搜索，DFS 枚举全路径（信度排序+上限截断）
- **Segmentor**：内置完整拼音音节表，贪婪最长匹配
- **Translator**：基于音节 ID 的词典二分查找 + 用户词索引查询 + 频率排序
- Filter/Formatter 层未实现（目前不需要繁简转换、注释等）
- 候选按 `score_user_match()` 评分降序排列（exact/pattern/prefix分档 + bounded frequency + recent_bonus，分 kPinyin/kWubi 两档）

## 查询预算

查询管道支持 deadline 超时和扫描预算截断，通过 `QueryBudget` 结构传递。详见 [查询预算与候选收集](query-control.md)（包括 deadline 检查点分布、`make_budget()` 分档、TopKCollector 等）。

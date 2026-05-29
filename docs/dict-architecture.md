# 词典三层架构

## 一、当前架构（重构前）

```
┌─────────────────────────────────────────────────────────────┐
│ 当前: 单层 SQLite + C++ 硬编码匹配                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  用户输入 "dd"                                              │
│    ↓                                                        │
│  segment_best("dd") → ["d", "d"]   ← 贪心最长匹配           │
│    ↓                                                        │
│  translate():                                               │
│    code_prefix = "dd" == pinyin "dd" → 走前缀路径            │
│    SELECT ... FROM dict WHERE code LIKE 'dd%'  ← 无结果      │
│    ↓                                                        │
│    fallback → lookup_initials("dd")                          │
│    SELECT ... FROM dict WHERE code LIKE 'd%'   ← 取回 ~1万行 │
│    ↓                                                        │
│    C++ 逐条过滤: vowel→consonant 边界检测                    │
│    硬编码的 "initials 匹配"                                  │
│    ↓                                                        │
│    返回: ["弟弟", "得到", "大地", ...]                        │
│                                                             │
│  问题:                                                      │
│  1. lookup_initials 全表扫描 + C++ 过滤 = 慢                  │
│  2. 缩写规则硬编码在 C++ 里，不可配置                         │
│  3. 不支持模糊拼音 (c↔ch, n↔l 等)                           │
│  4. segment_best 贪心只返回一种分词                           │
└─────────────────────────────────────────────────────────────┘
```

**涉及的代码**:

| 文件 | 当前职责 | 问题 |
|------|---------|------|
| `sqlite_dict.cc:lookup_initials()` | `LIKE 'd%'` + C++ 元音边界过滤 | 全表扫描，硬编码 |
| `sqlite_dict.cc:compute_initials()` | 提取音节首字母 | 与 Spelling Algebra 重复 |
| `sqlite_dict.cc:open()` | PRAGMA 检测 `has_initials_` | 应检测 spellings 表 |
| `dict.h:has_initials_` | initials 列标志 | 应改为 `has_spellings_` |
| `pinyin_translator.cc:translate()` | segment_best → lookup/lookup_initials | 应改为 Syllabifier → lookup_by_syllables |
| `fetch_dict.py:compute_initials()` | 构建时计算 initials | 应改为 Spelling Algebra |
| dict 表 `initials` 列 | 缩写索引 | 应改为 spellings 表 |

---

## 二、三层架构设计目标

### 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│ 第一层: Spelling Algebra (构建时规则引擎)                        │
│                                                                 │
│ 输入: 音节表 {ni, hao, da, di, cha, ca, ...}                    │
│ 规则: abbrev/fuzz/derive/xform/erase/xlit (regex-based)         │
│ 输出: Script = map<输入串, list<Spelling{syllable, type, cred}>> │
│                                                                 │
│ 效果: 音节表被展开为所有可能的输入串→音节映射                     │
├─────────────────────────────────────────────────────────────────┤
│ 第二层: Prism (构建时建索引, 运行时前缀搜索)                     │
│                                                                 │
│ 构建: trie 存 Script 所有 key                                   │
│       SpellingMap[spelling_id] = [{syllable_id, type, cred}...] │
│ 运行: CommonPrefixSearch(input) → spelling_id                   │
│       QuerySpelling(spelling_id) → syllable_id + type + cred    │
│                                                                 │
│ 效果: O(输入长度) 找到所有匹配的音节                             │
├─────────────────────────────────────────────────────────────────┤
│ 第三层: Table (构建时建索引, 运行时按音节序列查询)               │
│                                                                 │
│ 构建: 按 syllable_id 序列索引词条                                │
│ 运行: SyllableGraph indices → Code(syllable_ids) → DictEntry    │
│                                                                 │
│ 效果: O(音节数) 精确查找词条                                     │
├─────────────────────────────────────────────────────────────────┤
│ Syllabifier (运行时, 连接第二层和第三层)                         │
│                                                                 │
│ BFS 走 trie 前缀搜索:                                           │
│   每步 CommonPrefixSearch → SpellingMap → 得到 {syllable, type}  │
│   构建 SyllableGraph (edges: pos→pos → {syllable_id→props})     │
│   Transpose → indices 供 Table 查询                             │
│                                                                 │
│ 效果: 输入串 → 所有可能的音节分词方案                            │
└─────────────────────────────────────────────────────────────────┘
```

### 完整数据流示例

#### 示例1: 输入 "dd"（缩写匹配）

```
第一层 Spelling Algebra (构建时):
  音节表: {da, di, ni, hao, ...}
  规则: abbrev/^(.+).$/$1/  (取首字母)
  Script 产出:
    "d"  → [("da", abbrev, -0.693), ("di", abbrev, -0.693)]
    "da" → [("da", normal, 0)]
    "di" → [("di", normal, 0)]
    "n"  → [("ni", abbrev, -0.693)]
    "ni" → [("ni", normal, 0)]
    ...

第二层 Prism (运行时):
  输入 "dd":
    pos=0: CommonPrefixSearch("dd") → 匹配 "d"
      QuerySpelling("d") → {da:abbrev, di:abbrev}
      edges[0][1] = {da, di}
    pos=1: CommonPrefixSearch("d") → 匹配 "d"
      edges[1][2] = {da, di}

  SyllableGraph:
    edges: {0→1: {da,di}, 1→2: {da,di}}
    Transpose → indices: {0: {da,di}, 1: {da,di}}

第三层 Table (运行时):
  遍历 indices 路径:
    [da,da] → Code → 无结果
    [da,di] → Code → 无结果
    [di,da] → Code → 无结果
    [di,di] → Code → "弟弟" ✓, "笛笛" ✓
```

#### 示例2: 输入 "nihao"（精确匹配）

```
第一层 (构建时):
  Script:
    "ni"  → [("ni", normal, 0)]
    "hao" → [("hao", normal, 0)]
    "n"   → [("ni", abbrev, -0.693)]
    "h"   → [("hao", abbrev, -0.693)]

第二层 (运行时):
  pos=0: CommonPrefixSearch("nihao") → 匹配 "ni"(len=2)
    edges[0][2] = {ni:normal}
  pos=2: CommonPrefixSearch("hao") → 匹配 "hao"(len=3)
    edges[2][5] = {hao:normal}

第三层 (运行时):
  路径 [ni,hao] → Code → "你好" ✓
```

#### 示例3: 输入 "ca"（模糊拼音）

```
第一层 (构建时):
  规则: derive/^([zcs])h/$1/  (zh↔z, ch↔c, sh↔s)
  Script:
    "ca"  → [("ca", normal, 0), ("cha", fuzzy, -0.693)]  ← derive 保留原+加新
    "cha" → [("cha", normal, 0)]

第二层 (运行时):
  pos=0: CommonPrefixSearch("ca") → 匹配 "ca"
    QuerySpelling("ca") → {ca:normal, cha:fuzzy}

第三层 (运行时):
  路径 [ca]  → "擦" ✓
  路径 [cha] → "差" ✓  ← 模糊匹配
```

### 关键类型定义

```cpp
// algo/spelling.h
enum SpellingType {
    kNormalSpelling = 0,
    kFuzzySpelling  = 1,
    kAbbreviation   = 2,
    kCompletion     = 3,
    ...
};
struct SpellingProperties { SpellingType type; double credibility; string tips; };
struct Spelling { string str; SpellingProperties properties; };

// algo/algebra.h
class Script : public map<string, vector<Spelling>> {};

// dict/prism.h
struct SpellingDescriptor { SyllableId syllable_id; int32_t type; Credibility credibility; };
using SpellingMap = Array<List<SpellingDescriptor>>;

// dict/vocabulary.h
using SyllableId = int32_t;
class Code : public vector<SyllableId> {};
struct ShortDictEntry { string text; Code code; double weight; };
```

### 规则类型

```cpp
// algo/calculus.cc
Calculation* Transformation::Parse(args)  // xform/ptn/rep/ → 替换
Calculation* Derivation::Parse(args)      // derive/ptn/rep/ → 保留原+加新
Calculation* Fuzzing::Parse(args)         // fuzz/ptn/rep/  → 同derive + type=fuzzy, penalty=-0.693
Calculation* Abbreviation::Parse(args)    // abbrev/ptn/rep/ → 同derive + type=abbrev, penalty=-0.693
Calculation* Erasion::Parse(args)         // erase/ptn/     → 删除匹配项
Calculation* Transliteration::Parse(args) // xlit/abc/ABC/  → 逐字符映射
```

核心算法 `Projection::Apply(Script*)`:
```cpp
for (auto& rule : calculation_) {
    Script temp;
    for (auto& [key, spellings] : *script) {
        Spelling s(key);
        if (rule->Apply(&s)) {           // regex 替换成功
            if (!rule->deletion())        // derive/fuzz/abbrev: deletion=false
                temp.Merge(key, {}, spellings);  // 保留原 key
            if (rule->addition())         // erase: addition=false
                temp.Merge(s.str, s.properties, spellings);  // 加入变换后 key
        } else {
            temp.Merge(key, {}, spellings);  // 未匹配: 原样保留
        }
    }
    script->swap(temp);
}
```

---

## 三、重构后的三层架构

```
┌─────────────────────────────────────────────────────────────────┐
│ 第一层: Spelling Algebra (Python 构建时规则引擎)                 │
│ 文件: data/tools/spelling_algebra.py (新增)                     │
│       data/schemas/pinyin.schema.yaml (新增)                    │
│                                                                 │
│ 输入: dict.yaml 中的音节表                                      │
│ 规则: schema YAML 中的 speller/algebra 段                       │
│ 输出: Script = map<输入串, list<Spelling{syllable, type, cred}>> │
├─────────────────────────────────────────────────────────────────┤
│ 第二层: Prism (spellings SQLite 表)                             │
│ 文件: data/tools/fetch_dict.py (修改: 构建 spellings 表)        │
│       engine/include/cxxime/spellings_index.h (新增)            │
│       engine/src/spellings_index.cc (新增)                      │
│                                                                 │
│ 构建: Python 将 Script 写入 spellings 表                        │
│ 运行: SpellingsIndex::prefix_search() 查 spellings 表           │
├─────────────────────────────────────────────────────────────────┤
│ 第三层: Table (dict 表 + syllable_ids 列)                       │
│ 文件: data/tools/fetch_dict.py (修改: 加 syllable_ids 列)       │
│       engine/include/cxxime/dict.h (修改)                       │
│       engine/src/dict.cc (修改)                                 │
│                                                                 │
│ 构建: Python 将词条音节序列写入 syllable_ids 列                  │
│ 运行: Dict::lookup_by_syllables() 按 syllable_ids 查询          │
├─────────────────────────────────────────────────────────────────┤
│ Syllabifier (C++ BFS, 连接第二层和第三层)                       │
│ 文件: engine/include/cxxime/syllabifier.h (新增)                │
│       engine/src/syllabifier.cc (新增)                          │
│                                                                 │
│ BFS 走 spellings 表前缀搜索:                                    │
│   每步 prefix_search(input[pos:]) → {syllable, type, cred}      │
│   构建 SyllableGraph edges                                      │
│   枚举所有路径 → 按质量排序                                     │
│   每条路径 → lookup_by_syllables() → 候选                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 四、重构前后对比

### 删除的代码

| 删除项 | 文件 | 原因 |
|--------|------|------|
| `lookup_initials()` | `sqlite_dict.cc`, `dict.h` | 被 Syllabifier + lookup_by_syllables 替代 |
| `compute_initials()` | `sqlite_dict.cc`, `dict.h` | 被 Spelling Algebra 替代 |
| `has_initials_` | `sqlite_dict.cc`, `dict.h` | 被 `has_spellings_` 替代 |
| PRAGMA 检测 initials 列 | `sqlite_dict.cc:open()` | 改为检测 spellings 表 |
| user_dict 的 initials 列/索引 | `sqlite_dict.cc:open()` schema | 不需要 initials |
| `initials` 列/索引 | dict 表 schema | 被 spellings 表替代 |
| `compute_initials()` | `fetch_dict.py`, `dict_convert.py` | 被 Spelling Algebra 替代 |
| 旧 translate() 前缀匹配路径 | `pinyin_translator.cc` | 被 Syllabifier 替代 |

### 新增的代码

| 新增项 | 文件 | 对应层 |
|--------|------|--------|
| `SpellingRule` 类 | `spelling_algebra.py` | 第一层 |
| `Script` 类 | `spelling_algebra.py` | 第一层 |
| `SpellingAlgebra` 类 | `spelling_algebra.py` | 第一层 |
| `pinyin.schema.yaml` | `data/schemas/` | 第一层配置 |
| `spellings` 表 | SQLite | 第二层存储 |
| `SpellingsIndex` 类 | `spellings_index.h/cc` | 第二层查询 |
| `syllable_ids` 列 | dict 表 | 第三层存储 |
| `lookup_by_syllables()` | `sqlite_dict.cc` | 第三层查询 |
| `Syllabifier` 类 | `syllabifier.h/cc` | Syllabifier |

### 查询路径对比

```
重构前:
  "dd" → segment_best → ["d","d"] → translate() → lookup_initials("dd")
       → SELECT ... FROM dict WHERE code LIKE 'd%'  ← 取回 ~1万行
       → C++ 元音边界过滤  ← 逐条检查
       → ["弟弟", "得到", ...]

重构后:
  "dd" → Syllabifier::segment("dd")
       → pos=0: prefix_search("dd") → "d" → {da:abbrev, di:abbrev}
       → pos=1: prefix_search("d")  → "d" → {da:abbrev, di:abbrev}
       → 路径: [di,di], [di,da], [da,di], [da,da]
       → lookup_by_syllables(["di","di"])
       → SELECT ... FROM dict WHERE syllable_ids = 'di:di'  ← 精确查找
       → ["弟弟", ...]
```

### 构建流程对比

```
重构前:
  fetch_dict.py:
    parse_rime_dict() → entries
    compute_initials(code) → initials  ← 硬编码
    INSERT INTO dict (text, code, freq, initials)

重构后:
  fetch_dict.py:
    parse_rime_dict() → entries
    syllabary = set(codes)
    script = Script()
    for s in syllabary: script.add_syllable(s)
    SpellingAlgebra(rules).apply(script)  ← 可配置规则
    for input, spellings in script.items():
      INSERT INTO spellings (input, syllable, type, credibility)
    for text, code, freq in entries:
      syllable_ids = ":".join(syllables)
      INSERT INTO dict (text, code, freq, syllable_ids)
```

---


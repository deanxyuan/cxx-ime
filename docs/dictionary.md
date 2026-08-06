# 词典系统设计

## 1. 架构概览

CxxIME 采用三层架构处理拼音到汉字的转换：

```
用户输入 "nihao"
    │
    ▼
┌─────────────┐   spellings.bin (Patricia trie)
│ SpellingsIndex │ ─── prefix_search("ni") → [{syllable:"ni", type:normal}, ...]
│   (Prism 层)   │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ Syllabifier  │ ─── segment("nihao") → ["ni", "hao"]
│  (分词层)     │     BFS 构建音节图 + DFS 枚举路径
└──────┬──────┘
       │
       ▼
┌─────────────┐   dict.bin (排序数组) + user.tsv (内存 vector+map)
│    Dict      │ ─── lookup("ni:hao") → [你好(100), 你号(50), ...]
│  (Table 层)   │
└─────────────┘
```

### 1.1 三层架构详解

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
│ 构建: Patricia trie 存 Script 所有 key                           │
│       Node.spellings[] = [{syllable, type, cred}...]            │
│ 运行: prefix_search(input) → spellings[{syllable, type, cred}]  │
│                                                                 │
│ 效果: O(输入长度) 找到所有匹配的音节                             │
├─────────────────────────────────────────────────────────────────┤
│ 第三层: Table (构建时建索引, 运行时按音节序列查询)               │
│                                                                 │
│ 构建: 按 syllable_ids 序列索引词条                               │
│ 运行: SyllableGraph indices → Code(syllable_ids) → DictEntry    │
│                                                                 │
│ 效果: O(音节数) 精确查找词条                                     │
├─────────────────────────────────────────────────────────────────┤
│ Syllabifier (运行时, 连接第二层和第三层)                         │
│                                                                 │
│ BFS 走 trie 前缀搜索:                                           │
│   每步 prefix_search(input[pos:]) → {syllable, type, cred}       │
│   构建 SyllableGraph (edges: pos→pos → {syllable_id→props})     │
│   DFS 枚举路径 → lookup_by_syllables() → 候选                   │
└─────────────────────────────────────────────────────────────────┘
```

### 核心类关系

```
Engine
  ├── PinyinProcessor       按键处理（字母、退格、空格、数字选候选）
  ├── PinyinTranslator      翻译器，组合分词 + 词典查询
  │     ├── Dict*            词典指针
  │     ├── Syllabifier*     主分词路径（BFS+DFS 音节图）
  │     └── PinyinSegmentor  简版分词器（Syllabifier 不可用时回退）
  ├── Dict                   主词典（二进制加载）+ 用户词典（内存+TSV，多路索引）
  ├── SpellingsIndex         拼写索引（二进制加载），供 Syllabifier 使用
  ├── Context                输入状态（拼音缓冲、候选列表、已提交文本）
  └── Config                 配置（字体、布局、主题）
```

## 2. 查询流示例

### 2.1 输入 "dd"（缩写匹配）

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
    pos=0: prefix_search("dd") → 匹配 "d"
      QuerySpelling("d") → {da:abbrev, di:abbrev}
      edges[0][1] = {da, di}
    pos=1: prefix_search("d") → 匹配 "d"
      edges[1][2] = {da, di}

  SyllableGraph:
    edges: {0→1: {da,di}, 1→2: {da,di}}
    Transpose → indices: {0: {da,di}, 1: {da,di}}

第三层 Table (运行时):
  遍历 indices 路径:
    [da,da] → lookup_by_syllables → 无结果
    [da,di] → lookup_by_syllables → 无结果
    [di,da] → lookup_by_syllables → 无结果
    [di,di] → lookup_by_syllables → "弟弟" ✓, "笛笛" ✓
```

### 2.2 输入 "ca"（模糊拼音）

```
第一层 (构建时):
  规则: derive/^([zcs])h/$1/  (zh↔z, ch↔c, sh↔s)
  Script:
    "ca"  → [("ca", normal, 0), ("cha", fuzzy, -0.693)]
    "cha" → [("cha", normal, 0)]

第二层 (运行时):
  pos=0: prefix_search("ca") → 匹配 "ca"
    QuerySpelling("ca") → {ca:normal, cha:fuzzy}

第三层 (运行时):
  路径 [ca]  → "擦" ✓
  路径 [cha] → "差" ✓  ← 模糊匹配
```

## 3. 二进制格式规范

### 3.1 spellings.bin — 拼写索引（Prism 层）

将输入字符串映射到音节解释。例如 `"d"` → `["da"(缩写), "di"(缩写), "de"(缩写)]`。

采用 **Patricia trie**（压缩前缀树）实现 O(k) 前缀搜索，k 为前缀长度。

#### 文件布局

```
┌────────────────────────────────────────────┐
│ Header (28 bytes)                          │
│   magic[8]      = "CXSPL\x02\x00\x00"     │
│   version       = 2                        │
│   node_count    = N                        │
│   string_data_size                         │
│   nodes_offset  = 28                       │
│   strings_offset                           │
├────────────────────────────────────────────┤
│ Nodes[node_count]  (变长)                   │
│   每个节点:                                 │
│     key_offset    (uint32) → string_data   │
│     key_len       (uint32)                 │
│     num_spellings (uint8)  = ns            │
│     num_children  (uint8)  = nc            │
│     padding       (uint16)                 │
│     SpellingEntry[ns]  (每条 14 bytes)      │
│     ChildEntry[nc]     (每条 8 bytes)       │
├────────────────────────────────────────────┤
│ String Data (连续 UTF-8 字节块)             │
│   所有 key 和 syllable 字符串紧凑存储        │
└────────────────────────────────────────────┘
```

#### 结构体定义

**SpellingEntry** (14 bytes, `#pragma pack(push, 1)`)

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|------|------|------|------|------|
| 0 | 4 | uint32 | syllable_offset | 音节字符串在 string_data 中的偏移 |
| 4 | 4 | uint32 | syllable_len | 音节字符串长度 |
| 8 | 1 | uint8 | type | 0=正常, 1=模糊, 2=缩写 |
| 9 | 1 | — | padding | |
| 10 | 4 | float | credibility | 可信度评分 |

Python 格式: `"<IIbxf"`

**ChildEntry** (8 bytes)

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|------|------|------|------|------|
| 0 | 1 | uint8 | first_char | 子节点边的首字符 |
| 1 | 3 | — | padding | |
| 4 | 4 | uint32 | node_index | 子节点在节点数组中的索引 |

Python 格式: `"<B3xI"`

**SpellingType 枚举**

| 值 | 名称 | 含义 |
|----|------|------|
| 0 | kNormalSpelling | 标准音节匹配 |
| 1 | kFuzzySpelling | 模糊拼音匹配 |
| 2 | kAbbreviation | 缩写匹配（单字母→完整音节） |

#### Trie 示例

输入 `"da"→"da"`, `"di"→"di"`, `"de"→"de"`, `"d"→"da"(缩写), `"d"→"di"(缩写), `"d"→"de"(缩写)`:

```
root (node 0)
  └─ key="" (root 节点 key 为空)
     spellings: (无)
     children: 'd' → node 1

node 1
  key="d"
  spellings: [da(abbrev), di(abbrev), de(abbrev)]
  children: 'a' → node 2, 'e' → node 3, 'i' → node 4

node 2  key="a"  spellings: [da(normal)]  children: (无)
node 3  key="e"  spellings: [de(normal)]  children: (无)
node 4  key="i"  spellings: [di(normal)]  children: (无)
```

搜索 `"dd"` 时：走到 node 1 (key="d")，收集缩写拼写，然后找 'd' 子节点 → 未找到 → 返回缩写结果。

#### 搜索算法 (trie_prefix_search)

```cpp
prefix_pos = 0, current_node = 0
while current_node < node_count:
    node = nodes + node_offsets[current_node]
    key = strings + node.key_offset
    if prefix_pos + key_len > prefix_len: break
    if memcmp(key, prefix + prefix_pos, key_len) != 0: break
    prefix_pos += key_len
    collect all SpellingEntry from node
    if prefix_pos >= prefix_len: break
    find child with first_char == prefix[prefix_pos]
    if not found: break
    current_node = child.node_index
```

复杂度: O(k)，k = 前缀长度。

### 3.2 dict.bin — 主词典（Table 层）

存储拼音到汉字的映射，按 `syllable_ids` 排序，支持 O(log n) 二分查找。

#### 文件布局

```
┌────────────────────────────────────────────┐
│ Header (28 bytes)                          │
│   magic[8]      = "CXDIC\x02\x00\x00"     │
│   version       = 2                        │
│   entry_count   = M                        │
│   string_data_size                         │
│   entries_offset = 28                      │
│   strings_offset                           │
├────────────────────────────────────────────┤
│ DictEntry[entry_count]  (每条 20 bytes)     │
│   按 syllable_ids 字节序升序排列             │
├────────────────────────────────────────────┤
│ String Data (连续 UTF-8 字节块)             │
└────────────────────────────────────────────┘
```

#### DictEntry 结构体 (20 bytes, `#pragma pack(push, 1)`)

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|------|------|------|------|------|
| 0 | 4 | uint32 | syllable_ids_offset | 拼音键偏移，如 "ni:hao" |
| 4 | 4 | uint32 | text_offset | 汉字文本偏移，如 "你好" |
| 8 | 4 | uint32 | syllable_ids_len | 拼音键长度 |
| 12 | 4 | uint32 | text_len | 汉字文本长度 |
| 16 | 4 | int32 | frequency | 词频 |

Python 格式: `"<IIIIi"`

`static_assert(sizeof(DictEntry) == 20)`

#### 搜索算法 (lookup_by_syllables)

1. 将音节列表拼接为冒号分隔键: `["ni","hao"]` → `"ni:hao"`
2. 二分查找 `syllable_ids` 匹配的条目（memcmp 比较）
3. 从匹配位置向前扫描，收集所有相同 `syllable_ids` 的条目
4. 查询内存 user_entries_（同一 code）
5. 合并结果，按 frequency 降序排列

#### 搜索算法 (lookup — 前缀匹配)

与 lookup_by_syllables 类似，但匹配 `syllable_ids` 以给定前缀开头的条目。

#### 反查算法 (reverse_lookup)

线性扫描所有条目，比较 `text` 字段。复杂度 O(n)，因为 dict.bin 按 syllable_ids 排序而非 text。

### 3.3 五笔完整前缀索引（wubi86.dict.idx / CXWIDX v1）

本次更新为五笔引入独立的完整前缀索引，取代原来的整数 ID 索引（CXIDX）。`wubi86.dict.idx` 在构建时由 `data/tools/dict_builder/wubi_prefix_index.py` 生成，记录每个可达五笔编码前缀的排序词条列表；运行时由 `WubiPrefixIndex` 整文件加载，`Dict::lookup()` 二分查找后直接取预排序 postings，避免全表扫描。

#### 文件布局

```
┌────────────────────────────────────────────┐
│ Header (48 bytes)                          │
│   magic[8]      = "CXWIDX\x01\x00"         │
│   version       = 1                        │
│   header_size / file_size                  │
│   dict_entry_count / key_count             │
│   posting_count                            │
│   keys_offset / postings_offset            │
│   max_code_length = 4 / reserved = 0       │
├────────────────────────────────────────────┤
│ Key[ key_count ]  (每条 12 bytes)           │
│   packed_code / posting_offset             │
│   posting_count                            │
├────────────────────────────────────────────┤
│ Postings[ posting_count ]  (uint32 数组)    │
│   dict.bin 词条索引，按排名预排序           │
└────────────────────────────────────────────┘
```

- `packed_code`：五笔码按 5 bit/字符 压缩（a=1..z=26），最多 4 码
- key 按 `packed_code` 升序，`posting_offset` 连续覆盖 postings 区
- postings 排名：精确匹配 → 码长升序 → 词频降序 → 码序 → 文本长度 → 文本字典序，并按文本去重
- 构建入口：`build_runtime_dictionary.py --wubi-prefix-index`

#### 运行期查询

`Dict::open_wubi_bundle()` 加载 `wubi86.dict.bin` + `wubi86.dict.idx`；`Dict::lookup()` 优先走 `WubiPrefixIndex::find()`（对 `packed_code` 二分查找），直接返回预排序 postings；无索引时回退到旧的二分 + 扫描路径。

### 3.4 格式版本兼容

通过文件头 magic 字节自动检测版本：

| 格式 | magic (hex/ASCII) | 说明 |
|------|-------------------|------|
| spellings v1 | `43 58 53 50 4C 01 00 00` / `CXSPL\x01\x00\x00` | 平坦排序数组（旧格式，向后兼容） |
| spellings v2 | `43 58 53 50 4C 02 00 00` / `CXSPL\x02\x00\x00` | Patricia trie（当前生产格式） |
| dict v1 | `43 58 44 49 43 01 00 00` / `CXDIC\x01\x00\x00` | 平坦排序数组 |
| dict v2 | `43 58 44 49 43 02 00 00` / `CXDIC\x02\x00\x00` | 平坦排序数组（布局相同，版本号升级） |
| dict.idx v2/v3 | `43 58 49 44 58 00 00 00 00` / `CXIDX\0\0\0\0` | 整数 ID 索引（音节→词条，v2 变长解析，v3 zero-copy） |
| wubi idx v1 | `43 58 57 49 44 58 01 00` / `CXWIDX\x01\x00` | 五笔完整前缀索引（packed code → 排序 postings） |
| topn.bin v2 | `43 58 54 4F 50 4E 02 00` / `CXTOPN\x02\0` | DAT-16 格式：Darts-clone 双数组 Trie 键索引 + 内联 16 字节候选条目 |

## 4. 数据存储方案

### 4.1 存储层选择

| 数据 | 格式 | 原因 |
|------|------|------|
| 主词典 (dict.bin) | 堆内存二进制加载 | 只读，一次性读入，O(log n) 二分查找 |
| 拼写索引 (spellings.bin) | 堆内存二进制加载 | 只读，一次性读入，O(k) trie 遍历 |
| 用户词典 (user.tsv) | 内存 vector + map | 运行时 UPSERT，TSV 持久化 |

### 4.2 文件大小对比

| 文件 | SQLite (.db) | 二进制 (.bin / .idx) | 压缩 (.zip) |
|------|-------------|----------------------|-------------|
| pinyin 主词典 | 146 MB | 72.8 MB (dict.bin) | 57 MB |
| pinyin 整数 ID 索引 | — | 48.4 MB (dict.idx) | — |
| pinyin Top-N 候选索引 | — | 212 MB (topn.bin, DAT-16) | — |
| pinyin 拼写索引 | — | 2.9 MB (spellings.bin) | — |
| wubi86 主词典 | 3.2 MB | 2.6 MB (dict.bin) | 1.7 MB |
| wubi86 完整前缀索引 | — | 2.3 MB (dict.idx) | — |

### 4.3 为什么不用 SQLite 存主词典

- SQLite 需要编译 120MB 的 sqlite3.c
- SQL `LIKE` 查询 + C++ 过滤比纯内存数据结构慢一个数量级
- 页开销 + 索引冗余导致文件体积膨胀
- 主词典是只读数据，不需要事务/写入能力

### 4.4 为什么不用 DARTS+MARISA

- DARTS (Double-Array Trie) 和 MARISA Trie 需要引入外部依赖
- Patricia trie 在零依赖条件下实现 O(k) 搜索，性能相当
- 字符串去重对短字符串（拼音键平均 4-6 字节）不划算：2.69M × 8 字节引用开销 > 去重节省

## 5. 构建流程

### 5.1 数据源

原始词典存储在 SQLite `.dict.db` 文件中（以 `.zip` 压缩提交到 git）：

```sql
-- spellings 表（拼写规则）
CREATE TABLE spellings (
    input TEXT,        -- 输入键，如 "ni", "n", "ne"
    syllable TEXT,     -- 音节，如 "ni", "ne"
    type INTEGER,      -- 0=正常, 1=模糊, 2=缩写
    credibility REAL   -- 可信度
);

-- dict 表（词典条目）
CREATE TABLE dict (
    text TEXT,          -- 汉字，如 "你好"
    code TEXT,          -- 拼音编码，如 "ni hao"
    frequency INTEGER,  -- 词频
    syllable_ids TEXT   -- 音节键，如 "ni:hao"
);
```

### 5.2 构建工具

`data/tools/build_runtime_dictionary.py`（`dict_builder` 包）— 将 SQLite 转换为二进制格式：

```bash
# 从 .dict.db 构建（直接路径）
python data/tools/build_runtime_dictionary.py -i data/pinyin.dict.db -o data/pinyin

# 从 .zip 构建（自动解压到临时目录）
python data/tools/build_runtime_dictionary.py -i data/pinyin.dict.db.zip -o data/pinyin

# 仅构建 spellings 或 dict
python data/tools/build_runtime_dictionary.py -i data/pinyin.dict.db -o data/pinyin --spellings-only
python data/tools/build_runtime_dictionary.py -i data/pinyin.dict.db -o data/pinyin --dict-only
```

输出文件：
- `data/pinyin.spellings.bin` — Patricia trie 拼写索引
- `data/pinyin.dict.bin` — 排序数组主词典

五笔：先拆分符号扩展项，再生成 `dict.bin` + 完整前缀索引：

```bash
python data/tools/split_wubi_symbols.py --input data/wubi86.dict.db \
    --symbols-output data/symbols.json --filtered-output <filtered-wubi.dict.db>
python data/tools/build_runtime_dictionary.py -i <filtered-wubi.dict.db> -o data/wubi86 \
    --dict-only --wubi-prefix-index
```

### 5.3 构建管线

```
fetch_pinyin_dictionary.py / fetch_wubi_dictionary.py    从网络获取词典数据
        │
        ▼
   pinyin.dict.db                 SQLite 源文件（git 中以 .zip 存储）
        │
        ▼
  build_runtime_dictionary.py     Python 转换工具（dict_builder 包）
   ├── pinyin_spellings.py        SQLite → Patricia trie → spellings.bin
   ├── runtime_dictionary.py      SQLite → 排序数组 → dict.bin
   └── pinyin_syllable_index.py   SQLite → 整数 ID 索引 → dict.idx
        │
        ▼
   pinyin.spellings.bin           运行时内存加载
   pinyin.dict.bin                 运行时内存加载
   pinyin.dict.idx                 整数 ID 索引

  build_pinyin_topn.py            SQLite → Top-N 候选键与评分
        │
        ▼
  topn_builder --format dat16     中间文件 → DAT-16 索引
        │
        ▼
   pinyin.topn.bin                运行时内存加载（Darts trie + 内联候选）

  fetch_wubi_dictionary.py / split_wubi_symbols.py   五笔源数据 → symbols.json + 过滤后词典
        │
        ▼
  build_runtime_dictionary.py --dict-only --wubi-prefix-index
        │
        ▼
   wubi86.dict.bin + wubi86.dict.idx（完整前缀索引）
```

### 5.4 Patricia Trie 构建过程

1. 从 SQLite 读取所有 `(input, syllable, type, credibility)` 条目
2. 逐条插入 Patricia trie：
   - 遍历 key 字符，查找匹配的子节点
   - 完全匹配：递归进入子节点
   - 部分匹配：分裂节点（创建公共前缀中间节点）
   - 无匹配：创建新叶节点
3. BFS 遍历分配节点索引
4. 序列化：header + 节点数据 + 字符串数据

### 5.5 SQLite → 二进制的数据流

```
SQLite spellings 表          Patricia Trie              spellings.bin
┌─────────────────┐         ┌─────────┐               ┌──────────────┐
│ input│syll│type │  ──→    │  root   │  ──serialize→  │ Header       │
│ "d"  │"da"│  2  │         │   │     │               │ Nodes[]      │
│ "da" │"da"│  0  │         │   d     │               │ String Data  │
│ ...  │    │     │         │  /│\    │               └──────────────┘
└─────────────────┘         a  e  i
```

## 6. 用户词典 (user_dict)

采用多路内存索引 + TSV 持久化。每条词条存储 `text`、`code`、`syllables`（冒号分隔音节键）、`abbr_code`、`mixed_keys`，支持 `frequency`、`sequence`（版本计数）、`deleted`（软删除）。

索引分四路：

| 索引 | 用途 |
|------|------|
| `user_exact_index_` | 完整 code 精确匹配 |
| `user_prefix_index_` | 短前缀匹配（长度 1..6） |
| `user_abbr_index_` | 缩写匹配（首字母组合） |
| `user_mixed_index_` | 混合匹配（声母增强 / 首音节展开 / 前两音节展开 / 长词首字母码） |

用户词评分分双档：`kPinyin` 和 `kWubi`，通过 `set_user_scoring_profile()` 设置。

详见 [用户词典设计](user-dictionary.md)。

## 7. 二进制加载

### 7.1 加载流程

```cpp
HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, ...);
GetFileSizeEx(hFile, &li);
data_ = new (std::nothrow) char[file_size];
ReadFile(hFile, data_, file_size, &bytes_read, nullptr);
CloseHandle(hFile);
// 验证 magic → 设置指针 → 构建索引
```

### 7.2 资源管理

```cpp
~SpellingsIndex() {
    delete[] data_;   // 释放堆内存
}
```

### 7.3 优势

- 一次性读入：避免 mmap 的 page-out 延迟（防病毒软件/文件系统过滤驱动干扰）
- 无解析开销：二进制布局与内存布局一致，指针直接偏移
- 确定性：内存占用等于文件大小，无按需加载的不确定性

## 8. 当前构建状态

### 8.1 已编译的文件 (engine/CMakeLists.txt)

```
src/dict.cc               ← 二进制加载主词典 + 内存用户词典（多路索引）
src/spellings_index.cc    ← Patricia trie 拼写索引
src/syllabifier.cc        ← 音节分词器（BFS+DFS）
src/pinyin_translator.cc  ← 拼音翻译（主路径，含长查询页缓存）
src/pinyin_segmentor.cc   ← 简版拼音分词器（Syllabifier 回退）
src/pinyin_processor.cc   ← 拼音按键处理
src/wubi_translator.cc    ← 五笔翻译
src/wubi_processor.cc     ← 五笔按键处理
src/mixed_translator.cc   ← 混合模式翻译（拼音+五笔交叉排序）
src/output_composer.cc    ← 输出合成（全角/CapsLock/按键拦截）
src/ascii_composer.cc     ← 中英文切换
src/short_code_cache.cc   ← Top-N 候选缓存（pinyin.topn.bin，DAT-16 格式，Darts trie 查找）
src/engine.cc             ← 引擎入口
src/context.cc            ← 输入状态上下文
src/config.cc             ← JSON 配置加载
```

## 9. 测试

### 9.1 测试框架

自定义轻量级测试框架 (`test/util/testutil.h`)，无外部依赖：

```cpp
TEST(SuiteName, TestName) { ... }          // 自注册测试用例
ASSERT_TRUE(cond) / ASSERT_EQ(a, b) / ...  // 断言宏（fatal）
RUN_ALL_TESTS()                            // main 入口，自动发现并运行
```

### 9.2 测试覆盖

共 35 个 C++ 测试可执行文件 + 4 个 Python 测试（39 个 ctest 条目），合计 500+ 个 `TEST()` 用例：

| 测试文件 | 用例数 | 测试内容 |
|----------|--------|----------|
| `engine_test` | 66 | 按键处理、集成翻译、多种输入模式 |
| `engine_source_test` | 22 | 引擎源码级测试 |
| `segmentor_test` | 5 | 标准拼音切分 |
| `dict_test` | 26 | 词典打开/前缀查找/音节查找/空查询/反查/用户词频更新 |
| `config_test` | 12 | 默认值/JSON加载/缺失文件/无效JSON |
| `layout_test` | 6 | 文本宽度估算/水平布局/换行/垂直布局 |
| `preedit_mode_test` | 5 | 合成模式/预览模式/内联 |
| `ipc_test` | 29 | 协议结构体/服务器启停/会话管理/多客户端 |
| `wubi_test` | 7 | Wubi86 基本查找/前缀匹配/去重 |
| `wubi_engine_test` | 22 | 五笔引擎集成测试 |
| `candidate_window_test` | 10 | 候选窗口渲染与交互 |
| `candidate_quality_test` | 1 | 候选质量排序 |
| `status_window_test` | 18 | 状态窗口渲染 |
| `benchmark_test` | 16 | 性能基准测试 |
| `short_cache_test` | 9 | 短码缓存查询 |
| `trace_test` | 21 | 查询链路追踪 |
| `mpscq_test` | 3 | 多生产者单消费者队列 |
| `config_monitor_test` | 10 | 配置变更监控 |
| `dictionary_monitor_test` | 3 | 词典 manifest 监控 |
| `output_composer_test` | 44 | 输出合成（全角/CapsLock/按键拦截） |
| `session_manager_status_test` | 18 | 会话管理器状态 |
| `session_manager_integration_test` | 27 | 会话管理器集成 |
| `wubi_prefix_query_test` | 1 | 五笔完整前缀索引查询排序 |
| `wubi_prefix_index_test` | (Python) | 五笔完整前缀索引构建验证 |
| `pinyin_topn_pipeline_test` | (Python) | Top-N 键生成与 DAT-16 转换验证 |
| **合计** | **500+** | |

### 9.3 运行测试

```bash
cd build
ctest -C Debug                          # 运行全部测试（39 个）
build\test\Debug\engine_test.exe        # 单独运行某个测试
```

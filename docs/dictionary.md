# CxxIME 词典系统设计文档

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
┌─────────────┐   dict.bin (排序数组) + user.dict.db (SQLite)
│    Dict      │ ─── lookup("ni:hao") → [你好(100), 你号(50), ...]
│  (Table 层)   │
└─────────────┘
```

### 核心类关系

```
Engine
  ├── PinyinProcessor       按键处理（字母、退格、空格、数字选候选）
  ├── PinyinTranslator      翻译器，组合分词 + 词典查询
  │     ├── Dict*            词典指针
  │     └── PinyinSegmentor  分词器（当前使用简版，非 Syllabifier）
  ├── Dict                   主词典（mmap）+ 用户词典（SQLite）
  ├── SpellingsIndex         拼写索引（mmap），供 Syllabifier 使用
  ├── Context                输入状态（拼音缓冲、候选列表、已提交文本）
  └── Config                 配置（字体、布局、主题）
```

## 2. 二进制格式规范

### 2.1 spellings.bin — 拼写索引（Prism 层）

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

### 2.2 dict.bin — 主词典（Table 层）

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

`sstatic_assert(sizeof(DictEntry) == 20)`

#### 搜索算法 (lookup_by_syllables)

1. 将音节列表拼接为冒号分隔键: `["ni","hao"]` → `"ni:hao"`
2. 二分查找 `syllable_ids` 匹配的条目（memcmp 比较）
3. 从匹配位置向前扫描，收集所有相同 `syllable_ids` 的条目
4. 查询 user_dict SQLite 表（同一 code）
5. 合并结果，按 frequency 降序排列

#### 搜索算法 (lookup — 前缀匹配)

与 lookup_by_syllables 类似，但匹配 `syllable_ids` 以给定前缀开头的条目。

#### 反查算法 (reverse_lookup)

线性扫描所有条目，比较 `text` 字段。复杂度 O(n)，因为 dict.bin 按 syllable_ids 排序而非 text。

### 2.3 格式版本兼容

通过文件头 magic 字节自动检测版本：

| 格式 | magic (hex) | 说明 |
|------|-------------|------|
| spellings v1 | `43 58 53 50 4C 01 00 00` | 平坦排序数组（旧格式，向后兼容） |
| spellings v2 | `43 58 53 50 4C 02 00 00` | Patricia trie（当前生产格式） |
| dict v1 | `43 58 44 49 43 01 00 00` | 平坦排序数组 |
| dict v2 | `43 58 44 49 43 02 00 00` | 平坦排序数组（布局相同，版本号升级） |

## 3. 数据存储方案

### 3.1 存储层选择

| 数据 | 格式 | 原因 |
|------|------|------|
| 主词典 (dict.bin) | mmap 二进制文件 | 只读，零拷贝加载，O(log n) 二分查找 |
| 拼写索引 (spellings.bin) | mmap 二进制文件 | 只读，O(k) trie 遍历 |
| 用户词典 (user.dict.db) | SQLite | 需要运行时 UPSERT 写入 |

### 3.2 文件大小对比

| 文件 | SQLite (.db) | 二进制 (.bin) | 压缩 (.zip) |
|------|-------------|---------------|-------------|
| pinyin 词典 | 146 MB | 69 MB (dict.bin) | 57 MB |
| pinyin 拼写 | — | 2.9 MB (spellings.bin) | — |
| wubi86 词典 | 3.2 MB | — | 1.7 MB |

### 3.3 为什么不用 SQLite 存主词典

- SQLite 需要编译 120MB 的 sqlite3.c
- SQL `LIKE` 查询 + C++ 过滤比纯内存数据结构慢一个数量级
- 页开销 + 索引冗余导致文件体积膨胀
- 主词典是只读数据，不需要事务/写入能力

### 3.4 为什么不用 librime 的 DARTS+MARISA

- DARTS (Double-Array Trie) 和 MARISA Trie 需要引入外部依赖
- Patricia trie 在零依赖条件下实现 O(k) 搜索，性能相当
- 字符串去重对短字符串（拼音键平均 4-6 字节）不划算：2.69M × 8 字节引用开销 > 去重节省

## 4. 构建流程

### 4.1 数据源

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

### 4.2 构建工具

`data/tools/build_binary.py` — 将 SQLite 转换为二进制格式：

```bash
# 从 .dict.db 构建（直接路径）
python data/tools/build_binary.py -i data/pinyin.dict.db -o data/pinyin

# 从 .zip 构建（自动解压到临时目录）
python data/tools/build_binary.py -i data/pinyin.dict.db.zip -o data/pinyin

# 仅构建 spellings 或 dict
python data/tools/build_binary.py -i data/pinyin.dict.db -o data/pinyin --spellings-only
python data/tools/build_binary.py -i data/pinyin.dict.db -o data/pinyin --dict-only
```

输出文件：
- `data/pinyin.spellings.bin` — Patricia trie 拼写索引
- `data/pinyin.dict.bin` — 排序数组主词典

### 4.3 构建管线

```
fetch_dict.py / fetch_wubi.py    从网络获取词典数据
        │
        ▼
   pinyin.dict.db                 SQLite 源文件（git 中以 .zip 存储）
        │
        ▼
  build_binary.py                 Python 转换工具
   ├── build_spellings_trie()     SQLite → Patricia trie → spellings.bin
   └── build_dict_bin()           SQLite → 排序数组 → dict.bin
        │
        ▼
   pinyin.spellings.bin           运行时 mmap 加载
   pinyin.dict.bin                 运行时 mmap 加载
```

### 4.4 Patricia Trie 构建过程

1. 从 SQLite 读取所有 `(input, syllable, type, credibility)` 条目
2. 逐条插入 Patricia trie：
   - 遍历 key 字符，查找匹配的子节点
   - 完全匹配：递归进入子节点
   - 部分匹配：分裂节点（创建公共前缀中间节点）
   - 无匹配：创建新叶节点
3. BFS 遍历分配节点索引
4. 序列化：header + 节点数据 + 字符串数据

### 4.5 SQLite → 二进制的数据流

```
SQLite spellings 表          Patricia Trie              spellings.bin
┌─────────────────┐         ┌─────────┐               ┌──────────────┐
│ input│syll│type │  ──→    │  root   │  ──serialize→  │ Header       │
│ "d"  │"da"│  2  │         │   │     │               │ Nodes[]      │
│ "da" │"da"│  0  │         │   d     │               │ String Data  │
│ ...  │    │     │         │  /│\    │               └──────────────┘
└─────────────────┘         a  e  i
```

## 5. 用户词典 (user_dict)

### 5.1 SQLite 表结构

```sql
CREATE TABLE user_dict (
    id INTEGER PRIMARY KEY,
    text TEXT NOT NULL,
    code TEXT NOT NULL,
    frequency INTEGER DEFAULT 1,
    last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(text, code)
);
CREATE INDEX idx_user_code ON user_dict(code);
```

### 5.2 词频更新

每次用户选择候选词时，通过 UPSERT 更新频率：

```sql
INSERT INTO user_dict (text, code, frequency) VALUES (?1, ?2, 1)
ON CONFLICT(text, code) DO UPDATE
SET frequency = frequency + 1, last_used = CURRENT_TIMESTAMP
```

### 5.3 存储位置

- 默认路径: `%LOCALAPPDATA%\CxxIME\user.dict.db`
- 测试模式: `:memory:` 内存数据库

## 6. 内存映射 (mmap) 加载

### 6.1 加载流程

```cpp
HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, ...);
HANDLE hMap  = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
void* base   = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
// 验证 magic → 设置指针 → 构建索引
```

### 6.2 资源管理

```cpp
~SpellingsIndex() {
    UnmapViewOfFile(data_);      // 解除映射
    CloseHandle(mapping_handle_); // 关闭文件映射
    CloseHandle(file_handle_);    // 关闭文件句柄
}
```

### 6.3 优势

- 零拷贝：操作系统直接将文件映射到进程地址空间
- 按需加载：OS 按页调度，只加载实际访问的部分
- 无解析开销：二进制布局与内存布局一致，指针直接偏移

## 7. 当前构建状态

### 7.1 已编译的文件 (engine/CMakeLists.txt)

```
src/dict.cc               ← 二进制 mmap + SQLite user_dict 混合实现
src/spellings_index.cc    ← Patricia trie 拼写索引
src/syllabifier.cc        ← 音节分词器
src/pinyin_translator.cc
src/pinyin_segmentor.cc
src/pinyin_processor.cc
src/engine.cc
src/context.cc
src/config.cc
```

## 8. 测试

### 8.1 测试框架

自定义轻量级测试宏，无外部依赖：

```cpp
#define TEST(name) static void name()
#define ASSERT(cond) do { if (!(cond)) { ...; return; } } while(0)
#define RUN_TEST(name) do { name(); tests_passed++; } while(0)
```

### 8.2 测试覆盖

| 测试组 | 用例数 | 测试内容 |
|--------|--------|----------|
| Protocol | 4 | IPC 协议结构体大小、字段对齐 |
| Server | 3 | 服务启停、管道创建 |
| Client | 3 | 连接、断开、重连 |
| IPC | 8 | 命令收发、会话管理 |
| MultiClient | 2 | 多客户端并发 |
| Reconnect | 1 | 服务重启后重连 |
| Engine | 9 | 按键处理、候选提交 |
| Segmentor | 5 | 拼音分词 |
| Dict | 6 | 词典查询、用户词典 |
| Config | 6 | 配置加载 |
| **合计** | **48** | |

### 8.3 运行测试

```bash
cmake --build build --config Debug
./build/test/Debug/cxxime-test.exe
```

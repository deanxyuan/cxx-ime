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

- **枚举上限**：最多枚举 256 条路径（`kMaxPaths = 256`）。超过则 DFS 提前退出。translator 只取前 8 条路径，256 条已留出 32 倍余量。密集缩写图（如 11 字符全拼产生 154 条边）在无上限时可生成 10,000+ 条路径，降至 256 后同一输入 <1ms 完成。

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

同时查询用户词典索引（`lookup_user_exact` / `lookup_user_prefix`），补充个性化候选。用户词候选按 `user_boost + frequency + recent_bonus` 评分，插入到结果中。

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
| `lookup_user_prefix` | `lookup()` 前缀匹配 | `user_prefix_index_` 或 `user_code_sorted_` 二分 |
| `lookup_user_short` | 短输入快速路径 | exact → prefix → abbr → mixed |

### 用户词评分

```
score = user_boost + frequency + recent_bonus

user_boost   = 50000（固定加分，低于系统词精确匹配的 100000）
recent_bonus = min(1000, max(0, 1000 - (user_sequence_ - entry.sequence)))
```

用户词候选低于系统词精确匹配但高于系统词前缀/缩写匹配，保证用户选过的词优先于未选过的同码词，但不会覆盖系统词的精确匹配。

### 扫描预算

用户词查询遵守 `QueryBudget::max_user_scan`（默认 256）。`user_scan_count` 只反映命中 bucket 或二分范围内的实际检查次数，不随用户词总量增长。

详见 [用户词典设计](user-dictionary.md)。

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
- 候选按主词典频率降序排列，用户词典匹配项额外加分（`user_boost=50000` + `frequency` + `recent_bonus`）

## 查询预算（Deadline & Scan Budget）

查询管道在多个检查点支持 deadline 超时和扫描条目数限制，通过 `QueryBudget` 结构传递：

- **Syllabifier 入口**：`deadline_us < 10ms` 时跳过（Syllabifier 内部不检查 deadline）
- **has_prefix 前**：每条路径检查 deadline
- **lookup_by_ids 前**：每条路径检查 deadline
- **lookup_by_ids 内部**：每 64 条检查 deadline + 扫描上限（`max_exact_scan` / `max_prefix_scan`）
- **用户词索引扫描**：每 64 条检查 deadline + 扫描上限（`max_user_scan`，默认 256）

任一触发都会设置 `QueryTrace::deadline_exceeded` 和 `truncated`。详见 [查询预算与候选收集](query-control.md)。

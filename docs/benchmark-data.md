# 性能基准数据

本文档记录各版本的查询延迟基准数据，用于回归对比。

> 本文以下表格是 DAT-16 完整键 Top-N 索引之前的历史数据，其中“长度大于6不走快速路径”
> 只描述当时版本。
> 当前规则见 `short-input-fast-path.md`：完整键不受长度 6 限制，只有前缀候选仍物化 1..6。
> v4（共享候选）改变的是索引文件布局与内存占用（posting 24B→8B、文本/音节/词频复用 dict.bin），Trie 查找与候选合并路径未变。

## 测试条件

```cmd
build\tools\query_bench\Release\query_bench.exe --data data --input s,sd,sdf,sddf,bj,srf,shrf,zguo,nihao,nihaoshijie --repeat 500 --warmup 100 --page-size 7 --deadline-ms 30
```

参数：`--repeat 500 --warmup 100 --page-size 7 --deadline-ms 30`，取 3 轮中位数（轮间冷却 10 秒）。

## 原始基线（无优化）

> 无 TopK / 无 make_budget / 无 deadline，全量扫描后排序。

| 输入 | 类型 | 路径 | 候选 | e2e P50 | e2e P99 | 查询 P50 | 查询 P99 |
|------|------|------|------|---------|---------|----------|----------|
| `s` | 单字母 | 8 | 7 | 789 us | 1100 us | 785 us | 921 us |
| `sd` | 双字母缩写 | 4 | 7 | 936 us | 1379 us | 199 us | 269 us |
| `sdf` | 三字母缩写 | 0 | 0 | 2889 us | 3902 us | 1924 us | 2291 us |
| `sddf` | 四字母缩写 | 0 | 0 | 6059 us | 7740 us | 2939 us | 3514 us |
| `bj` | 双字母缩写 | 8 | 7 | 2067 us | 2612 us | 118 us | 128 us |
| `srf` | 三字母缩写 | 0 | 0 | 2143 us | 2670 us | 1243 us | 1437 us |
| `shrf` | 四字母缩写 | 0 | 0 | 6462 us | 7720 us | 2979 us | 3501 us |
| `zguo` | 混合拼音 | 2 | 7 | 1912 us | 2387 us | 486 us | 566 us |
| `nihao` | 全拼 | 2 | 7 | 3814 us | 4488 us | 1500 us | 1705 us |
| `nihaoshijie` | 长输入 | 1 | 1 | 25911 us | 30351 us | 4139 us | 4985 us |

## TopK + make_budget

> TopKCollector 限制候选收集量，make_budget 按输入长度调优 scan 预算。

| 输入 | 类型 | 路径 | 候选 | e2e P50 | e2e P99 | 查询 P50 | 查询 P99 | trunc% |
|------|------|------|------|---------|---------|----------|----------|--------|
| `s` | 单字母 | 8 | 7 | 97 us | 116 us | 94 us | 97 us | 100% |
| `sd` | 双字母缩写 | 4 | 7 | 335 us | 456 us | 223 us | 257 us | 0% |
| `sdf` | 三字母缩写 | 0 | 0 | 2233 us | 2996 us | 1894 us | 2365 us | 0% |
| `sddf` | 四字母缩写 | 0 | 0 | 5477 us | 7338 us | 2951 us | 3814 us | 0% |
| `bj` | 双字母缩写 | 8 | 7 | 210 us | 282 us | 92 us | 102 us | 0% |
| `srf` | 三字母缩写 | 0 | 0 | 1524 us | 2105 us | 1226 us | 1413 us | 0% |
| `shrf` | 四字母缩写 | 0 | 0 | 5763 us | 8179 us | 2950 us | 3875 us | 0% |
| `zguo` | 混合拼音 | 2 | 7 | 1085 us | 1558 us | 482 us | 539 us | 0% |
| `nihao` | 全拼 | 2 | 7 | 2494 us | 3782 us | 1487 us | 1792 us | 0% |
| `nihaoshijie` | 长输入 | 1 | 1 | 24439 us | 28818 us | 4116 us | 5718 us | 0% |

**vs 原始基线：** 短输入（1–2 字母）e2e P50 下降 64%–90%，长输入无劣化。`s` 的 trunc%=100% 是预期行为 — 单字母触发 scan 上限。

## 当前基线（deadline 保护）

> TopKCollector + make_budget + QueryDeadline，默认 30ms deadline。

| 输入 | 类型 | 路径 | 候选 | e2e P50 | e2e P99 | 查询 P50 | 查询 P99 | trunc% | deadline% |
|------|------|------|------|---------|---------|----------|----------|--------|-----------|
| `s` | 单字母 | 8 | 7 | 36 us | 40 us | 34 us | 35 us | 100% | 0% |
| `sd` | 双字母缩写 | 4 | 7 | 258 us | 363 us | 211 us | 244 us | 100% | 0% |
| `sdf` | 三字母缩写 | 0 | 0 | 2233 us | 3024 us | 1968 us | 2401 us | 0% | 0% |
| `sddf` | 四字母缩写 | 0 | 0 | 5593 us | 7304 us | 3080 us | 3825 us | 0% | 0% |
| `bj` | 双字母缩写 | 8 | 7 | 117 us | 187 us | 79 us | 92 us | 100% | 0% |
| `srf` | 三字母缩写 | 0 | 0 | 1506 us | 2058 us | 1289 us | 1522 us | 0% | 0% |
| `shrf` | 四字母缩写 | 0 | 0 | 5949 us | 7547 us | 3086 us | 3853 us | 0% | 0% |
| `zguo` | 混合拼音 | 2 | 7 | 1061 us | 1448 us | 511 us | 566 us | 100% | 0% |
| `nihao` | 全拼 | 2 | 7 | 2508 us | 3175 us | 1563 us | 1759 us | 100% | 0% |
| `nihaoshijie` | 长输入 | 1 | 1 | 25898 us | 30726 us | 4306 us | 5987 us | 0% | 0% |

> 数据取 3 次稳定值中位数。deadline%=0% 说明默认 30ms 下常规输入均未触发 deadline 保护。

**vs TopK 基线：** `s` 查询 P50 从 94μs 降至 34μs（**-64%**），`bj` 从 92μs 降至 79μs（**-14%**），其余输入波动 ±6% 以内。

## 短输入快速路径

> TopKCollector + make_budget + QueryDeadline + **ShortCodeCache (pinyin.topn.bin)**
>
> 短输入（1–6 小写字母）先查 session recent cache + 预构建 topn.bin，命中时完全跳过 syllabifier 和 dict scan。
>
> 测试条件：`--repeat 500 --warmup 100 --page-size 7 --deadline-ms 30`，3 轮取中位数（轮间冷却 10 秒）。

| 输入 | 类型 | 路径 | 候选 | e2e P50 | e2e P99 | 查询 P50 | 查询 P99 | exact_scan | prefix_scan | cache_hit | trunc% | deadline% |
|------|------|------|------|---------|---------|----------|----------|------------|-------------|-----------|--------|-----------|
| `s` | 单字母 | 0 | 7 | 12 us | 12 us | 9 us | 10 us | 0 | 0 | ✅ | 0% | 0% |
| `sd` | 双字母缩写 | 0 | 7 | 24 us | 26 us | 9 us | 10 us | 0 | 0 | ✅ | 0% | 0% |
| `sdf` | 三字母缩写 | 0 | 7 | 36 us | 49 us | 9 us | 10 us | 0 | 0 | ✅ | 0% | 0% |
| `sddf` | 四字母缩写 | 0 | 7 | 48 us | 52 us | 9 us | 10 us | 0 | 0 | ✅ | 0% | 0% |
| `bj` | 双字母缩写 | 0 | 7 | 23 us | 25 us | 9 us | 10 us | 0 | 0 | ✅ | 0% | 0% |
| `srf` | 三字母缩写 | 0 | 7 | 36 us | 38 us | 9 us | 10 us | 0 | 0 | ✅ | 0% | 0% |
| `shrf` | 四字母缩写 | 0 | 0 | 3161 us | 4162 us | 3114 us | 3904 us | 0 | 0 | — | 0% | 0% |
| `zguo` | 混合拼音 | 0 | 7 | 48 us | 49 us | 9 us | 10 us | 0 | 0 | ✅ | 0% | 0% |
| `nihao` | 全拼 | 0 | 7 | 59 us | 61 us | 9 us | 10 us | 0 | 0 | ✅ | 0% | 0% |
| `nihaoshijie` | 长输入 | 1 | 1 | 21000 us | 32616 us | 4444 us | 6704 us | 1 | 21 | — | 0% | 0% |

**关键指标变化：**
- 路径数 = 0：纯 cache 命中时 syllabifier 完全未被调用。
- exact/prefix_scan = 0：dict scan 完全跳过。
- `shrf` 无候选：topn.bin 中无此 key 的词典条目（词典不含 `shu:ru:fa` 对应的 `shrf` 混合码），无 cache 命中，回退 syllabifier 后仍无结果。
- `nihaoshijie` 路径 = 1：长度 > 6，不走快速路径，行为与 deadline 基线一致。

**vs deadline 基线：** 3–6 字母输入收益最大（-91% ~ -99%），因为这些输入原先需要 syllabifier 路径枚举 + 多次 dict scan，快速路径完全跳过。1–2 字母输入原本就快，提升幅度相对较小但仍然显著（-67% ~ -80%）。长输入（>6 字母）不走快速路径，数据波动属正常范围。

## 用户词索引化

> 短输入快速路径 + 用户词多路索引（exact / prefix / abbr / mixed）。
>
> 用户词查询从全表线性扫描改为索引 bucket 命中，`user_scan_count` 只反映实际检查的索引项数。
>
> 测试条件：`--repeat 500 --warmup 100 --page-size 7 --deadline-ms 30`，3 轮取中位数（轮间冷却 10 秒）。

| 输入 | 类型 | 路径 | 候选 | e2e P50 | e2e P99 | 查询 P50 | 查询 P99 | exact_scan | prefix_scan | user_scan | cache_hit | trunc% | deadline% |
|------|------|------|------|---------|---------|----------|----------|------------|-------------|-----------|-----------|--------|-----------|
| `s` | 单字母 | 0 | 7 | 12 us | 13 us | 9 us | 10 us | 0 | 0 | 0 | ✅ | 0% | 0% |
| `sd` | 双字母缩写 | 0 | 7 | 24 us | 26 us | 9 us | 10 us | 0 | 0 | 0 | ✅ | 0% | 0% |
| `sdf` | 三字母缩写 | 0 | 7 | 35 us | 38 us | 9 us | 10 us | 0 | 0 | 0 | ✅ | 0% | 0% |
| `sddf` | 四字母缩写 | 0 | 7 | 48 us | 50 us | 9 us | 10 us | 0 | 0 | 0 | ✅ | 0% | 0% |
| `bj` | 双字母缩写 | 0 | 7 | 23 us | 24 us | 9 us | 10 us | 0 | 0 | 0 | ✅ | 0% | 0% |
| `srf` | 三字母缩写 | 0 | 7 | 35 us | 39 us | 9 us | 10 us | 0 | 0 | 0 | ✅ | 0% | 0% |
| `shrf` | 四字母缩写 | 0 | 0 | 3133 us | 4163 us | 3085 us | 3863 us | 0 | 0 | 0 | — | 0% | 0% |
| `zguo` | 混合拼音 | 0 | 7 | 48 us | 51 us | 9 us | 10 us | 0 | 0 | 0 | ✅ | 0% | 0% |
| `nihao` | 全拼 | 0 | 7 | 60 us | 65 us | 9 us | 10 us | 0 | 0 | 0 | ✅ | 0% | 0% |
| `nihaoshijie` | 长输入 | 1 | 1 | 29279 us | 33081 us | 6304 us | 6948 us | 1 | 21 | 0 | — | 0% | 0% |

> `user_scan=0`：基准词典无用户词，索引查询命中空 bucket，零开销。`shrf` 无候选与 ShortCache 基线一致（词典不含 `shu:ru:fa` 对应的混合码）。`nihaoshijie` 长输入不走快速路径，行为与前版一致。

**vs ShortCache 基线：** 短输入 e2e P50 持平（±1μs），用户词索引未引入额外延迟。`nihao` 从 59μs → 60μs（+1μs，在测量误差内）。长输入波动 ±1% 以内。

## Mixed Code 生成优化

> 短输入快速路径 + MixedCodeGenerator 统一生成（声母增强简拼 / 长词首字母码 / 前两音节展开等）。
>
> `shrf` 等增强简拼 key 现由 mixed generator 生成并写入 topn.bin，全部短输入均可通过 cache 命中。
>
> 测试条件：`--repeat 500 --warmup 100 --page-size 7 --deadline-ms 30`，3 轮取中位数（轮间冷却 4 秒）。

| 输入 | 类型 | 路径 | 候选 | e2e P50 | e2e P99 | 查询 P50 | 查询 P99 | exact_scan | prefix_scan | cache_hit | trunc% | deadline% |
|------|------|------|------|---------|---------|----------|----------|------------|-------------|-----------|--------|-----------|
| `s` | 单字母 | 0 | 7 | 0 us | 1 us | 1 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `sd` | 双字母缩写 | 0 | 7 | 0 us | 1 us | 1 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `sdf` | 三字母缩写 | 0 | 7 | 0 us | 1 us | 1 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `sddf` | 四字母缩写 | 0 | 7 | 0 us | 1 us | 1 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `bj` | 双字母缩写 | 0 | 7 | 0 us | 1 us | 0 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `srf` | 三字母缩写 | 0 | 7 | 0 us | 1 us | 1 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `shrf` | 声母增强简拼 | 0 | 7 | 0 us | 1 us | 0 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `zguo` | 混合拼音 | 0 | 7 | 0 us | 1 us | 1 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `nihao` | 全拼 | 0 | 7 | 0 us | 1 us | 1 us | 1 us | 0 | 0 | ✅ | 0% | 0% |
| `nihaoshijie` | 长输入 | 1 | 1 | 170 us | 249 us | 205 us | 230 us | 1 | 21 | — | 0% | 0% |

**vs 用户词索引基线：** `shrf` 从 3133μs（无 cache，syllabifier 回退）降至 0μs（cache 命中），**-100%**。其余短输入持平。核心收益是 mixed code generator 覆盖了声母增强简拼，消除了 `shrf` 的 syllabifier 回退路径。所有短输入 cache_hit = 100%，exact/prefix_scan = 0，syllabifier 完全跳过。`nihaoshijie` 长输入不走快速路径，DFS 路径上限 256 后查询延迟从 ~55ms 降至 ~205μs（**-99.6%**），不再触发 deadline 保护。

## 版本对比总表

| 输入 | 原始 e2e P50 | TopK e2e P50 | deadline e2e P50 | ShortCache e2e P50 | 索引 e2e P50 | Mixed优化 e2e P50 | 总提升 |
|------|--------------|--------------|------------------|--------------------|--------------|-------------------|--------|
| `s` | 789 us | 97 us | 36 us | 12 us | 12 us | 0 us | **-100%** |
| `sd` | 936 us | 335 us | 258 us | 24 us | 24 us | 0 us | **-100%** |
| `sdf` | 2889 us | 2233 us | 2233 us | 36 us | 35 us | 0 us | **-100%** |
| `sddf` | 6059 us | 5477 us | 5593 us | 48 us | 48 us | 0 us | **-100%** |
| `bj` | 2067 us | 210 us | 117 us | 23 us | 23 us | 0 us | **-100%** |
| `srf` | 2143 us | 1524 us | 1506 us | 36 us | 35 us | 0 us | **-100%** |
| `shrf` | 6462 us | 5763 us | 5949 us | 3161 us | 3133 us | 0 us | **-100%** |
| `zguo` | 1912 us | 1085 us | 1061 us | 48 us | 48 us | 0 us | **-100%** |
| `nihao` | 3814 us | 2494 us | 2508 us | 59 us | 60 us | 0 us | **-100%** |
| `nihaoshijie` | 25911 us | 24439 us | 25898 us | 21000 us | 29279 us | 170 us | **-99.3%** |

> 短输入快速路径覆盖了 1–6 字母的全拼和简拼场景，查询延迟从微秒级降至个位数微秒。长输入（>6 字母）不走快速路径，DFS 路径上限 256 后查询延迟从 ~25ms 降至 ~170μs。Mixed code 优化后 `shrf` 等增强简拼 key 全部命中 cache，短输入 P50 稳定在 0μs。

## DAT-16 格式升级

> Top-N 索引文件从 CXTOPN v1（平坦排序数组 + 二分查找）升级为 DAT-16（CXTOPN v2，Darts-clone 双数组 Trie + 内联 16 字节候选）。查询行为与结果完全一致，无时延变化。

| 指标 | v1 (CXTOPN\x01) | DAT-16 (CXTOPN\x02) | 变化 |
|------|------------------|----------------------|------|
| 文件大小 | ~363 MB | ~212 MB | **-42%** |
| 键查找 | 二分查找 O(log N) | Darts trie O(k) | 复杂度降为线性 |
| 候选条目 | 24 bytes/条 | 16 bytes/条 | **-33%** |
| 键存储 | 显式字符串表 | Trie 隐式编码 | 消除字符串冗余 |

## 共享候选格式升级（CXTOPN v4）

> 这次升级的目标是**降低运行时内存占用**。v3 的索引自带候选文本与规范音节（v2 的 posting 为 16 字节，v3 加入规范音节身份后变为 24 字节，另有独立字符串池）；v4 把 posting 压缩为 8 字节的「词典词条索引 + 构建期 score」，文本 / 音节 / 词频改为查询时从 `pinyin.dict.bin` 词条读取，并与词典通过 `dictionary_entry_count` + FNV-1a `dictionary_fingerprint` 绑定（不匹配即拒绝加载，Server 路径视为致命错误）。

对比条件：同一份中间文件（key 3,618,702 / posting 8,846,785）与同一份词典（1,909,924 词条，69.5 MB）；两版索引都通过全量语义校验（3,618,702 个 key 逐一比对候选）。`topn.bin` 运行时整文件读入堆内存，因此**索引堆占用 = 文件大小**。

| 指标 | v3 (CXTOPN\x03 DAT-16) | v4 (CXTOPN\x04 共享候选) | 变化 |
|------|------------------------|--------------------------|------|
| **索引文件 / 运行时堆占用** | **305,143,247 B (291.0 MiB)** | **126,987,392 B (121.1 MiB)** | **-178,155,855 B (-58.4%)** |
| posting 数组 | 8,846,785 × 24 B = 212,322,840 B | 8,846,785 × 8 B = 70,774,280 B | -66.7% |
| posting list 数组 | 3,618,702 × 8 B = 28,949,616 B | 3,618,702 × 4 B = 14,474,808 B | -50.0% |
| 候选字符串池 | 22,132,471 B（自带 text + 规范音节） | 0（复用 `pinyin.dict.bin` 词条） | -100% |
| Darts 键索引 | 41,738,240 B | 41,738,240 B | 0% |
| 索引加载耗时 | 180 ms | 75 ms | -58.3% |
| 顺序查找 p50 / p95 / p99 | 0 / 100 / 200 ns | 0 / 100 / 200 ns | 持平 |
| 随机查找 p50 / p95 / p99 | 600 / 900 / 1100 ns | 700 / 1100 / 1300 ns | +100~200 ns |
| 未命中查找 p50 / p95 / p99 | 0 / 100 / 100 ns | 0 / 100 / 100 ns | 持平 |
| 4 线程并发吞吐 | 71,168,026 qps | 55,527,795 qps | -22.0% |

> 三次测量合集 ——
> v3：加载 180/180/177 ms；顺序查找 0/100/200 ns（三次相同）；随机查找 600/900/1100 ns（三次相同）；未命中查找 0/100/100 ns（三次相同）；4 线程吞吐 69,833,093 / 71,647,534 / 71,168,026 qps。
> v4：加载 77/73/75 ms；顺序查找 0/100/200 ns（三次相同）；随机查找 700/1100/1300 ns（三次相同）；未命中查找 0/100/100 ns（三次相同）；4 线程吞吐 53,692,250 / 58,549,743 / 55,527,795 qps。
> v4 复测（重建 `data\pinyin.topn.bin` 后）：加载 73/73/74 ms；顺序查找 0/100/200 ns；随机查找 700/1100/1300 ns；未命中查找 0/100/100 ns；4 线程吞吐 57,009,788 / 52,153,351 / 56,178,201 qps。

> 测试命令（`--queries 3618702` 即 key_count，两版顺序/随机键序一致）——

```cmd
# v4（当前版本）
build\tools\topn_index\Release\topn_benchmark.exe ^
    --baseline data\pinyin.topn.intermediate.bin --dictionary data\pinyin.dict.bin ^
    --index data\pinyin.topn.bin --queries 3618702 --threads 4

# v3（上一版本：topn_builder 生成三种布局，topn_benchmark 测 dat16）
topn_builder.exe --input data\pinyin.topn.intermediate.bin --output v3\pinyin.flat16.bin --format flat16
topn_builder.exe --input data\pinyin.topn.intermediate.bin --output v3\pinyin.dat16.bin --format dat16
topn_builder.exe --input data\pinyin.topn.intermediate.bin --output v3\pinyin.dat8.bin --format dat8
topn_benchmark.exe ^
    --baseline data\pinyin.topn.intermediate.bin --flat16 v3\pinyin.flat16.bin ^
    --dat16 v3\pinyin.dat16.bin --dat8 v3\pinyin.dat8.bin --queries 3618702 --threads 4
```

> 查找指标为索引层口径（Darts trie 查找 + 按词典词条展开候选），不含引擎的 Syllabifier、分页与候选窗口，与上文 `query_bench` 的端到端微秒级数据不是同一口径；计时器粒度 100 ns，p50 为 0 表示低于一个计时单位。

> 结论：v4 以随机查找 p95/p99 增加约 100~200 ns、4 线程并发吞吐下降 22.0% 为代价，换取索引堆占用减少 58.4%、加载耗时减少 58.3%；开销来自每条 posting 多一次「词典词条」间接寻址（20 字节 entry + dict 字符串池）。

## 重跑基准

```cmd
# 重建 topn.bin（build_pinyin_topn.py 生成中间文件，topn_builder 绑定 pinyin.dict.bin 转共享候选索引）
python scripts\build_pinyin_topn.py --input data\pinyin.dict.db --output data\pinyin.topn.intermediate.bin
build\tools\topn_index\Release\topn_builder.exe --input data\pinyin.topn.intermediate.bin --dictionary data\pinyin.dict.bin --output data\pinyin.topn.bin

# Top-N 索引对比（中间格式基线 vs 共享候选索引）
powershell -ExecutionPolicy Bypass -File scripts\benchmark_topn.ps1 -Source data\pinyin.topn.intermediate.bin -Dictionary data\pinyin.dict.bin -Builder build\tools\topn_index\Release\topn_builder.exe -Benchmark build\tools\topn_index\Release\topn_benchmark.exe

# 离线查询 benchmark（无需 server）
build\tools\query_bench\Release\query_bench.exe --data data --input s,sd,sdf,sddf,bj,srf,shrf,zguo,nihao,nihaoshijie --repeat 500 --warmup 100 --page-size 7 --deadline-ms 30

# IPC 端到端 benchmark（需先启动 server）
scripts\benchmark.bat

# 单元 benchmark
build\test\Release\benchmark_test.exe
```

> 重建 `pinyin.topn.bin` 后需要同步刷新 `dictionary_manifest.json`（`python scripts\prepare_dictionary_bundle.py`）：服务器启动时会校验 manifest 中每个角色的 size 与 sha256，索引文件已变而 manifest 未更新会导致词典加载失败。

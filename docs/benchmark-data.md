# 性能基准数据

本文档记录各版本的查询延迟基准数据，用于回归对比。

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

## 版本对比总表

| 输入 | 原始 e2e P50 | TopK e2e P50 | deadline e2e P50 | ShortCache e2e P50 | 总提升 |
|------|--------------|--------------|------------------|----------------|--------|
| `s` | 789 us | 97 us | 36 us | 12 us | **-98%** |
| `sd` | 936 us | 335 us | 258 us | 24 us | **-97%** |
| `sdf` | 2889 us | 2233 us | 2233 us | 36 us | **-99%** |
| `sddf` | 6059 us | 5477 us | 5593 us | 48 us | **-99%** |
| `bj` | 2067 us | 210 us | 117 us | 23 us | **-99%** |
| `srf` | 2143 us | 1524 us | 1506 us | 36 us | **-98%** |
| `shrf` | 6462 us | 5763 us | 5949 us | 3161 us | **-51%** |
| `zguo` | 1912 us | 1085 us | 1061 us | 48 us | **-97%** |
| `nihao` | 3814 us | 2494 us | 2508 us | 59 us | **-98%** |
| `nihaoshijie` | 25911 us | 24439 us | 25898 us | 21000 us | ~波动 |

> 短输入快速路径覆盖了 1–6 字母的全拼和简拼场景，查询延迟从微秒级降至个位数微秒。长输入（>6 字母）不走快速路径，行为与 deadline 基线一致。

## 重跑基准

```cmd
# 离线查询 benchmark（无需 server）
build\tools\query_bench\Release\query_bench.exe --data data --input s,sd,sdf,sddf,bj,srf,shrf,zguo,nihao,nihaoshijie --repeat 500 --warmup 100 --page-size 7 --deadline-ms 30

# IPC 端到端 benchmark（需先启动 server）
scripts\benchmark.bat

# 单元 benchmark
build\test\Release\benchmark_test.exe
```

# 词典数据与构建工具

`data/` 保存 CxxIME 的词典源数据、默认配置和词典构建工具。运行时二进制词典由这里的源数据生成，
不应手工修改。

## 数据分类

| 类型 | 文件 | 是否提交 | 说明 |
|------|------|---------|------|
| 词典源数据 | `pinyin.dict.db.zip` | 是 | 拼音 SQLite 词典的压缩分发副本 |
| 词典源数据 | `wubi86.dict.db.zip` | 是 | 未修改的五笔 86 词典源数据 |
| 授权材料 | `licenses/rime-ice-GPL-3.0.txt` | 是 | 雾凇拼音 GPL-3.0-only 完整许可证 |
| 可审查派生数据 | `symbols.json` | 是 | 从五笔源词典拆出的独立符号分类 |
| 默认配置 | `default.json`、`themes.json` 等 | 是 | 安装包使用的出厂配置 |
| 临时源数据 | `*.dict.db` | 否 | 从压缩源解包或下载得到的 SQLite 文件 |
| 运行时数据 | `*.dict.bin`、`*.dict.idx`、`*.spellings.bin`、`*.topn.bin`、`*.reverse.idx` | 否 | 打包阶段生成的二进制文件 |

`dictionary_manifest.json` 由打包流水线在所有运行时数据生成完毕后写入，记录文件角色、大小和
SHA-256；它同样不作为源文件维护。

## 数据授权

- 拼音词典派生自 [rime-ice](https://github.com/iDvel/rime-ice)，按 GPL-3.0-only
  发布。完整许可证保存在 `licenses/rime-ice-GPL-3.0.txt`，并随安装包分发。
- 五笔词典和 `symbols.json` 派生自
  [rime-wubi86-jidian](https://github.com/KyleBing/rime-wubi86-jidian)，按
  Apache-2.0 发布。
- 项目代码的 Apache-2.0 许可证不替代上述第三方词典数据各自的许可证。

## 本地工具入口

需要单独调试 SQLite 到运行时格式的转换时，可使用:

```bat
python data\tools\build_runtime_dictionary.py ^
    --input data\pinyin.dict.db --output data\pinyin
```

五笔完整前缀索引必须显式指定，且输入必须是已经由 `split_wubi_symbols.py` 过滤符号扩展项的
临时 SQLite 词典：

```bat
python data\tools\build_runtime_dictionary.py ^
    --input <filtered-wubi-db> --output data\wubi86 ^
    --dict-only --wubi-prefix-index ^
    --wubi-ranking-source data\pinyin.dict.db.zip ^
    --wubi-ranking-baseline data\tools\dict_builder\wubi_ranking_baseline.json
```

## 工具职责

`data/tools/` 中的文件名使用“词典领域 + 动作或产物”命名，避免无法判断用途的通用名称。

| 工具 | 职责 |
|------|------|
| `build_runtime_dictionary.py` | 将 SQLite 源词典转换为运行时词典和对应索引 |
| `fetch_pinyin_dictionary.py` | 下载并生成拼音 SQLite 源词典 |
| `fetch_wubi_dictionary.py` | 下载并生成五笔 SQLite 源词典 |
| `convert_rime_dictionary.py` | 将其他 RIME YAML 词典转换为 SQLite 格式 |
| `generate_pinyin_spellings.py` | 根据拼音 schema 生成 spellings 表 |
| `generate_pinyin_syllable_ids.py` | 为拼音 SQLite 词典生成音节 ID 分段字段 |
| `split_wubi_symbols.py` | 从五笔源词典生成符号表和过滤后的临时词典 |

`data/tools/dict_builder/` 是构建实现包，不是面向用户的命令集合：

| 模块 | 生成内容 |
|------|---------|
| `runtime_dictionary.py` | 通用 `.dict.bin` |
| `pinyin_spellings.py` | 拼音 `.spellings.bin` |
| `pinyin_syllable_index.py` | 拼音 `.dict.idx` |
| `wubi_prefix_index.py` | 五笔完整前缀候选 `.dict.idx` |
| `wubi_ranking.py` | 五笔可见候选的离线语料排序与全量验收 |
| `reverse_index.py` | Settings 词语反查使用的 `.reverse.idx` |
| `source_archive.py` | 安全解析 `.dict.db` 与 `.dict.db.zip` 输入 |

## 数据流水线

拼音：

```text
pinyin.dict.db.zip
  -> 临时 SQLite
  -> 拼写规则展开
  -> dict.bin + spellings.bin + syllable idx + reverse idx
```

五笔：

```text
wubi86.dict.db.zip
  -> 临时 SQLite
  -> symbols.json + 过滤后的临时 SQLite
  -> 读取 pinyin.dict.db.zip 的通用词频
  -> dict.bin + 完整前缀候选 idx + reverse idx
```

生成文件必须从源数据重建，不应手工编辑或提交。

## 下载与转换示例

拼音、五笔源词典的下载与转换（在 `data/` 目录下执行）：

```bat
python tools\fetch_pinyin_dictionary.py      :: 下载并生成 pinyin.dict.db（约 90 MB）
python tools\fetch_wubi_dictionary.py        :: 下载并生成 wubi86.dict.db
python tools\convert_rime_dictionary.py input.yaml output.db   :: 转换其他 RIME 词典
```

下载得到的 `.dict.db` 为临时源数据，不提交到仓库；仅重新打包后的 `.dict.db.zip` 提交。

## 维护流程

完整数据链路：**zip → db → 拼写规则展开 → bin/idx/spellings + Top-N 中间文件 → DAT-16 → manifest**

```bat
:: 0. 首次拉取后解压（仅一次）
cd data && python -c "import zipfile; zf = zipfile.ZipFile('pinyin.dict.db.zip'); zf.extractall()"

:: 1. 生成拼写表（spellings，应用 schema 拼写代数规则）
python data\tools\generate_pinyin_spellings.py data\pinyin.dict.db

:: 2. 生成全部二进制文件（.bin + .idx + .spellings.bin）
python data\tools\build_runtime_dictionary.py --input data\pinyin.dict.db --output data\pinyin

:: 3. 生成 Top-N 中间文件并转换为 DAT-16（需要 topn_builder）
python scripts\build_pinyin_topn.py --input data\pinyin.dict.db --output data\pinyin.topn.bin
build\tools\topn_index\Release\topn_builder.exe ^
    --input data\pinyin.topn.bin --output data\pinyin.topn.bin --format dat16
```

以上步骤也可以用 `scripts\prepare_dictionary_bundle.py` 一键完成（并行处理拼音和五笔，自动生成 manifest）：

```bat
python scripts\prepare_dictionary_bundle.py ^
    --data-dir data --output-dir data ^
    --topn-builder build\tools\topn_index\Release\topn_builder.exe
```

五笔链路：`wubi86.dict.db.zip` → 解包 → `split_wubi_symbols.py` 拆出 `symbols.json` 和过滤后的临时词典 → `build_runtime_dictionary.py --dict-only --wubi-prefix-index` 生成 `wubi86.dict.bin` + `wubi86.dict.idx`（完整前缀索引）。

如果修改了 `.db`（修复脏数据等），需要重新打包 zip：

```bat
cd data && del pinyin.dict.db.zip && python -c "
import zipfile; zf = zipfile.ZipFile('pinyin.dict.db.zip', 'w', zipfile.ZIP_DEFLATED)
zf.write('pinyin.dict.db'); zf.close()
"
```

> **注意：** `.db`、`.bin`、`.idx`、`.spellings.bin` 均不入库，仅 `.db.zip` 提交。其他开发者拉取后从步骤 0 开始。

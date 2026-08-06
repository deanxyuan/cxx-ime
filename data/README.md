# 词典数据与构建工具

`data/` 保存 CxxIME 的词典源数据、默认配置和词典构建工具。运行时二进制词典由这里的源数据生成，
不应手工修改。

## 数据分类

| 类型 | 文件 | 是否提交 | 说明 |
|------|------|---------|------|
| 词典源数据 | `pinyin.dict.db.zip` | 是 | 拼音 SQLite 词典的压缩分发副本 |
| 词典源数据 | `wubi86.dict.db.zip` | 是 | 未修改的五笔 86 词典源数据 |
| 可审查派生数据 | `symbols.json` | 是 | 从五笔源词典拆出的独立符号分类 |
| 默认配置 | `default.json`、`themes.json` 等 | 是 | 安装包使用的出厂配置 |
| 临时源数据 | `*.dict.db` | 否 | 从压缩源解包或下载得到的 SQLite 文件 |
| 运行时数据 | `*.dict.bin`、`*.dict.idx`、`*.spellings.bin`、`*.topn.bin` | 否 | 打包阶段生成的二进制文件 |

`dictionary_manifest.json` 由打包流水线在所有运行时数据生成完毕后写入，记录文件角色、大小和
SHA-256；它同样不作为源文件维护。

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
    --dict-only --wubi-prefix-index
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
| `source_archive.py` | 安全解析 `.dict.db` 与 `.dict.db.zip` 输入 |

## 数据流水线

拼音：

```text
pinyin.dict.db.zip
  -> 临时 SQLite
  -> 拼写规则展开
  -> dict.bin + spellings.bin + syllable idx
```

五笔：

```text
wubi86.dict.db.zip
  -> 临时 SQLite
  -> symbols.json + 过滤后的临时 SQLite
  -> dict.bin + 完整前缀候选 idx
```

生成文件必须从源数据重建，不应手工编辑或提交。

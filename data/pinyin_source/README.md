# 拼音原始源的离线修正规则

原始 `../pinyin.dict.db.zip` 保持不变。普通构建在私有 SQLite 副本中修正已核实的
音节边界，排除被误作编码的未注音记录，然后生成运行时文件。按键路径不执行修正。

`reading_corrections.tsv` 是工具生成的稀疏修正表，不逐项手工维护。列为记录 ID、
文字、连续编码、旧音节、新音节；只修改音节字段，保留原 ID、文字、编码和词频。
`manifest.json` 固定原数据库身份、修正表摘要、数量和上游来源。

## 日常构建

从仓库根目录运行统一入口，自动应用本目录规则；不需要下载上游文件：

```bat
python scripts\prepare_dictionary_bundle.py --data-dir data --output-dir data ^
    --topn-builder build\tools\topn_index\Release\topn_builder.exe
```

只需检查修正后的 SQLite 时，指定一个不存在的输出文件：

```bat
python data\tools\prepare_pinyin_source.py --input data\pinyin.dict.db.zip ^
    --output prepared-pinyin.dict.db
```

输出包含 928,290 条已注音记录，修正 33,943 条音节边界，排除 981,634 条误导入
记录。后续仍须使用统一 bundle 入口执行符号过滤、拼写和全部索引生成。
五笔排序继续读取原始拼音 ZIP 的语料，不读取这个修正副本。

## 维护时重新生成规则

来源为 rime-ice 的固定提交
[`785489b3220dfca98d7ce632e590f416faea4fdf`](https://github.com/iDvel/rime-ice/tree/785489b3220dfca98d7ce632e590f416faea4fdf/cn_dicts)。
先下载该版本的[源码 ZIP](https://github.com/iDvel/rime-ice/archive/785489b3220dfca98d7ce632e590f416faea4fdf.zip)，
再从仓库根目录运行，输出目录必须不存在：

```bat
python data\tools\generate_pinyin_source_corrections.py ^
    --input data\pinyin.dict.db.zip --upstream rime-ice-source.zip ^
    --output-dir regenerated-pinyin-source
```

生成器核对六份上游文件的原始字节摘要，不依赖 ZIP 外层目录名，也不联网。
全部保留记录须按文字和连续编码匹配到唯一显式读音；拟排除记录须存在于
腾讯未注音词表，且原库编码、词频和旧标注符合已审计特征。缺少证据或出现
歧义会失败，不猜读音。生成后比较 TSV 的规范化文本和 manifest 内容。

原始 ZIP SHA-256：

```text
7ee978b1d839e9b26e08cce4af66d2b664c0df7706ca48288fa70aec0e204fc5
```

数据库摘要及逐文件来源摘要见 manifest。修正表摘要统一文本换行后计算，
原 ZIP、数据库和上游 YAML 的身份摘要按原始字节计算。

规则只适用于该原始数据库及其逐字节解包副本。其他显式注音数据库只做结构校验，
不套用旧规则、不自动删除 `100` 记录。结构校验不能判断诸如“西安/xian”的词读音
是否正确，自定义源的读音准确性由提供者负责。更换原 ZIP 必须重新审核来源。

本目录派生词典数据沿用 rime-ice 的 GPL-3.0-only 授权，完整许可证见
[`../licenses/rime-ice-GPL-3.0.txt`](../licenses/rime-ice-GPL-3.0.txt)。

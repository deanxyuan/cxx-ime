# CxxIME

轻量级 Windows TSF 输入法（拼音 / 五笔 / 混输）

A lightweight Windows TSF (Text Services Framework) input method (Pinyin + Wubi + Mixed).

## 项目简介

CxxIME 是一个基于 Windows TSF (Text Services Framework) 的输入法，支持拼音、五笔和拼音五笔混输三种模式，采用客户端/服务端架构设计。TSF DLL 负责捕获按键并通过 IPC 与后台服务端通信，服务端执行拼音解析、词典查询和候选生成。项目目录结构见 [docs/architecture.md](docs/architecture.md)。

主要能力：

- 拼音、五笔 86 和混合输入模式，支持简拼、模糊及五笔简码
- Top-N 候选按匹配质量分层排序，避免高频长词压过精确音节和接近完成的词
- 候选学习默认关闭，开启后将选词结果持久化到用户词典，可在设置中删除词条恢复系统排序
- 五笔四码唯一候选自动上屏，可在设置中关闭
- 支持应用宿主通过 TSF UIElement 接管 inline preedit 和候选窗口绘制，DOTA2 场景已验证

## 环境要求

- Windows 10/11
- Visual Studio 2022（或 Build Tools），需要 C++ 工作负载
- CMake 3.15+
- Python 3.6+（用于词典下载工具，可选）

## 构建

```cmd
build.bat              # Release 构建
build.bat debug        # Debug 构建
build.bat clean        # 清理构建目录
```

构建产物在 `build/<config>/` 目录下：

- `cxxime_tsf_x64.dll` / `cxxime_tsf_x86.dll` — TSF 文本服务 DLL（双架构）
- `cxxime-resources.dll` — 输入法 profile 资源 DLL
- `cxxime-server.exe` — 后台服务进程
- `cxxime-settings.exe` — 配置编辑器
- `test/` — 测试可执行文件（每个测试文件一个 exe）

## 获取词典

CxxIME 使用 SQLite 格式的词典作为**源数据**，构建时通过 `data/tools/build_runtime_dictionary.py`（`dict_builder` 包）转换为二进制格式（一次性读入内存）。

### 拼音词典

来源：[rime-ice](https://github.com/iDvel/rime-ice)（雾凇拼音，约 190 万词条）
许可证：GPL-3.0-only；完整文本见
[`data/licenses/rime-ice-GPL-3.0.txt`](data/licenses/rime-ice-GPL-3.0.txt)。

```cmd
cd data
python tools/fetch_pinyin_dictionary.py   # 下载 → data/pinyin.dict.db（约 90 MB）
```

### 五笔词典

来源：[KyleBing/rime-wubi86-jidian](https://github.com/KyleBing/rime-wubi86-jidian)（五笔 86 极点版）
许可证：Apache-2.0。

```cmd
cd data
python tools/fetch_wubi_dictionary.py     # 下载 → data/wubi86.dict.db
```

也可以手动用 `convert_rime_dictionary.py` 转换其他 RIME 格式词典：

```cmd
python tools/convert_rime_dictionary.py input.yaml output.db
```

### 词典数据文件

| 文件 | 来源 | 是否提交 | 说明 |
|------|------|----------|------|
| `pinyin.dict.db.zip` | `fetch_pinyin_dictionary.py` 下载后压缩 | **是** | 拼音词典分发副本 |
| `wubi86.dict.db.zip` | `fetch_wubi_dictionary.py` 下载后压缩 | **是** | 五笔词典分发副本 |
| `pinyin.dict.db` | 解压 `.zip` 得到 | 否 | 拼音 SQLite 源词典 |
| `wubi86.dict.db` | 解压 `.zip` 得到 | 否 | 五笔 SQLite 源词典 |
| `pinyin.dict.bin` | `build_runtime_dictionary.py` 生成 | 否 | 拼音二进制词典（运行时） |
| `pinyin.spellings.bin` | `build_runtime_dictionary.py` 生成 | 否 | Patricia trie 拼写索引（运行时） |
| `pinyin.dict.idx` | `build_runtime_dictionary.py` 生成 | 否 | 音节 ID 索引（运行时） |
| `pinyin.topn.bin` | `build_pinyin_topn.py` + `topn_builder` 生成 | 否 | DAT-16 Top-N 索引（运行时） |
| `wubi86.dict.bin` | `build_runtime_dictionary.py` 生成 | 否 | 五笔二进制词典（运行时） |
| `wubi86.dict.idx` | `build_runtime_dictionary.py --wubi-prefix-index` 生成 | 否 | 五笔完整前缀索引（运行时） |
| `dictionary_manifest.json` | `prepare_dictionary_bundle.py` 生成 | 否 | 运行时词典 bundle 清单与校验哈希 |

### 词典维护

数据来源链路：**zip → db → 拼写规则展开 → bin/idx/spellings + Top-N 中间文件 → DAT-16 → manifest**

```cmd
# 0. 拉取后先解压（仅首次）
cd data && python -c "
import zipfile; zf = zipfile.ZipFile('pinyin.dict.db.zip'); zf.extractall()
"

# 1. 生成拼写表（spellings 表，应用 schema 拼写代数规则）
python data/tools/generate_pinyin_spellings.py data/pinyin.dict.db

# 2. 生成全部二进制文件（.bin + .idx + .spellings.bin）
python data/tools/build_runtime_dictionary.py --input data/pinyin.dict.db --output data/pinyin

# 3. 生成 Top-N 中间文件并转换为 DAT-16（需要 topn_builder）
python scripts/build_pinyin_topn.py --input data/pinyin.dict.db --output data/pinyin.topn.bin
build\tools\topn_index\Release\topn_builder.exe ^
    --input data\pinyin.topn.bin --output data\pinyin.topn.bin --format dat16
```

以上步骤也可以用 `scripts/prepare_dictionary_bundle.py` 一键完成（并行处理拼音和五笔，自动生成 manifest）：

```cmd
python scripts/prepare_dictionary_bundle.py ^
    --data-dir data --output-dir data ^
    --topn-builder build\tools\topn_index\Release\topn_builder.exe
```

五笔链路：`wubi86.dict.db.zip` → 解包 → `split_wubi_symbols.py` 拆出 `symbols.json` 和过滤后的临时词典 → `build_runtime_dictionary.py --dict-only --wubi-prefix-index` 生成 `wubi86.dict.bin` + `wubi86.dict.idx`（完整前缀索引）。

如果修改了 `.db`（修复脏数据等），更新 zip：

```cmd
cd data && del pinyin.dict.db.zip && python -c "
import zipfile; zf = zipfile.ZipFile('pinyin.dict.db.zip', 'w', zipfile.ZIP_DEFLATED)
zf.write('pinyin.dict.db'); zf.close()
"
```

> **注意：** `.db`、`.bin`、`.idx`、`.spellings.bin` 均在 `.gitignore` 中。仅 `.db.zip` 提交到仓库。其他开发者拉取后从步骤 0 开始。

## 打包

构建 + 词典转换 + NSIS 安装程序编译：

```cmd
scripts\package.py                     # Release 打包
scripts\package.py --debug             # Debug 打包
scripts\package.py --skip-dict         # 复用已有 dist/data 词典
scripts\package.py --fast              # NSIS 跳过大文件压缩
scripts\package.py --host-diag         # 宿主诊断包
```

安装包输出到 `..\output\cxxime-v<version>-setup.exe`，诊断包文件名包含
`-host-diag` 后缀。`version` 取自仓库根目录的 `VERSION`。

修改词典源数据或短码排序算法后，正式打包不得使用 `skip-dict`，必须重新生成
`pinyin.topn.bin` 和词典清单。

普通安装包不包含 IME Host Probe 和阶段日志导出工具，但包含通用的
`collect_diagnostics.ps1`。仅在复现宿主接管问题时使用 `--host-diag`。

需要预先安装 [NSIS 3.x](https://nsis.sourceforge.io/) 并确保 `makensis.exe` 在 PATH 中。如果未安装 NSIS，`package.py` 会跳过安装程序生成，`dist/` 目录中保留原始分发文件。

`package.py` 执行流程：构建（x64 + x86 双 TSF DLL）→ 复制配置 → 词典准备与校验（`prepare_dictionary_bundle.py` / `verify_dictionary_bundle.py`）→ NSIS 编译 → 输出单文件安装程序。

打包脚本默认使用 `build-package\` 构建目录，避免和 `build.bat` 的开发构建目录互相污染。CMake 生成器默认交给 CMake 和当前命令行环境决定，也可以通过 `--generator`、`--platform` 或环境变量 `CXXIME_CMAKE_GENERATOR`、`CXXIME_CMAKE_PLATFORM` 覆盖。

## 安装

运行 `cxxime-v<version>-setup.exe`，按向导提示操作：

1. 选择程序安装目录，默认安装到 `C:\Program Files\CxxIME`
2. 程序文件和出厂数据安装到安装目录，用户配置初始化到 `%USERPROFILE%\cxxime\`
3. 安装程序自动注册 TSF DLL（x64 + x86）、配置自启动、创建开始菜单快捷方式
4. 安装完成后**注销并重新登录**即可使用

## 卸载

- 开始菜单 → CxxIME → 卸载 CxxIME
- 或控制面板 → 添加/删除程序 → CxxIME

卸载默认只清理程序文件、开始菜单快捷方式、TSF 注册项、自启动项和卸载项，**默认保留** `%USERPROFILE%\cxxime\` 下的用户配置和用户词典。

## 配置

编辑用户目录下的 `%USERPROFILE%\cxxime\default.json`，或通过开始菜单打开 CxxIME Settings：

```json
{
    "engine": {
        "page_size": 7,
        "wubi_auto_commit": true,
        "candidate_learning": false
    },
    "style": {
        "font_face": "Microsoft YaHei UI",
        "font_point": 14,
        "layout": "horizontal",
        "render_backend": "d2d"
    },
    "theme": "azure"
}
```

开启 `candidate_learning` 后，选词学习记录会跨重启保留; 关闭该选项只停止后续学习，
不会删除已有记录。需要恢复某个编码的系统排序时，可在设置的用户词库中删除对应词条。

## 开发工具

构建后在 `build/tools/<name>/Debug/` 下：

| 工具 | 用途 |
|------|------|
| `dict_query` | 拼音/五笔查词（`--mode pinyin\|wubi`，wubi 模式支持 `--index` 指定完整前缀索引） |
| `sqlite_query` | 直读 `.db` 文件调试 |
| `ipc_tool` | IPC 交互测试（connect / key / bench / stress 等，源码在 `tools/ipc_test/`） |
| `query_bench` | 查询性能基准（配合 `scripts/check_query_bench.py` 做回归） |
| `punct_test` | 标点映射测试 |
| `candidate_window_tool` | 候选窗口可视化测试（主题/布局/D2D/preedit 切换） |
| `status_window_tool` | 状态窗口可视化测试 |
| `tray_icon_tool` | 托盘图标实验工具 |
| `tsf_position_tool` | 候选窗口定位测试（光标移动、屏幕边缘 clamp、多显示器适配） |

## 测试

`build.bat debug` 默认为开发构建（启用 tests 和 tools，数据目录指向项目 `data/`）。

```cmd
cd build
ctest -C Debug --output-on-failure
```

或单独运行某个测试：`build\test\Debug\ipc_test.exe`

当前共 40 个 ctest 条目（36 个 C++ 测试 + 4 个 Python 测试），510+ 用例。

## 文档

`docs/` 下为项目级文档（面向维护者、开发者与用户），主要入口：

- [架构总览](docs/architecture.md) — 总体架构、模块划分、技术选型与专题文档索引
- [安装与卸载](docs/installation.md) — 安装细节、手动注册与故障排查
- [设置指南](docs/settings-guide.md) — 设置窗口与配置文件参考

## 许可证

项目代码按 Apache License 2.0 发布。第三方组件和词典数据保留各自许可证，详见
[`THIRD_PARTY_NOTICES.txt`](THIRD_PARTY_NOTICES.txt)。

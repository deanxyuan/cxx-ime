# CxxIME

轻量级 Windows TSF 输入法（拼音 / 五笔 / 混输）

A lightweight Windows TSF (Text Services Framework) input method (Pinyin + Wubi + Mixed).

> 项目已进入日常试用阶段：核心输入功能、安装部署与配置体系均已完成。

## 项目简介

CxxIME 是一个基于 Windows TSF (Text Services Framework) 的输入法，支持拼音、五笔和拼音五笔混输三种模式，采用客户端/服务端架构设计。TSF DLL 负责捕获按键并通过 IPC 与后台服务端通信，服务端执行拼音解析、词典查询和候选生成。

## 功能特性

- **三种输入模式**：拼音（全拼/简拼/模糊音）、五笔 86（四码唯一自动上屏，可配置开启/关闭）、混输（按候选强度智能排序拼音与五笔结果）
- **用户词典与候选学习**：选词自动学习词频与音节键，学过的词可被简拼命中；支持 TSV 导入导出、设置界面增删改查；可通过 `candidate_learning` 控制学习开关
- **性能**：二进制堆加载词典 + Patricia trie 拼写索引 + 短码候选缓存 + 长拼音查询页缓存，IPC 单次往返 < 1ms
- **跨窗口状态一致**：中英文/大小写/全半角/标点/输入模式为全局状态，切换窗口不丢失
- **界面**：候选窗口 + 状态窗口，Direct2D / GDI 双渲染后端，14 套配色主题，DPI 缩放，横排/竖排布局
- **可观测性**：查询链路 trace（JSONL 采样落盘）、TSF 事件级追踪、一键诊断包导出

## 架构

```
cxx-ime/
├── shared/          共享类型、IPC 协议、日志、数据路径解析
├── engine/          输入引擎：音节切分、翻译器（拼音/五笔/混输）、词典、配置
├── ipc/             命名管道 IPC 客户端/服务端（IOCP）
├── server/          后台服务进程（共享资源 + 会话管理 + 配置/词典热重载）
├── tsf/             TSF 文本服务 DLL（由 Windows 加载）
├── ui/              候选窗口 + 状态窗口（D2D / GDI 双后端渲染）
├── settings/        配置编辑器 GUI（Win32 原生控件）
├── docs/            项目文档（设计与实现、安装、配置指南）
├── data/            词典文件、Python 工具和默认配置
├── resource/        图标与资源 DLL 素材
├── scripts/         打包、词典准备、校验脚本
├── tools/           开发调试工具（9 个）
├── test/            测试套件（22 个 C++ 测试 + 2 个 Python 测试）
└── third_party/     sqlite3, nlohmann/json
```

**输入流程：** 按键 → TSF DLL → IPC → 服务端 → 引擎 → IPC → TSF DLL → 文字上屏

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

CxxIME 使用 SQLite 格式的词典作为**源数据**，构建时通过 `build_binary.py` 转换为二进制格式（一次性读入内存）。

### 拼音词典

来源：[rime-ice](https://github.com/iDvel/rime-ice)（雾凇拼音，约 190 万词条）

```cmd
cd data
python tools/fetch_dict.py          # 下载 → data/pinyin.dict.db（约 90 MB）
```

### 五笔词典

来源：[KyleBing/rime-wubi86-jidian](https://github.com/KyleBing/rime-wubi86-jidian)（五笔 86 极点版）

```cmd
cd data
python tools/fetch_wubi.py          # 下载 → data/wubi86.dict.db
```

也可以手动用 `dict_convert.py` 转换其他 RIME 格式词典：

```cmd
python tools/dict_convert.py input.yaml output.db
```

### 词典数据文件

| 文件 | 来源 | 是否提交 | 说明 |
|------|------|----------|------|
| `pinyin.dict.db.zip` | `fetch_dict.py` 下载后压缩 | **是** | 拼音词典分发副本 |
| `wubi86.dict.db.zip` | `fetch_wubi.py` 下载后压缩 | **是** | 五笔词典分发副本 |
| `pinyin.dict.db` | 解压 `.zip` 得到 | 否 | 拼音 SQLite 源词典 |
| `wubi86.dict.db` | 解压 `.zip` 得到 | 否 | 五笔 SQLite 源词典 |
| `pinyin.dict.bin` | `build_binary.py` 生成 | 否 | 拼音二进制词典（运行时） |
| `pinyin.spellings.bin` | `build_binary.py` 生成 | 否 | Patricia trie 拼写索引（运行时） |
| `pinyin.dict.idx` | `build_binary.py` 生成 | 否 | 音节 ID 索引（运行时） |
| `pinyin.topn.bin` | `build_short_cache.py` 生成 | 否 | 短码候选缓存（运行时） |
| `wubi86.dict.bin` | `build_binary.py` 生成 | 否 | 五笔二进制词典（运行时） |
| `wubi86.dict.idx` | `build_binary.py` 生成 | 否 | 五笔编码索引（运行时） |
| `dictionary_manifest.json` | `prepare_dict.py` 生成 | 否 | 运行时词典 bundle 清单与校验哈希 |

> 五笔词典为**必选**：打包流程缺失 wubi86 源数据会直接报错。

### 词典维护

数据来源链路：**zip → db → algebra → bin/idx/spellings/topn → manifest**

```cmd
# 0. 拉取后先解压（仅首次）
cd data && python -c "
import zipfile; zf = zipfile.ZipFile('pinyin.dict.db.zip'); zf.extractall()
"

# 1. 生成拼写表（spellings 表，应用 schema 拼写代数规则）
python data/tools/spelling_algebra.py data/pinyin.dict.db

# 2. 生成全部二进制文件（.bin + .idx + .spellings.bin）
python data/tools/build_binary.py --input data/pinyin.dict.db --output data/pinyin

# 3. 如果修改了 .db（修复脏数据等），更新 zip
cd data && del pinyin.dict.db.zip && python -c "
import zipfile; zf = zipfile.ZipFile('pinyin.dict.db.zip', 'w', zipfile.ZIP_DEFLATED)
zf.write('pinyin.dict.db'); zf.close()
"
```

一键完成上述全部步骤（含五笔与短码缓存）：

```cmd
python scripts/prepare_dict.py --data-dir data/ --output-dir dist/data/
```

> **注意：** `.db`、`.bin`、`.idx`、`.spellings.bin` 均在 `.gitignore` 中。仅 `.db.zip` 提交到仓库。其他开发者拉取后从步骤 0 开始。

## 打包

构建 + 词典转换 + NSIS 安装程序编译：

```cmd
scripts\package.py                     # Release 打包 → ..\output\cxxime-v0.2.0-beta-setup.exe
scripts\package.py --debug             # Debug 打包
scripts\package.py --skip-dict         # 复用已有 dist/data 词典
scripts\package.py --fast --skip-dict  # 快速调试包（NSIS 跳过大文件压缩）
scripts\package.py --host-diag         # 包含宿主诊断探针（cxxime-ime-host-probe）
```

需要预先安装 [NSIS 3.x](https://nsis.sourceforge.io/) 并确保 `makensis.exe` 在 PATH 中。如果未安装 NSIS，`package.py` 会跳过安装程序生成，`dist/` 目录中保留原始分发文件。

`package.py` 执行流程：构建（x64 + x86 双 TSF DLL）→ 复制配置 → 词典转换（`.db` → `.bin`）→ 数据校验 → NSIS 编译 → 输出单文件安装程序。

打包脚本默认使用 `build-package\` 构建目录，避免和 `build.bat` 的开发构建目录互相污染。CMake 生成器默认交给 CMake 和当前命令行环境决定，也可以通过 `--generator`、`--platform` 或环境变量 `CXXIME_CMAKE_GENERATOR`、`CXXIME_CMAKE_PLATFORM` 覆盖。

## 安装

运行 `cxxime-v0.2.0-beta-setup.exe`，按向导提示操作：

1. 选择程序安装目录，默认安装到 `C:\Program Files\CxxIME`
2. 程序文件和出厂数据安装到安装目录，用户配置初始化到 `%USERPROFILE%\cxxime\`
3. 安装程序自动注册 TSF DLL（x64 + x86）、配置自启动、创建开始菜单快捷方式
4. 安装完成后**注销并重新登录**即可使用

## 卸载

- 开始菜单 → CxxIME → 卸载 CxxIME
- 或 Windows 设置 → 应用 → CxxIME

卸载默认只清理程序文件、开始菜单快捷方式、TSF 注册项、自启动项和卸载项，**默认保留** `%USERPROFILE%\cxxime\` 下的用户配置和用户词典；卸载向导中可勾选「删除用户配置和词典数据」一并清除。

## 快速上手

| 按键 | 功能 |
|------|------|
| 字母键 | 输入拼音/五笔编码，空格或数字键选词 |
| `PageUp` / `PageDown` | 候选翻页 |
| `Shift` | 切换中英文（左右 Shift 行为可分别配置，出厂：左 Shift 提交编码并切换，右 Shift 切到英文） |
| `CapsLock` | 切换模式（可配置 clear/code/candidate/append，关灯恢复原模式） |
| `Shift+Space` | 全角/半角切换 |
| `Ctrl+.` | 中文/英文标点切换 |

中英文、全半角等状态跨窗口保持一致；详细按键配置见 [设置指南](docs/settings-guide.md)。

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

开启 `candidate_learning` 后选词会自动学习到用户词库，提升个性化排序。需要恢复默认行为时，在设置的用户词库中删除对应词条即可。

## 开发工具

构建后在 `build/tools/<name>/Debug/` 下：

| 工具 | 用途 |
|------|------|
| `dict_query` | 拼音/五笔查词（`--mode pinyin\|wubi`，binary 词典） |
| `sqlite_query` | 直读 `.db` 文件调试 |
| `ipc_tool` | IPC 交互测试（connect / key / bench / stress 等，源码在 `tools/ipc_test/`） |
| `query_bench` | 查询性能基准（配合 `scripts/check_query_bench.py` 做回归） |
| `punct_test` | 标点映射测试 |
| `candidate_window_tool` | 候选窗口可视化测试（主题/布局/D2D/preedit 切换） |
| `status_window_tool` | 状态窗口可视化测试 |
| `tray_icon_tool` | 托盘图标实验工具 |
| `tsf_position_tool` | 候选窗口定位测试（光标移动、屏幕边缘 clamp、多显示器适配） |

## 测试

`build.bat` / `build.bat debug` 默认为开发构建（`CXXIME_PRODUCTION_BUILD=OFF`，数据目录指向项目 `data/`，启用 tests 和 tools）。

```cmd
cd build
ctest -C Debug
```

或单独运行某个测试：`build\test\Debug\ipc_test.exe`

当前共 24 个 ctest 条目（22 个 C++ 测试 exe + 2 个 Python 测试），390+ 用例。

## 文档

`docs/` 下为项目级文档（面向维护者、开发者与用户），主要入口：

- [架构总览](docs/architecture.md) — 总体架构、模块划分、技术选型与专题文档索引
- [安装与卸载](docs/installation.md) — 安装细节、手动注册与故障排查
- [设置指南](docs/settings-guide.md) — 设置窗口与配置文件参考

## 许可证

Apache License 2.0. Copyright (c) 2026 CxxIME Contributors.

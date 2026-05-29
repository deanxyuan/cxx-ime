# CxxIME

轻量级 Windows TSF 输入法（拼音 / 五笔）

A lightweight Windows TSF (Text Services Framework) input method (Pinyin + Wubi).

## 项目简介

CxxIME 是一个基于 Windows TSF (Text Services Framework) 的输入法，支持拼音和五笔两种模式，采用客户端/服务端架构设计。TSF DLL 负责捕获按键并通过 IPC 与后台服务端通信，服务端执行拼音解析、词典查询和候选生成。

## 架构

```
cxx-ime/
├── shared/          共享类型、IPC 协议、日志工具
├── engine/          拼音处理：分词器、翻译器、词典
├── ipc/             命名管道 IPC 客户端/服务端
├── server/          后台服务进程（系统托盘）
├── tsf/             TSF 文本服务 DLL（由 Windows 加载）
├── ui/              候选窗口（D2D / GDI 双后端渲染）
├── docs/            设计文档
├── data/            词典文件、Python 工具和默认配置
├── tools/           开发工具（dict_query, sqlite_query, ipc_tool）
├── test/            测试套件
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
- `cxxime_tsf.dll` — TSF 文本服务 DLL
- `cxxime-server.exe` — 后台服务进程
- `cxxime-settings.exe` — 配置编辑器
- `test/` — 测试可执行文件（每个测试文件一个 exe）

## 获取词典

CxxIME 使用 SQLite 格式的词典作为**源数据**，运行时通过 `build_binary.py` 转换为二进制格式（一次性读入内存）。

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
| `wubi86.dict.bin` | `build_binary.py` 生成 | 否 | 五笔二进制词典（运行时） |

### 词典维护

数据来源链路：**zip → db → algebra → bin/idx/spellings**

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

> **注意：** `.db`、`.bin`、`.idx`、`.spellings.bin` 均在 `.gitignore` 中。仅 `.db.zip` 提交到仓库。其他开发者拉取后从步骤 0 开始。

## 打包

构建 + 词典转换 + NSIS 安装程序编译：

```cmd
scripts\package.bat              # Release 打包 → cxxime-v0.1.0-setup.exe
scripts\package.bat debug        # Debug 打包
```

需要预先安装 [NSIS 3.x](https://nsis.sourceforge.io/) 并确保 `makensis.exe` 在 PATH 中。如果未安装 NSIS，`package.bat` 会跳过安装程序生成，`dist/` 目录中保留原始分发文件。

`package.bat` 执行流程：构建 → 词典转换（`.db` → `.bin`）→ NSIS 编译 → 输出单文件安装程序。

## 安装

运行 `cxxime-v0.1.0-setup.exe`，按向导提示操作：

1. 选择安装模式：用户目录（默认 `%USERPROFILE%\cxxime\`）或程序目录
2. 程序目录模式下可自定义安装路径，数据文件将放在安装目录下的 `data\` 子目录
3. 安装程序自动注册 TSF DLL、配置自启动、创建开始菜单快捷方式
4. 安装完成后**注销并重新登录**即可使用

## 卸载

- 开始菜单 → CxxIME → 卸载 CxxIME
- 或控制面板 → 添加/删除程序 → CxxIME

## 配置

编辑安装目录下的 `data/default.json`：

```json
{
    "engine": {
        "page_size": 9
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

## 开发工具

构建后在 `build/tools/<name>/Debug/` 下：

| 工具 | 用途 |
|------|------|
| `dict_query` | 拼音/五笔查词（`--mode pinyin\|wubi`，binary 词典） |
| `sqlite_query` | 直读 `.db` 文件调试 |
| `ipc_tool` | IPC 交互测试（connect / key / bench / stress 等） |
| `candidate_window_tool` | 候选窗口可视化测试（主题/布局/D2D/preedit 切换） |
| `tsf_position_tool` | 候选窗口定位测试（光标移动、屏幕边缘 clamp、多显示器适配） |

## 测试

开发构建需要指定数据目录：

```cmd
cmake .. -DCXXIME_PRODUCTION_BUILD=OFF
```

测试：

```cmd
cd build
ctest -C Debug
```

或单独运行某个测试：`build\test\Debug\ipc_test.exe`

## 模块状态

| 模块 | 状态 | 说明 |
|------|------|------|
| 词典 | ✅ 就绪 | 二进制堆加载词典、Patricia trie 拼写索引、音节 ID 索引、拼音/五笔双模式 |
| Engine | ✅ 就绪 | Syllabifier + Segmentor + Translator，缩写/模糊音/五笔简码 |
| IPC | ✅ 就绪 | IOCP 高性能命名管道，< 1ms 延迟，多客户端并发 |
| 会话管理 | ✅ 就绪 | 共享资源预加载，session 瞬时创建 |
| 用户词典 | ✅ 就绪 | 内存数据结构 + TSV 持久化，shared_mutex 并发读写 |
| TSF DLL | ✅ 就绪 | 按键捕获、编辑会话、候选上屏、候选窗口定位（GetTextExt 四层降级链）、DisplayAttributeProvider |
| 候选窗口 | ✅ 就绪 | D2D 渲染（默认），可切换 GDI，14 套配色，DPI 缩放，屏幕边缘 clamp，圆角窗口 |
| 安装部署 | ✅ 就绪 | NSIS 安装程序，支持用户目录/程序目录两种模式，开始菜单快捷方式，控制面板卸载 |
| 配置编辑器 | ✅ 就绪 | Win32 原生 GUI，左侧导航 + 右侧面板，支持所有配置项 |
| 配置系统 | ✅ 就绪 | JSON 配置（page_size, font, theme, layout spacing/padding） |

> 单元测试：64+ 个用例，10 个独立 exe，`ctest -C Debug` 全部通过。

## 许可证

Apache License 2.0. Copyright (c) 2026 CxxIME Contributors.

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
├── ui/              候选窗口（GDI 绘制）
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
- `test/` — 测试可执行文件（每个测试文件一个 exe）

## 获取词典

CxxIME 使用 SQLite 格式的拼音词典作为**源数据**，运行时通过 `build_binary.py` 转换为内存映射（mmap）二进制格式。可以从 [rime-ice](https://github.com/iDvel/rime-ice)（雾凇拼音）自动下载：

```cmd
cd data
python tools/fetch_dict.py
```

这会下载约 190 万词条的拼音词典（约 90 MB），保存为 `data/pinyin.dict.db`。

也可以手动使用 `data/tools/dict_convert.py` 转换 RIME 格式词典：

```cmd
python tools/dict_convert.py input.yaml output.db
```

### 词典数据文件

| 文件 | 来源 | 是否提交 | 说明 |
|------|------|----------|------|
| `pinyin.dict.db.zip` | `fetch_dict.py` 下载后压缩 | **是** | **提交到仓库的分发副本** |
| `pinyin.dict.db` | 解压 `.zip` 得到 | 否（`.gitignore`） | SQLite 源词典 |
| `pinyin.dict.bin` | `build_binary.py` 生成 | 否 | 二进制 mmap 词典（运行时） |
| `pinyin.spellings.bin` | `build_binary.py` 生成 | 否 | Patricia trie 拼写索引（运行时） |
| `pinyin.dict.idx` | `build_binary.py` 生成 | 否 | 音节 ID 索引（运行时） |
| `wubi86.dict.db` | `fetch_wubi.py` 下载 | 否 | Wubi86 SQLite 源词典 |

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

将构建产物、词典、脚本收集到发布目录：

```cmd
package.bat
```

生成 `dist/` 目录，包含二进制文件、数据文件和安装脚本。如有 PowerShell 还会创建 zip 压缩包。

## 安装

以管理员身份运行（双击或命令行）：

```cmd
install.bat                         # 默认安装到 %ProgramFiles%\CxxIME
install.bat "D:\MyPath\CxxIME"      # 自定义安装目录
```

安装程序会：
1. 停止正在运行的服务端
2. 注销已有的 TSF DLL（升级时）
3. 复制文件到安装目录
4. 通过 `regsvr32` 注册 TSF DLL
5. 配置开机自启动
6. 启动服务端

安装完成后需要注销并重新登录（或重启），输入法才会出现在系统输入法列表中。

也提供 PowerShell 安装脚本（可选）：`install.ps1`

## 卸载

以管理员身份运行：

```cmd
uninstall.bat                       # 从默认位置卸载
uninstall.bat "D:\MyPath\CxxIME"    # 自定义路径
```

卸载程序会：
1. 停止服务端
2. 注销 TSF DLL
3. 移除开机自启动项
4. 清理 TSF 注册表项
5. 删除已安装文件

也提供 PowerShell 卸载脚本（可选）：`uninstall.ps1`

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
        "layout": "horizontal"
    },
    "theme": "light"
}
```

## 开发工具

构建后在 `build/tools/<name>/Debug/` 下：

| 工具 | 用途 |
|------|------|
| `dict_query` | 拼音/五笔查词（`--mode pinyin\|wubi`，binary 词典） |
| `sqlite_query` | 直读 `.db` 文件调试 |
| `ipc_tool` | IPC 交互测试（connect / key / bench / stress 等） |

## 测试

```cmd
cd build
ctest -C Debug
```

或单独运行某个测试：`build\test\Debug\ipc_test.exe`

## 项目状态

当前已完成：
- TSF 文本服务框架集成
- 拼音/五笔处理引擎（Syllabifier + Segmentor + Translator）
- IOCP 高性能 IPC（< 1ms 延迟，支持多客户端并发）
- 多会话管理（共享 Dict/SpellingsIndex/Config，session 瞬时创建）
- 候选窗口（GDI 渲染）
- 内存用户词典（TSV 持久化，shared_mutex 并发读写）
- 拼音缩写/模糊音 + 五笔简码支持
- 词典下载与二进制构建工具
- 安装/卸载/打包脚本
- 单元测试（87 个，8 个独立 exe）

## 许可证

Apache License 2.0. Copyright (c) 2026 CxxIME Contributors.

# 安装与卸载

## 目录

- [构建与打包](#构建与打包)
- [安装](#安装)
- [卸载](#卸载)
- [安装模式](#安装模式)
- [数据目录结构](#数据目录结构)
- [命令行参数](#命令行参数)
- [配置编辑器](#配置编辑器)
- [诊断包](#诊断包)
- [常见问题](#常见问题)

## 构建与打包

### 开发构建

```cmd
build.bat debug          # Debug 构建（自动设置 CXXIME_PRODUCTION_BUILD=OFF）
build.bat                # Release 构建
build.bat clean          # 清理构建目录
```

`build.bat` 自动传入 `-DCXXIME_PRODUCTION_BUILD=OFF`，`data_dir()` 返回源码目录下的 `data/`，可直接运行测试和开发联调。

### 打包分发

```cmd
scripts\package.py                    # Release 打包
scripts\package.py --debug            # Debug 打包
scripts\package.py --fast --skip-dict # 快速复用已有 dist\data 词典打包
```

需要预先安装 [NSIS 3.x](https://nsis.sourceforge.io/)。`package.py` 执行：

1. 检查并触发构建（如未构建）
2. 复制 `cxxime_tsf_x64.dll`、`cxxime_tsf_x86.dll`、`cxxime-resources.dll`、`cxxime-server.exe`、`cxxime-settings.exe`、`collect_diagnostics.ps1`
3. 复制 `default.json`、`themes.json`、`settings_presets.json`、`punctuation.json`
4. 调用 `prepare_dict.py` 将 `.db` 词典转换为 `.bin` / `.idx` / `.spellings.bin`
5. 校验发布数据文件、CRT 依赖和热路径日志
5. 调用 `makensis.exe` 编译 NSIS 安装脚本
6. 输出 `..\output\cxxime-v<version>-setup.exe`

安装包文件名中的 `version` 取自仓库根目录的 `VERSION` 文件。

`package.py` 默认使用独立的 `build-package\` 构建目录，避免和 `build.bat` 使用的开发构建目录互相污染。 CMake 生成器默认交给 CMake 和当前命令行环境决定; 如需要显示指定，可使用：

```cmd
python scripts\package.py --generator "Visual Studio 17 2022" --platform x64
```

## 安装

运行 `cxxime-v<version>-setup.exe`，按向导操作：

1. 许可协议
2. 选择程序安装目录，默认 `C:\Program Files\CxxIME`
3. 停止已有 `cxxime-server.exe` 进程
4. 反注册已有旧版 TSF DLL（x86 和 x64）
5. 删除旧版 TSF DLL 及 `.old` 残留
6. 复制程序文件：
   - `cxxime_tsf_x64.dll`、`cxxime_tsf_x86.dll`、`cxxime-resources.dll`
   - `cxxime-server.exe`、`cxxime-settings.exe`、`collect_diagnostics.ps1`
7. 复制出厂数据（配置文件、主题、标点、二进制词典、清单）至 `<安装目录>\data\`
8. 创建用户配置目录 `%USERPROFILE%\cxxime\`，首次安装时复制默认配置
9. 注册 TSF DLL（x64: Sysnative regsvr32，x86: SysDir regsvr32）
10. 写入注册表自启动项 `Run\CxxIMEServer`
11. 写入卸载注册表项（`DisplayIcon` 引用 `cxxime-resources.dll`）
12. 创建开始菜单快捷方式（设置、诊断收集、卸载）
13. 启动 `cxxime-server.exe`
14. 可选：启动 `cxxime-settings.exe`

安装完成后**注销并重新登录**，输入法出现在系统输入法列表中。按 `Ctrl+Space` 或 `Win+Space` 切换。

## 卸载

- **推荐：** 开始菜单 → CxxIME → 卸载 CxxIME
- 或控制面板 → 添加/删除程序 → CxxIME

卸载过程如下：

1. 停止 `cxxime-server.exe`
2. 将系统键盘布局切换为英文（00000409），通过 `SendMessageTimeout` 通知 TSF 从所有进程卸载 CxxIME
3. 等待 3 秒确保 TSF 通知完成
4. 反注册 TSF DLL（x86: SysDir regsvr32 /u，x64: Sysnative regsvr32 /u）
5. 移除注册表项：`Run\CxxIMEServer`、Uninstall、CLSID、`CTF\TIP`
6. 删除开始菜单快捷方式
7. 删除程序文件（TSF DLL、可执行文件、数据文件等）。若 DLL 仍被占用，调度重启删除（`/REBOOTOK`）
8. **可选清除用户数据**：卸载向导中会出现"用户数据清理"页面（`un.UserDataPage`），默认保留。勾选"删除用户配置和词典数据"复选框并指定路径后，会删除 `%USERPROFILE%\cxxime\` 及其内容

卸载后如果安装目录暂时残留，重启后应自动清理。

> **注意**：卸载完成后建议注销重新登录。如果 TSF DLL 被占用，重启后才能完成清理。

## 诊断包

设置窗口"关于"页提供"导出诊断包"按钮，开始菜单也提供 "CCxxIME → Collect Diagnostics" 入口。诊断导出不会修改系统状态，默认收集：

- 版本、系统、PowerShell、当前用户等环境信息
- 安装目录、出厂数据目录、用户目录、日志目录
- 关键程序文件和数据文件的大小、时间戳、SHA256
- 日志文件清单和 trace-summary.txt 近期错误/慢路径摘要 
- CxxIME注册表卸载项、TIP注册项、键盘预加载状态
- `cxxime-server.exe`、`cxxime-settings.exe` 和加载 `cxxime tsf.dll` 的进程信息

默认不会复制日志、用户配置或用户词典。需要进一步排查时,可在安装目录运行:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File collect_diagnostics.ps1 -IncludeLogs
powershell -NoProfile -ExecutionPolicy Bypass -File collect_diagnostics.ps1 -IncludeUserConfig -IncludeUserDict
```

注意:日志可能包含输入编码,用户词典包含个人词条。对外反馈问题前应确认是否可以附带这些内容。

## 安装模式

当前 NSIS 安装器使用固定的程序目录安装模式：

| 类型 | 位置 | 说明 |
|---|---|---|
| 程序文件 | `C:\Program Files\CxxIME\`，可在安装向导中修改 | `cxxime-server.exe`、`cxxime-settings.exe`、`cxxime_tsf_x64.dll`、`cxxime_tsf_x86.dll`、`cxxime-resources.dll` |
| 出厂数据 | `<安装目录>\data\` | 出厂配置、主题、标点、二进制词典及清单 |
| 用户数据 | `%USERPROFILE%\cxxime\` | 用户配置、主题覆盖、标点覆盖和用户词典 |

用户数据目录由安装器初始化，后续覆盖安装不会覆盖已有用户配置。

## 数据目录结构

### 程序安装目录

```
<安装目录>\
├── cxxime_tsf_x64.dll
├── cxxime_tsf_x86.dll
├── cxxime-resources.dll
├── cxxime-server.exe
├── cxxime-settings.exe
├── collect_diagnostics.ps1
├── uninstall.exe
└── data\
    ├── default.json
    ├── dictionary_manifest.json
    ├── settings_presets.json
    ├── themes.json
    ├── punctuation.json
    ├── pinyin.dict.bin
    ├── pinyin.dict.idx
    ├── pinyin.spellings.bin
    ├── pinyin.topn.bin
    ├── wubi86.dict.bin
    └── wubi86.dict.idx
```

### 用户数据目录

```
%USERPROFILE%\cxxime\
├── default.json
├── themes.json
├── punctuation.json
└── user.tsv                 (自动生成)
```

## 命令行参数

### cxxime-server.exe

```
cxxime-server.exe --data "D:\MyData\CxxIME"    # 指定数据目录
cxxime-server.exe --dict "D:\dict\pinyin.dict.bin"   # 指定词典路径
cxxime-server.exe --config "D:\config.json"          # 指定配置文件
```

| 参数 | 说明 |
|------|------|
| `--data <dir>` | 数据根目录，覆盖默认路径 |
| `--dict <path>` | 词典文件完整路径（`.bin` 格式） |
| `--config <path>` | 配置文件完整路径 |

## 配置编辑器

运行开始菜单中的 "CxxIME 设置" 或直接启动 `cxxime-settings.exe`。

编辑器左侧导航分为四组：外观、候选窗口、输入、高级。修改配置后点击"保存"，编辑器会提示是否重启服务端以使配置生效。

## 常见问题

### 安装后输入法列表里找不到 CxxIME

1. 确认已注销并重新登录
2. 检查 `regsvr32` 是否成功：手动运行以下命令注册 x64 和 x86 两个架构的 DLL：
   ```cmd
   regsvr32 "C:\Program Files\CxxIME\cxxime_tsf_x64.dll"
   regsvr32 "C:\Program Files\CxxIME\cxxime_tsf_x86.dll"
   ```
3. 在"设置 → 时间和语言 → 语言和区域 → 中文(简体)"中添加输入法

### 服务端启动后立即退出

通常是词典文件缺失。检查 `C:\Program Files\CxxIME\data\pinyin.dict.bin` 是否存在。若缺失，重新运行 `scripts\package.py` 生成二进制词典后重新安装。

### 切换输入法后打字无反应

使用 [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) 查看日志输出（Debug 构建）。Release 构建不输出日志。

### 覆盖安装后配置丢失

覆盖安装不会删除 `user.tsv`（用户词典）。若 `default.json` 被覆盖，可通过配置编辑器重新修改。

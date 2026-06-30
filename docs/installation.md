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
scripts\package.py                    # Release 打包 → ..\output\cxxime-v0.1.0-setup.exe
scripts\package.py --debug            # Debug 打包
scripts\package.py --fast --skip-dict # 快速复用已有 dist\data 词典打包
```

需要预先安装 [NSIS 3.x](https://nsis.sourceforge.io/)。`package.py` 执行：

1. 检查并触发构建（如未构建）
2. 复制 `cxxime_tsf.dll`、`cxxime-server.exe`、`cxxime-settings.exe`
3. 复制 `default.json`、`themes.json`、`settings_presets.json`、`punctuation.json`
4. 调用 `prepare_dict.py` 将 `.db` 词典转换为 `.bin` / `.idx` / `.spellings.bin`
5. 校验发布数据文件、CRT 依赖和热路径日志
5. 调用 `makensis.exe` 编译 NSIS 安装脚本
6. 输出 `..\output\cxxime-v0.1.0-setup.exe`

`package.py` 默认使用独立的 `build-package\` 构建目录，避免和 `build.bat` 使用的开发构建目录互相污染。 CMake 生成器默认交给 CMake 和当前命令行环境决定; 如需要显示指定，可使用：

```cmd
python scripts\package.py --generator "Visual Studio 17 2022" --platform x64
```

## 安装

运行 `cxxime-v0.1.0-setup.exe`，按向导操作：

1. 许可协议
2. 选择程序安装目录，默认 `C:\Program Files\CxxIME`
3. 安装程序文件和出厂数据
4. 初始化用户配置目录 `%USERPROFILE%\cxxime\`
5. 注册 TSF DLL → 配置自启动 → 创建开始菜单快捷方式 → 启动服务端

安装完成后**注销并重新登录**，输入法出现在系统输入法列表中。按 `Ctrl+Space` 或 `Win+Space` 切换。

### 备用方式：批处理脚本

如果无法使用 NSIS 安装程序，`dist/` 目录中保留了 `.bat` / `.ps1` 安装脚本：

```cmd
install.bat                         # 安装到 %USERPROFILE%\cxxime\
install.bat "D:\MyData\CxxIME"      # 自定义数据目录
```

## 卸载

- **推荐：** 开始菜单 → CxxIME → 卸载 CxxIME
- 或控制面板 → 添加/删除程序 → CxxIME

卸载程序自动：停止服务端 → 反注册 TSF DLL → 移除自启动 → 清理注册表 → 删除文件。

默认卸载只删除程序文件、开始菜单快捷方式、TSF 注册项、自启动项和卸载项。用户目录
`%USERPROFILE%\cxxime\` 下的配置和用户词典会保留，便于重新安装或升级后继续使用。

如果 `cxxime_tsf.dll` 仍被 TSF 或宿主进程占用，卸载程序会把 DLL 删除安排到系统
重启时执行。卸载后如果安装目录暂时残留，重启后应自动清理。

### 备用方式

```cmd
uninstall.bat                       # 从默认位置卸载
uninstall.bat "D:\MyData\CxxIME"    # 自定义路径
```

卸载后建议注销重新登录; 如果安装目录内有被占用的 DLL 残留，重启后再检查目录。

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
powershell -NoProfile -ExecutionPolicy Bypass -File colllect_diagnostics.ps1 -IncludeLogs
powershell -NoProfile -ExecutionPolicy Bypass -File colllect_diagnostics.ps1 -IncludeUserConfig -IncludeUserDict
```

注意:日志可能包含输入编码,用户词典包含个人词条。对外反馈问题前应确认是否可以附带这些内容。

## 安装模式

当前 NSIS 安装器使用固定的程序目录安装模式：

| 类型 | 位置 | 说明 |
|---|---|---|
| 程序文件 | `C:\Program Files\CxxIME\`，可在安装向导中修改 | `cxxime-server.exe`、`cxxime-settings.exe`、`cxxime tsf.dll` |
| 出厂数据 | `<安装目录>\data\` | 出厂配置、主题、标点和二进制词典 |
| 用户数据 | `%USERPROFILE%\cxxime\` | 用户配置、主题覆盖、标点覆盖和用户词典 |

用户数据目录由安装器初始化，后续覆盖安装不会覆盖已有用户配置。

## 数据目录结构

### 程序安装目录

```
<安装目录>\
├── cxxime_tsf.dll
├── cxxime-server.exe
├── cxxime-settings.exe
├── uninstall.exe
└── data\
    ├── default.json
    ├── settings_presets.json
    ├── themes.json
    ├── punctuation.json
    ├── pinyin.dict.bin
    ├── pinyin.dict.idx
    ├── pinyin.spellings.bin
    ├── pinyin.topn.bin      (可选)
    ├── wubi86.dict.bin      (可选)
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
2. 检查 `regsvr32` 是否成功：手动运行 `regsvr32 "%USERPROFILE%\cxxime\cxxime_tsf.dll"`
3. 在"设置 → 时间和语言 → 语言和区域 → 中文(简体)"中添加输入法

### 服务端启动后立即退出

通常是词典文件缺失。检查 `%USERPROFILE%\cxxime%\pinyin.dict.bin` 是否存在。若缺失，重新运行 `package.bat` 生成二进制词典后重新安装。

### 切换输入法后打字无反应

使用 [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) 查看日志输出（Debug 构建）。Release 构建不输出日志。

### 覆盖安装后配置丢失

覆盖安装不会删除 `user.tsv`（用户词典）。若 `default.json` 被覆盖，可通过配置编辑器重新修改。

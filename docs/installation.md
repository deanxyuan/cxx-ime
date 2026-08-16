# 安装与卸载

## 构建与打包

### 环境要求

- Windows 10/11
- Visual Studio 2022（或 Build Tools），需要 C++ 桌面开发工作负载
- CMake 3.15+
- Python 3.10+（词典生成、测试和打包时需要；运行安装包不需要）

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
scripts\package.py --host-diag        # 宿主诊断包
```

安装包输出到 `..\output\cxxime-v<version>-setup.exe`；诊断包文件名包含
`-host-diag` 后缀。`<version>` 取自仓库根目录的 `VERSION`。

默认包关闭宿主诊断，不包含 IME Host Probe、宿主跟踪导出脚本及其快捷方式。
`--host-diag` 同时启用产品内宿主诊断并打包这些工具。两种包都保留通用的
`collect_diagnostics.ps1`。

需要预先安装 [NSIS 3.x](https://nsis.sourceforge.io/)。`package.py` 执行：

1. 检查并触发构建（如未构建）
2. 复制 `cxxime_tsf_x64.dll`、`cxxime_tsf_x86.dll`、`cxxime-resources.dll`、`cxxime-server.exe`、`cxxime-settings.exe`、`collect_diagnostics.ps1`
3. 复制 `default.json`、`themes.json`、`settings_presets.json`、`punctuation.json`
4. 调用 `prepare_dictionary_bundle.py` 准备运行时词典（`.bin` / `.idx` / `.spellings.bin` / `.topn.bin`）
5. 校验发布数据文件、CRT 依赖和热路径日志
5. 调用 `makensis.exe` 编译 NSIS 安装脚本
6. 输出 `..\output\cxxime-v<version>-setup.exe`

`package.py` 默认使用独立的 `build-package\` 构建目录，避免和 `build.bat` 使用的开发构建目录互相污染。 CMake 生成器默认交给 CMake 和当前命令行环境决定; 如需要显示指定，可使用：

```cmd
python scripts\package.py --generator "Visual Studio 17 2022" --platform x64
```

## 安装

运行 `cxxime-v<version>-setup.exe`，按向导操作：

1. 许可协议
2. 选择程序安装目录，默认 `C:\Program Files\CxxIME`
3. 检查占用旧版 CxxIME 文件的应用
4. 将新程序和出厂数据解压到同卷暂存目录
5. 备份旧版本并切换到新版本
6. 注册 TSF、复制 IMM 兼容模块并写入安装信息
7. 初始化用户配置目录 `%USERPROFILE\cxxime\%`，创建快捷方式并启动服务端

安装器使用同卷的暂存目录和备份目录完成升级。切换程序目录前，安装器会将旧程序状态、
64 位和 32 位 TSF 模块的实际注册状态、系统 IMM 模块和安装注册表状态写入持久事务文件，
不会根据 DLL 是否存在推断 TSF 是否已注册。TSF 注册、系统 IMM 模块复制或安装信息写入失败时，
会按事务文件恢复原状态；安装提交成功后才删除事务数据和旧版本。
如果安装进程异常终止，下次运行安装器会先停止服务端、检查当前目录和备份目录的文件占用，
再恢复未提交的安装或清理已经提交的备份。恢复未完成时不会继续覆盖文件。

覆盖安装会沿用注册表记录的安装目录。安装程序不会覆盖
`%USERPROFILE%\cxxime\default.json` 和用户数据（用户词库与选词偏好）。

如果有应用正在使用 CxxIME，安装器会列出 Restart Manager 检测到的进程，要求关闭后
重试。仅切换到其他输入法不能保证 TSF DLL 已从宿主进程卸载；Windows 系统进程仍占用文件时，
需要注销或重启后再运行安装器。安装阶段不使用重启后延迟覆盖，避免新旧模块混装。

安装完成后**注销并重新登录**，输入法出现在系统输入法列表中。按 `Ctrl+Space` 或 `Win+Space` 切换。

## 卸载

- **推荐：** 开始菜单 → CxxIME → 卸载 CxxIME
- 或控制面板 → 添加/删除程序 → CxxIME

卸载程序自动：停止服务端 → 检查文件占用 → 创建卸载事务 → 反注册 TSF DLL →
删除系统 IMM 模块 → 暂存并删除程序文件 → 移除自启动和注册表。

默认卸载只删除程序文件、开始菜单快捷方式、TSF 注册项、自启动项和卸载项。用户目录
`%USERPROFILE%\cxxime\` 下的配置、用户词库与选词偏好会保留，便于重新安装或升级后继续使用。

卸载器只删除安装器拥有的文件；安装目录中无法识别的文件会保留。删除程序文件成功前
控制面板卸载项和 `uninstall.exe` 保持可用。卸载中断后可再次运行卸载器继续处理；删除程序文件
前发生错误时会按卸载前记录的 64 位和 32 位状态恢复 TSF 注册和系统 IMM 模块。
全部程序文件安全移入同卷暂存目录后，卸载事务进入提交阶段；此后即使卸载中断，再次运行也会
继续删除和清理注册表，不会尝试恢复已经永久删除的文件。

如果 TSF DLL 或其他程序文件仍被宿主进程占用，卸载器会列出相关应用并要求关闭后重试。
卸载不会修改当前用户的键盘布局预加载项，卸载后如果安装目录暂时残留，重启后应自动清理。

> **注意**：卸载完成后建议注销重新登录。如果 TSF DLL 被占用，重启后才能完成清理。

## 诊断包

设置窗口"关于"页提供"导出诊断包"按钮，开始菜单也提供 "CCxxIME → Collect Diagnostics" 入口。诊断导出不会修改系统状态，默认收集：

- 版本、系统、PowerShell、当前用户等环境信息
- 安装目录、出厂数据目录、用户目录、日志目录
- 关键程序文件和数据文件的大小、时间戳、SHA256
- 日志文件清单和 trace-summary.txt 近期错误/慢路径摘要 
- CxxIME注册表卸载项、TIP注册项、键盘预加载状态
- `cxxime-server.exe`、`cxxime-settings.exe` 和加载 `cxxime tsf.dll` 的进程信息

默认不会复制日志、用户配置或用户数据（词库与偏好）。需要进一步排查时,可在安装目录运行:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File collect_diagnostics.ps1 -IncludeLogs
powershell -NoProfile -ExecutionPolicy Bypass -File collect_diagnostics.ps1 -IncludeUserConfig -IncludeUserDict -IncludeCandidatePreferences
```

注意:日志可能包含输入编码,用户数据包含个人词条与选词记录。对外反馈问题前应确认是否可以附带这些内容。

## 安装模式

当前 NSIS 安装器使用固定的程序目录安装模式：

| 类型 | 位置 | 说明 |
|---|---|---|
| 程序文件 | `C:\Program Files\CxxIME\`，可在安装向导中修改 | `cxxime-server.exe`、`cxxime-settings.exe`、`cxxime_tsf_x64.dll`、`cxxime_tsf_x86.dll`、`cxxime-resources.dll` |
| 出厂数据 | `<安装目录>\data\` | 出厂配置、主题、标点、二进制词典及清单 |
| 用户数据 | `%USERPROFILE%\cxxime\` | 用户配置、主题覆盖、标点覆盖、用户词库与选词偏好 |

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
├── THIRD_PARTY_NOTICES.txt
├── licenses\
│   └── rime-ice-GPL-3.0.txt
├── uninstall.exe
└── data\
    ├── default.json
    ├── dictionary_manifest.json
    ├── settings_presets.json
    ├── themes.json
    ├── punctuation.json
    ├── symbols.json
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
├── user_pinyin.tsv           (自动生成)
├── user_wubi.tsv             (自动生成)
├── learning_pinyin.tsv       (自动生成)
└── learning_wubi.tsv         (自动生成)
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

覆盖安装不会删除用户词库与选词偏好文件。若 `default.json` 被覆盖，可通过配置编辑器重新修改。

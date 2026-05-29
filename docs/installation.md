# 安装与卸载

## 目录

- [构建与打包](#构建与打包)
- [安装](#安装)
- [卸载](#卸载)
- [安装模式](#安装模式)
- [数据目录结构](#数据目录结构)
- [命令行参数](#命令行参数)
- [配置编辑器](#配置编辑器)
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
scripts\package.bat              # Release 打包 → cxxime-v0.1.0-setup.exe
scripts\package.bat debug        # Debug 打包
```

需要预先安装 [NSIS 3.x](https://nsis.sourceforge.io/)。`package.bat` 执行：

1. 检查并触发构建（如未构建）
2. 复制 `cxxime_tsf.dll`、`cxxime-server.exe`、`cxxime-settings.exe`
3. 复制 `default.json`、`themes.json`
4. 调用 `build_binary.py` 将 `.db` 词典转换为 `.bin` / `.idx` / `.spellings.bin`
5. 调用 `makensis.exe` 编译 NSIS 安装脚本
6. 输出 `cxxime-v0.1.0-setup.exe`

## 安装

运行 `cxxime-v0.1.0-setup.exe`，按向导操作：

1. 许可协议
2. 选择安装模式（用户目录 / 程序目录）
3. 选择安装目录（程序目录模式）
4. 安装文件 → 注册 TSF DLL → 配置自启动 → 启动服务端

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

### 备用方式

```cmd
uninstall.bat                       # 从默认位置卸载
uninstall.bat "D:\MyData\CxxIME"    # 自定义路径
```

卸载后注销重新登录即可完全清除。

## 安装模式

| | 用户目录模式（默认） | 程序目录模式 |
|---|---|---|
| 安装位置 | `%USERPROFILE%\cxxime\` | 用户自定义（如 `D:\CxxIME\`） |
| 目录结构 | 所有文件平铺 | exe/dll 在根目录，数据在 `data\` 子目录 |
| 运行时检测 | 默认路径回落 | 自动检测 `data\` 子目录 |
| 适用场景 | 个人使用 | 多用户共享、U 盘便携 |

## 数据目录结构

### 用户目录模式（默认）

```
%USERPROFILE%\cxxime\
├── cxxime_tsf.dll
├── cxxime-server.exe
├── cxxime-settings.exe
├── default.json
├── themes.json
├── pinyin.dict.bin
├── pinyin.dict.idx
├── pinyin.spellings.bin
├── wubi86.dict.bin          (可选)
├── wubi86.dict.idx          (可选)
├── wubi86.spellings.bin     (可选)
└── user.tsv                 (自动生成)
```

### 程序目录模式

```
<安装目录>\
├── cxxime_tsf.dll
├── cxxime-server.exe
├── cxxime-settings.exe
└── data\
    ├── default.json
    ├── themes.json
    ├── pinyin.dict.bin
    ├── pinyin.dict.idx
    ├── pinyin.spellings.bin
    ├── wubi86.dict.bin      (可选)
    ├── wubi86.dict.idx      (可选)
    ├── wubi86.spellings.bin (可选)
    └── user.tsv             (自动生成)
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

# CxxIME 安装与卸载

## 目录

- [构建与打包](#构建与打包)
- [安装](#安装)
- [卸载](#卸载)
- [数据目录结构](#数据目录结构)
- [命令行参数](#命令行参数)
- [常见问题](#常见问题)

## 构建与打包

### 开发构建

```cmd
build.bat debug          # Debug 构建
build.bat                # Release 构建
build.bat clean          # 清理构建目录
```

开发构建默认 `CXXIME_PRODUCTION_BUILD=ON`。需要运行测试或开发联调时，手动指定 CMake 参数：

```cmd
cd build_debug
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCXXIME_PRODUCTION_BUILD=OFF
cmake --build . --config Debug
ctest -C Debug
```

`CXXIME_PRODUCTION_BUILD=OFF` 时，`data_dir()` 返回源码目录下的 `data/`，可直接使用仓库中的词典和配置文件。

### 打包分发

```cmd
package.bat              # Release 打包
package.bat debug        # Debug 打包
```

`package.bat` 执行以下步骤：
1. 检查构建产物是否存在，不存在则自动触发 `build.bat`
2. 复制 `cxxime_tsf.dll`、`cxxime-server.exe`
3. 复制 `default.json`、`themes.json`
4. 调用 `build_binary.py` 将 `.db` 词典转换为运行时 `.bin` / `.idx` / `.spellings.bin` 格式
5. 复制安装/卸载脚本
6. 生成 `cxxime-v0.1.0-win64.zip`

产物在 `dist/` 目录下。

## 安装

### 方式一：批处理（推荐）

右键 `install.bat` → **以管理员身份运行**。

```
install.bat                         # 安装到 %USERPROFILE%\cxxime\
install.bat "D:\MyData\CxxIME"      # 自定义数据目录
```

安装步骤：
1. 验证 `cxxime_tsf.dll` 和 `cxxime-server.exe` 存在
2. 停止正在运行的服务端
3. 反注册已有的 TSF DLL（升级时）
4. 复制所有文件到数据目录
5. 注册 TSF DLL（`regsvr32`）
6. 配置开机自启动（`HKLM\...\Run`）
7. 启动服务端

### 方式二：PowerShell

```powershell
powershell -ExecutionPolicy Bypass -File install.ps1
powershell -ExecutionPolicy Bypass -File install.ps1 -DataDir "D:\MyData\CxxIME" -Silent
```

### 安装后

安装完成后需要**注销并重新登录**（或重启），输入法才会出现在系统输入法列表中。之后按 `Ctrl+Space` 或 `Win+Space` 切换输入法即可使用。

## 卸载

### 方式一：批处理

右键 `uninstall.bat` → **以管理员身份运行**。

```
uninstall.bat                         # 从默认位置卸载
uninstall.bat "D:\MyData\CxxIME"      # 自定义路径
```

卸载步骤：
1. 停止服务端进程
2. 反注册 TSF DLL
3. 移除开机自启动项
4. 清理 CLSID 注册表（`HKEY_CLASSES_ROOT\CLSID\{...}`）
5. 清理 TSF TIP 注册表（`HKLM\SOFTWARE\Microsoft\CTF\TIP\{...}`）
6. 删除数据目录

### 方式二：PowerShell

```powershell
powershell -ExecutionPolicy Bypass -File uninstall.ps1
powershell -ExecutionPolicy Bypass -File uninstall.ps1 -DataDir "D:\MyData\CxxIME" -Silent
```

卸载后注销重新登录即可完全清除。

## 数据目录结构

安装后的目录结构（默认 `%USERPROFILE%\cxxime\`）：

```
%USERPROFILE%\cxxime\
├── cxxime_tsf.dll          TSF 文本服务 DLL
├── cxxime-server.exe       后台服务进程
├── default.json            配置文件
├── themes.json             配色方案（14 套）
├── pinyin.dict.bin         拼音词典（运行时）
├── pinyin.dict.idx         拼音音节索引（运行时）
├── pinyin.spellings.bin    拼音拼写索引（运行时）
├── pinyin.dict.db          拼音源词典（可选，供重建用）
├── wubi86.dict.bin         五笔词典（运行时, 可选）
├── wubi86.dict.idx         五笔音节索引（可选）
├── wubi86.spellings.bin    五笔拼写索引（可选）
├── wubi86.dict.db          五笔源词典（可选）
└── user.tsv                用户词典（自动生成）
```

## 命令行参数

### cxxime-server.exe

```
cxxime-server.exe                              # 使用默认数据目录
cxxime-server.exe --data "D:\MyData\CxxIME"    # 指定数据目录
cxxime-server.exe --dict "D:\dict\pinyin.dict.bin"   # 指定词典路径
cxxime-server.exe --config "D:\config.json"          # 指定配置文件
```

| 参数 | 说明 |
|------|------|
| `--data <dir>` | 数据根目录，覆盖默认的 `%USERPROFILE%\cxxime\` |
| `--dict <path>` | 词典文件完整路径（`.bin` 格式） |
| `--config <path>` | 配置文件完整路径 |

优先级：`--config` / `--dict` > `--data` > 默认路径。

## 常见问题

### 安装后输入法列表里找不到 CxxIME

1. 确认已注销并重新登录
2. 检查 `regsvr32` 是否成功：手动运行 `regsvr32 "%USERPROFILE%\cxxime\cxxime_tsf.dll"` 看是否有错误提示
3. 在"设置 → 时间和语言 → 语言和区域 → 中文(简体)"中添加输入法

### 服务端启动后立即退出

通常是词典文件缺失。检查 `%USERPROFILE%\cxxime%\pinyin.dict.bin` 是否存在。如果缺失，重新运行 `package.bat` 生成二进制词典后重新安装。

### 切换输入法后打字无反应

使用 [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) 查看日志输出（Debug 构建）。Release 构建不输出日志。

### 如何更新词典

1. 重新运行 `data\tools\fetch_dict.py` 获取最新 `.db`
2. 运行 `package.bat` 重新生成 `.bin`
3. 重新运行 `install.bat`（会覆盖旧文件）

# CxxIME

轻量级 Windows TSF 拼音输入法

A lightweight Windows TSF (Text Services Framework) pinyin input method.

## 项目简介

CxxIME 是一个基于 Windows TSF (Text Services Framework) 的拼音输入法，采用客户端/服务端架构设计。TSF DLL 负责捕获按键并通过 IPC 与后台服务端通信，服务端执行拼音解析、词典查询和候选生成。

## 架构

```
cxx-ime/
├── shared/          共享类型、IPC 协议、日志工具
├── engine/          拼音处理：分词器、翻译器、词典
├── ipc/             命名管道 IPC 客户端/服务端
├── server/          后台服务进程（系统托盘）
├── tsf/             TSF 文本服务 DLL（由 Windows 加载）
├── ui/              候选窗口（GDI 绘制）
├── data/            词典工具和默认配置
├── test/            测试套件
└── third_party/     sqlite3, nlohmann/json
```

**输入流程：** 按键 → TSF DLL → IPC → 服务端 → 引擎 → IPC → TSF DLL → 文字上屏

## 环境要求

- Windows 10/11
- Visual Studio 2022（或 Build Tools），需要 C++ 工作负载
- CMake 3.16+
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
- `cxxime-test.exe` — 测试套件

## 获取词典

CxxIME 使用 SQLite 格式的拼音词典。可以从 [rime-ice](https://github.com/iDvel/rime-ice)（雾凇拼音）自动下载：

```cmd
cd data
python tools/fetch_dict.py
```

这会下载约 190 万词条的拼音词典（约 90 MB），保存为 `data/pinyin.dict.db`。

也可以手动使用 `data/tools/dict_convert.py` 转换 RIME 格式词典：

```cmd
python tools/dict_convert.py input.yaml output.db
```

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

## 交互式安装

提供菜单式安装体验：

```cmd
setup.bat
```

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

## 测试

```cmd
cd build
ctest -C Release
```

或直接运行：`build\test\Release\cxxime-test.exe`

## 项目状态

当前已完成：
- TSF 文本服务框架集成
- 按键处理与拼音组合
- 命名管道 IPC 通信
- 多会话管理
- 候选窗口显示
- JSON 配置加载
- 词典下载与转换工具
- 安装/卸载/打包脚本
- 单元测试（17 个）

## 许可证

MIT License. Copyright (c) 2026 CxxIME Contributors.

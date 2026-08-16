# CxxIME

[English](README_EN.md) | **中文**

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-10%2F11-0078D6?style=flat-square&logo=windows&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.15%2B-064F8C?style=flat-square&logo=cmake&logoColor=white)
![Windows CI](https://img.shields.io/github/actions/workflow/status/deanxyuan/cxx-ime/windows-ci.yml?branch=master&label=Windows%20CI&style=flat-square)
![License](https://img.shields.io/github/license/deanxyuan/cxx-ime?style=flat-square)

> 轻量级 Windows TSF 输入法（拼音 / 五笔 / 混输）

CxxIME 是一款基于 Windows TSF（Text Services Framework）的轻量级输入法，支持拼音、五笔和拼音五笔混输三种模式。它采用客户端/服务端架构：TSF DLL 捕获按键并通过 IPC 与后台服务通信，服务端完成拼音解析、词典查询与候选生成。

## 特性

- 拼音、五笔 86 与混输模式，支持简拼、模糊拼音与五笔简码
- 候选按匹配质量分层排序，高频长词不会压过精确音节与接近完成的词
- 五笔四码唯一候选自动上屏，可在设置中关闭
- 支持应用宿主通过 TSF UIElement 接管 inline preedit 与候选窗口绘制（DOTA2 已验证）
- 12 套内置配色主题（6 种色系 × 浅色/深色），支持横向/纵向布局
- 选词学习默认关闭，开启后偏好独立持久化，可在设置中随时清除

## 界面预览

候选窗口主题预览（6 种色系 × 浅色/深色）：

| 色系 | 浅色 | 深色 |
|------|------|------|
| 月白 | ![月白浅](docs/images/themes/moon_light.png) | ![月白深](docs/images/themes/moon_dark.png) |
| 晴空 | ![晴空浅](docs/images/themes/sky_light.png) | ![晴空深](docs/images/themes/sky_dark.png) |
| 新翠 | ![新翠浅](docs/images/themes/jade_light.png) | ![新翠深](docs/images/themes/jade_dark.png) |
| 琥珀 | ![琥珀浅](docs/images/themes/amber_light.png) | ![琥珀深](docs/images/themes/amber_dark.png) |
| 珊瑚 | ![珊瑚浅](docs/images/themes/coral_light.png) | ![珊瑚深](docs/images/themes/coral_dark.png) |
| 鸢尾 | ![鸢尾浅](docs/images/themes/iris_light.png) | ![鸢尾深](docs/images/themes/iris_dark.png) |

## 安装

1. 运行安装程序 `cxxime-v<version>-setup.exe`，按向导完成安装
2. 安装完成后**注销并重新登录**
3. 通过 `Ctrl+Space` 或 `Win+Space` 切换到 CxxIME

安装、卸载与升级的详细说明见 [docs/installation.md](docs/installation.md)。

## 配置

- 通过开始菜单打开 **CxxIME Settings** 图形化配置
- 或直接编辑用户配置文件 `%USERPROFILE%\cxxime\default.json`

所有配置项（输入模式、候选窗口、主题、词库管理、快捷键等）见 [docs/settings-guide.md](docs/settings-guide.md)。

## 词典

CxxIME 内置拼音与五笔 86 词库。拼音词典来自 [rime-ice](https://github.com/iDvel/rime-ice)（约 190 万词条，GPL-3.0-only），五笔词典来自 [KyleBing/rime-wubi86-jidian](https://github.com/KyleBing/rime-wubi86-jidian)（Apache-2.0）。词库来源与授权见 [data/README.md](data/README.md)，数据格式与生成维护流程见 [docs/dictionary.md](docs/dictionary.md)。

## 从源码构建（开发者）

```cmd
build.bat              # Release 构建
build.bat debug        # Debug 构建（含测试与工具）
```

环境要求：Windows 10/11、Visual Studio 2022（C++ 工作负载）、CMake 3.15+、Python 3.10+（词典生成与打包需要）。完整构建与打包说明见 [docs/installation.md](docs/installation.md)，架构说明见 [docs/architecture.md](docs/architecture.md)，开发工具见 [tools/README.md](tools/README.md)。

## 许可证

项目代码按 Apache License 2.0 发布。第三方组件与词典数据保留各自许可证，详见 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)。

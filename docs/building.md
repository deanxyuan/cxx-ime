# 从源码构建 CxxIME

本文面向需要在本地编译、运行测试或制作发布包的开发者。日常开发使用
`build.bat`；构建安装包使用 `scripts\package.py`。

以下命令默认在仓库根目录 `cxx-ime\` 中执行。

## 环境要求

- Windows 10/11。
- Visual Studio 2017 或更新版本，安装“使用 C++ 的桌面开发”工作负载和 Windows SDK。
  只安装 Visual Studio Build Tools 也可以。
- CMake 3.15 或更新版本。
- Python 3.10 或更新版本。`build.bat` 默认启用测试，而测试配置要求 Python；
  只编译程序时可以通过 CMake 关闭测试。
- 制作安装包时还需要 NSIS 3.x。仅编译或运行测试不需要 NSIS。

## 快速开始

在普通的 `cmd` 窗口中执行：

```cmd
build.bat
```

`build.bat` 默认执行 Release 构建，并把产物放在 `build\`。首次构建可能较慢；
后续构建会复用已有的 CMake 配置。运行测试前还需要生成测试词典，步骤见
[运行测试](#运行测试)。

需要调试版本时：

```cmd
build.bat debug
```

## build.bat 用法

| 命令 | 作用 |
| --- | --- |
| `build.bat` | Release 构建 |
| `build.bat debug` | Debug 构建 |
| `build.bat -j 8` | Release 构建，并行任务上限为 8 |
| `build.bat debug -j 8` | Debug 构建，并行任务上限为 8 |
| `build.bat clean` | 删除 `build\` 构建目录 |

`-j N` 中的 `N` 必须是正整数。指定后，它会设置
`CMAKE_BUILD_PARALLEL_LEVEL`；未指定时沿用该环境变量或底层构建工具的默认值。
并行任务越多不一定越快，内存不足时应降低 `N`。NMake 不支持并行构建。

`build.bat` 同时启用测试和开发工具，并把 Source 目录显式传给 CMake，因此可以从
其他工作目录调用。它不会切换当前命令行目录。

## 构建产物

Visual Studio 等多配置生成器使用以下布局：

```text
build\<模块>\<配置>\
```

例如 Release 构建的主要产物位于：

```text
build\server\Release\cxxime-server.exe
build\settings\Release\cxxime-settings.exe
build\resource\Release\cxxime-resources.dll
build\tsf\Release\cxxime_tsf_x64.dll
build\legacy_ime\Release\cxxime_ime_x64.ime
build\test\Release\
```

NMake、Ninja 等单配置生成器不使用额外的配置子目录：

```text
build\<模块>\
```

测试程序按测试源文件分别生成，不是单个 `cxxime-test.exe`。

## 运行测试

单元测试依赖 `data\` 下生成的运行时词典 bundle。因此，新构建目录在运行 CTest
前必须先生成测试词典；CI 也按这个顺序执行。

标准 Release 构建使用以下命令：

```cmd
python scripts\prepare_dictionary_bundle.py ^
    --data-dir data ^
    --output-dir data ^
    --workers 2 ^
    --topn-builder build\tools\topn_index\Release\topn_builder.exe

ctest --test-dir build -C Release --output-on-failure
```

Debug 构建需要把 `--topn-builder` 指向
`build\tools\topn_index\Debug\topn_builder.exe`，并把 CTest 参数改为
`-C Debug`。使用自定义 CMake 构建目录时，同时替换 `--topn-builder` 和
`--test-dir` 中的路径。

如果词典源数据没有变化，且当前构建目录已经成功生成过测试词典，可以跳过重复生成，
直接运行 `ctest`。

## 手动使用 CMake

`build.bat` 适合日常开发。需要指定生成器、控制 CMake 选项或使用独立构建目录时，
可以直接调用 CMake。

### Visual Studio

下面的示例使用 Visual Studio 2022；请按实际安装的版本替换生成器名称：

```cmd
cmake -S . -B build-vs2022 -G "Visual Studio 17 2022" -A x64 ^
    -DCXXIME_PRODUCTION_BUILD=OFF ^
    -DCXXIME_BUILD_TOOLS=ON ^
    -DCXXIME_BUILD_TESTS=ON

cmake --build build-vs2022 --config Release --parallel
```

如需测试，按[运行测试](#运行测试)中的步骤生成词典，并将 `--topn-builder` 指向
`build-vs2022\tools\topn_index\Release\topn_builder.exe`，将 `--test-dir`
改为 `build-vs2022`。

### NMake 或 Ninja

NMake 和 Ninja 使用当前命令行中的编译器。请先从 Visual Studio 的
`x64 Native Tools Command Prompt` 打开终端，再执行构建。

```cmd
cmake -S . -B build-ninja -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCXXIME_PRODUCTION_BUILD=OFF ^
    -DCXXIME_BUILD_TOOLS=ON ^
    -DCXXIME_BUILD_TESTS=ON

cmake --build build-ninja --parallel
```

使用 NMake 时，把 `-G Ninja` 替换成 `-G "NMake Makefiles"`，并把构建目录改为
`build-nmake`；构建命令可省略 `--parallel`。

如需测试，按[运行测试](#运行测试)中的步骤生成词典，并将 `--topn-builder` 指向
`build-ninja\tools\topn_index\topn_builder.exe` 或
`build-nmake\tools\topn_index\topn_builder.exe`，同时把 `--test-dir` 改为对应
构建目录。

常用 CMake 选项：

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `CXXIME_PRODUCTION_BUILD` | `ON` | 生产布局；开发构建通常设为 `OFF` |
| `CXXIME_BUILD_TOOLS` | `ON` | 生成开发工具 |
| `CXXIME_BUILD_TESTS` | `ON` | 生成测试；关闭后不需要 Python 生成测试目标 |
| `CXXIME_BUILD_HOST_PROBE` | `OFF` | 生成宿主诊断 Probe |
| `CXXIME_ENABLE_HOST_DIAGNOSTICS` | `OFF` | 启用宿主诊断插桩 |

## 选择工具链

默认情况下由 CMake 选择已安装的生成器和工具链。`build.bat` 会保留现有
`build\CMakeCache.txt` 中的选择；首次配置前可以通过标准 CMake 环境变量固定
生成器、平台、VS 实例或工具集：

```cmd
set "CMAKE_GENERATOR=Visual Studio 17 2022"
set "CMAKE_GENERATOR_PLATFORM=x64"
build.bat
```

多实例或特定工具集还可以使用 `CMAKE_GENERATOR_INSTANCE` 和
`CMAKE_GENERATOR_TOOLSET`。生成器名称必须与已安装的 Visual Studio 和 CMake
版本匹配。

CMake 不会把已有缓存自动切换到另一套工具链。要更换生成器、VS 版本或架构，
应先运行 `build.bat clean`，或者为手动 CMake 构建使用新的构建目录。

如果 CMake 不在 `PATH` 中，可设置 `CXXIME_CMAKE` 为 `cmake.exe` 的完整路径：

```cmd
set "CXXIME_CMAKE=C:\Tools\CMake\bin\cmake.exe"
build.bat
```

`CXXIME_CMAKE` 只接受可执行文件路径，不要附加其他参数。路径不可用时会直接报错，
不会自动改用其他 CMake 或 Visual Studio 安装。

## 构建发布包

发布包入口会构建程序、准备词典、校验打包内容并生成安装包：

```cmd
python scripts\package.py
```

默认使用 `build-package\` 和 `build-package-x86\` 分别构建 x64 与 x86 模块，
最终安装包默认输出到：

```text
..\output\cxxime-v<版本>-setup.exe
```

常用参数：

| 参数 | 作用 |
| --- | --- |
| `--output-dir <目录>` | 指定安装包输出目录 |
| `--fast` | 跳过大型文件的 NSIS 压缩 |
| `--skip-dict` | 复用现有 `dist\data\` 词典 |
| `--skip-build` | 复用已有构建产物 |
| `--host-diag` | 包含宿主诊断工具 |
| `--with-tests` | 同时构建测试目标 |

只检查本地打包目录、不生成 NSIS 安装包时：

```cmd
python scripts\package.py --skip-nsis --skip-x86-tsf
```

`--skip-x86-tsf` 必须与 `--skip-nsis` 同时使用。正式发布前不应在词典源数据或
词典生成逻辑变更后使用 `--skip-dict`。

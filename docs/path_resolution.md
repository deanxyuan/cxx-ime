# 路径解析

CxxIME 的数据文件分布在两个位置：安装目录（只读共享数据）和用户目录（可写用户数据）。

## 目录布局

### 安装目录 `%ProgramFiles%\CxxIME\`

```
C:\Program Files\CxxIME\
├── cxxime_tsf.dll
├── cxxime-server.exe
├── cxxime-settings.exe
└── data\
    ├── default.json          默认配置
    ├── themes.json           颜色主题（14 套）
    ├── pinyin.dict.bin       拼音二进制词典（运行时）
    ├── pinyin.dict.idx       音节索引（运行时）
    ├── pinyin.spellings.bin  拼写 trie（运行时）
    ├── pinyin.topn.bin       短码缓存（运行时）
    ├── wubi86.dict.bin       五笔词典（运行时）
    └── wubi86.dict.idx       五笔完整前缀索引（运行时）
```

### 用户目录 `%USERPROFILE%\cxxime\`

```
C:\Users\<username>\cxxime\
├── default.json              用户配置覆盖（可选）
├── themes.json               用户主题覆盖（可选）
├── user_pinyin.tsv           用户词库（拼音）
├── user_wubi.tsv             用户词库（五笔）
├── learning_pinyin.tsv       选词偏好（拼音）
└── learning_wubi.tsv         选词偏好（五笔）
```

由 `user_data_dir()` 首次调用时自动创建。

## 路径解析函数

声明在 `shared/include/cxxime/data_path.h`，实现在 `shared/src/data_path.cc`。

### data_dir()

共享数据目录，只读。回退链：

1. **运行时覆盖** — `set_data_dir()` 设置的路径（server 的 `--data` 参数）
2. **编译时常量** — `CXXIME_DATA_DIR` 宏（开发构建由 CMake 定义）
3. **生产默认** — `<exe_dir>\data\`（GetModuleFileNameW 推导）

```
server --data "D:\custom\data"  →  优先级 1
cmake -DCXXIME_PRODUCTION_BUILD=OFF  →  优先级 2
NSIS 安装到 Program Files  →  优先级 3
```

### user_data_dir()

用户可写目录。固定解析为 `%USERPROFILE%\cxxime\`（CSIDL_PROFILE + `\cxxime\`）。

首次调用时通过 `CreateDirectoryW` 自动创建。

### data_path() / user_data_path()

便捷拼接：

```cpp
cxxime::data_path("pinyin.dict.bin")       // → data_dir() + "pinyin.dict.bin"
cxxime::user_data_path("user_pinyin.tsv")  // → user_data_dir() + "user_pinyin.tsv"
```

### set_data_dir()

运行时覆盖 `data_dir()` 的返回值。自动补尾 `\`。

```cpp
cxxime::set_data_dir("D:\\mydata");   // data_dir() → "D:\\mydata\\"
cxxime::set_data_dir("");             // 清除覆盖，恢复默认回退链
```

## 配置加载顺序

server 和 settings 按以下顺序加载配置，后者覆盖前者：

```cpp
config.load(data_path("default.json"));         // 安装目录默认配置
config.load(user_data_path("default.json"));    // 用户目录覆盖
config.load_themes(data_path("themes.json"));   // 主题（仅从安装目录）
```

## CMake 宏

| 宏 | 定义位置 | 用途 |
|----|----------|------|
| `CXXIME_DATA_DIR` | 顶层 CMakeLists.txt（非 production）<br>test/CMakeLists.txt（测试） | `data_dir()` 编译时回退值 |
| `CXXIME_PROJECT_DIR` | test/CMakeLists.txt | 测试中拼接项目根路径 |

### 生产构建

```cmd
cmake -DCXXIME_PRODUCTION_BUILD=ON ...
```

不定义 `CXXIME_DATA_DIR`。`data_dir()` 走 `<exe_dir>\data\`。

### 开发构建

```cmd
cmake -DCXXIME_PRODUCTION_BUILD=OFF ...
```

定义 `CXXIME_DATA_DIR="${CMAKE_SOURCE_DIR}/data/"`。`data_dir()` 直接返回源码 data/ 目录。

### 测试

test/CMakeLists.txt 为每个测试目标定义：

```cmake
target_compile_definitions(${name} PRIVATE
    CXXIME_PROJECT_DIR="${CMAKE_SOURCE_DIR}/"
    CXXIME_DATA_DIR="${CMAKE_SOURCE_DIR}/data/"
)
```

测试代码用 `project_path()` 拼接项目根路径：

```cpp
static std::string project_path(const char* rel) {
    return std::string(CXXIME_PROJECT_DIR) + rel;
}
// project_path("data/pinyin.dict.bin")  → D:/gitee/cxx-ime/data/pinyin.dict.bin
// project_path("data/default.json")     → D:/gitee/cxx-ime/data/default.json
```

## Python 脚本路径

Python 脚本分布在两个目录，职责不同：

| 目录 | 定位 | 脚本 |
|------|------|------|
| `scripts/` | **主入口脚本**：打包、词典准备、校验、基准回归 | `package.py`、`prepare_dictionary_bundle.py`、`build_pinyin_topn.py`、`verify_dictionary_bundle.py`、`verify_package.py`、`check_query_bench.py` |
| `data/tools/` | **词典数据处理工具**：由 `scripts/` 入口调用，也可独立运行 | `fetch_pinyin_dictionary.py`、`fetch_wubi_dictionary.py`、`convert_rime_dictionary.py`、`build_runtime_dictionary.py`、`generate_pinyin_spellings.py`、`generate_pinyin_syllable_ids.py`、`split_wubi_symbols.py`，以及 `dict_builder/` 实现包 |

脚本通过 `--input`/`--output` 参数接收路径，不依赖环境变量。`scripts/package.py` 经 `scripts/prepare_dictionary_bundle.py` 调用 `data/tools/` 下的词典工具时传入绝对路径：

```python
# prepare_dictionary_bundle.py 内部
cmd = [sys.executable, os.path.join(DATA_TOOLS, "build_runtime_dictionary.py"),
       "--input", db_path, "--output", output_prefix]
```

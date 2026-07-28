# CxxIME 架构总览

描述 CxxIME 的总体架构、模块划分、技术选型与数据架构现状。面向维护者与开发者，专题细节见文末相关文档。

**关键指标（现状）：**

| 指标 | 数值 | 说明 |
|------|------|------|
| 安装包 | ~72 MB | 单文件 NSIS 安装器，含全部词典数据 |
| Server 常驻内存 | ~480 MB 量级 | 词典数据全量堆载（~415 MB）为主：dict.bin 72.8 + dict.idx 48.4 + topn.bin ~212 + wubi ~4.7 + spellings ~2.9 + darts trie + 用户词索引 |
| IPC 往返延迟 | < 1 ms | 实测 preedit avg ~50us（见 [IPC 架构设计](ipc-architecture.md)） |
| 启动 | 词典一次性读入 | 无 mmap 换页延迟，代价是启动时的顺序读盘 |

---

## 1. 项目定位

轻量级 Windows TSF 输入法：拼音 / 五笔 86 / 混输三种模式，客户端（TSF DLL）/ 服务端（后台进程）分离，仅支持 Windows 10+，仅实现 TSF 输入处理器（无 IMM32 兼容层）。

**设计原则：**

1. **轻量依赖** — 第三方库仅 nlohmann/json（header-only）；SQLite 仅构建时使用；无 Boost
2. **客户端/服务端分离** — TSF DLL 只做按键捕获与展示，引擎与词典集中在服务端
3. **模块化** — 引擎层与 UI 层完全解耦
4. **仅 TSF** — Windows 10+ 行为稳定，无需 IMM32 兜底

---

## 2. 总体架构

```
┌──────────────────────┐    Named Pipe (IOCP)    ┌───────────────────────────┐
│  TSF DLL (x64/x86)   │  ◄════════════════════► │     cxxime-server.exe     │
│  ┌────────────────┐  │                         │  ┌─────────────────────┐  │
│  │ KeyEventSink   │  │                         │  │ SharedResources     │  │
│  │ EditSession    │  │                         │  │ 词典/拼写/配置/标点   │  │
│  ├────────────────┤  │                         │  ├─────────────────────┤  │
│  │ IPC Client     │  │                         │  │ SessionManager      │  │
│  ├────────────────┤  │                         │  │ + GlobalVisibleState│  │
│  │ CandidateWindow│  │                         │  ├─────────────────────┤  │
│  │ StatusController│ │                         │  │ Engine (per session)│  │
│  │ LanguageBar    │  │                         │  ├─────────────────────┤  │
│  └────────────────┘  │                         │  │ Config/Dict Monitor │  │
└──────────────────────┘                         │  └─────────────────────┘  │
          ▲                                      └───────────────────────────┘
          │ 共享内存 + Event（配置变更通知）                    ▲
┌──────────────────────┐                                     │
│ cxxime-settings.exe  │─────────────────────────────────────┘
│ 配置编辑 / 用户词典管理 │              IPC（用户词典 CRUD、重载）
└──────────────────────┘
```

**输入流程：** 按键 → TSF DLL → IPC → Server（SessionManager → Engine）→ IPC → TSF DLL → 上屏/候选窗口

**状态流程：** 可见状态（中英文/Caps/全半角/标点/模式）为服务端全局状态，经 IPC（GET_STATUS / 心跳）同步给各 TSF 客户端，驱动状态窗口与语言栏。

---

## 3. 模块划分

### 3.1 Engine（输入引擎核心）

| 子模块 | 功能 | 实现 |
|--------|------|------|
| **Processor** | 拼音按键处理 | `PinyinProcessor` |
| **WubiProcessor** | 五笔按键处理（编码输入、Z 键通配、候选选择） | `WubiProcessor` |
| **Translator** | 拼音→汉字候选翻译（Syllabifier 主路径 + PinyinSegmentor 回退，长拼音查询页缓存） | `PinyinTranslator` |
| **WubiTranslator** | 五笔→汉字候选翻译（按编码精确/前缀查找） | `WubiTranslator` |
| **MixedTranslator** | 混合模式翻译（拼音+五笔交叉排序，三种排序策略） | `MixedTranslator` |
| **Segmentor** | 音节切分 | `Syllabifier`（BFS+DFS）+ `PinyinSegmentor`（回退） |
| **SpellingsIndex** | Patricia trie 拼写索引（缩写扩展，Prism 层） | `SpellingsIndex` |
| **AsciiComposer** | 可配置中英文切换，CapsLock overlay | `AsciiComposer` |
| **OutputComposer** | 输出合成（全角/CapsLock/按键拦截） | `OutputComposer` |
| **ShortCodeCache** | 短码候选缓存（DAT-16 Top-N 索引，Darts trie 查找，短输入快速路径） | `ShortCodeCache` |
| **Dict** | 词典加载与查询 | 二进制加载主词典 + 内存用户词典（多路索引） |
| **Config** | 配置加载 | JSON（nlohmann/json） |

**数据存储：**
- **主词典：** 二进制堆加载词典 + Patricia trie 拼写索引（一次性读入），详见 [词典系统设计](dictionary.md)
- **用户词典：** 内存多路索引（exact/prefix/abbr/mixed），TSV 文件持久化，详见 [用户词典设计](user-dictionary.md)

### 3.2 TSF DLL（输入法前端）

实现的 TSF 接口：

```
ITfTextInputProcessorEx     — 输入处理器激活/停用
ITfKeyEventSink             — 按键事件接收
ITfCompositionSink          — 组合输入管理
ITfEditSession              — 编辑会话回调
ITfDisplayAttributeProvider — 显示属性（下划线等）
ITfThreadFocusSink          — 线程焦点通知
```

**关键流程：**

```
1. Windows TSF 加载 DLL → DllGetClassObject → ITfTextInputProcessorEx
2. 应用获焦 → ActivateEx() → 创建 IPC 连接
3. 用户按键 → OnKeyDown() → IPC 发送到 Server → 返回候选
4. 用户选词 → select_candidate → IPC 提交 → 插入文本到应用
5. 应用失焦 → Deactivate() → 断开 IPC
```

**注册方式：** `regsvr32` 注册 COM DLL，在 `HKLM\SOFTWARE\Microsoft\CTF\TIP\` 下注册输入处理器；x64 与 x86 DLL 分别注册，系统按进程位数加载。

### 3.3 Server（后台服务进程）

| 功能 | 说明 |
|------|------|
| 共享资源 | 词典/拼写索引/配置/标点映射启动时加载一次，所有 session 共享 |
| 会话管理 | 创建/销毁输入会话，per-session Engine 引用共享资源 |
| 全局可见状态 | GlobalVisibleState 保证跨窗口中英文/模式等状态一致 |
| IPC 服务 | 命名管道监听（IOCP），处理请求/响应 |
| 热重载 | ConfigMonitor（共享内存 + Event）、DictionaryMonitor（manifest 轮询） |

### 3.4 UI（候选窗口 + 状态窗口）

- **渲染后端：** Direct2D + DirectWrite（默认），GDI 可选（`render_backend` 配置）
- **布局：** 横排（默认）/ 竖排，跟随光标定位，屏幕边缘 clamp，DPI 感知，圆角窗口
- **主题：** 14 套配色预设（`themes.json`，兼容 Weasel 配色格式）
- **状态窗口：** 中英文/大小写等状态显示，与语言栏图标联动

### 3.5 IPC 层

Named Pipe（每用户 `\\.\pipe\<username>\CxxIME`），Server 端 IOCP 线程池（2-4 worker），Client 端同步 I/O。固定结构体 + memcpy 序列化。

协议定义见 `shared/include/cxxime/ipc_protocol.h`（`IPCCommand` / `IPCRequest` / `IPCResponse` / `ImeStatus`），涵盖会话、按键、候选、状态切换、用户词典管理与重载命令。架构细节见 [IPC 架构设计](ipc-architecture.md)。

### 3.6 配置系统

JSON 配置（`default.json` + `themes.json`），Settings 编辑器（Win32 原生 GUI）修改后经共享内存 + Event 通知热重载。配置项与界面说明详见 [设置指南](settings-guide.md)，中英文切换配置详见 [中英文切换机制](ascii-composer.md)。

---

## 4. 技术选型

| 技术领域 | 选型 | 理由 |
|----------|------|------|
| 引擎 | 自研（C++17） | 按需实现拼音/五笔，无需完整输入法框架 |
| 输入处理器 | 仅 TSF | Windows 10+ 行为稳定，无需 IMM32 兜底 |
| 序列化 | 固定结构体 + memcpy | 简单高效 |
| IPC | Named Pipe + IOCP | 零外部依赖，< 1ms 往返 |
| 词典 | 二进制堆加载 + DAT-16 Top-N 索引 | 一次性读入，Darts trie O(k) 查找，运行时无 SQLite |
| 配置 | nlohmann/json | header-only，轻量 |
| UI 渲染 | Direct2D/DirectWrite（默认）+ GDI（可选） | 高质量渲染，双后端可配置 |
| 日志 | CXXIME_LOG（自研 OutputDebugString 宏） | 零依赖 |
| 安装 | NSIS | 成熟的 Windows 安装方案 |
| 运行库 | 静态 VC++ 运行时 | 无 vcredist 依赖 |

### 依赖清单

| 依赖 | 用途 | 获取方式 |
|------|------|----------|
| Windows SDK | TSF/COM/Direct2D/GDI | 系统自带 |
| SQLite3 | 构建工具、sqlite_query 工具 | 源码编译（amalgamation，FTS5 + JSON1） |
| Darts-clone | Top-N 索引键查找（Double Array Trie） | 源码编译（bundled in third_party/） |
| nlohmann/json | 配置解析 | 头文件 only |
| Python 3.6+ | 词典数据工具 | 可选（仅构建词典时需要） |

---

## 5. 项目目录结构

```
cxx-ime/
├── CMakeLists.txt
├── README.md
├── LICENSE                 # Apache 2.0
├── build.bat
│
├── shared/                 # 共享基础类型
│   ├── include/cxxime/     ipc_protocol.h, key_event.h, logging.h, candidate.h,
│   │                       query_trace.h, query_budget.h, config_monitor.h,
│   │                       config_notify.h, diagnostics_config.h, mpscq.h,
│   │                       data_path.h, dictionary_manifest.h, dictionary_monitor.h
│   └── src/                key_event.cc, query_trace_log.cc, mpscq.cc
│
├── engine/                 # 输入引擎
│   ├── include/cxxime/     engine.h, processor.h, translator.h, segmentor.h,
│   │                       dict.h, context.h, config.h, ascii_composer.h,
│   │                       spellings_index.h, syllabifier.h, query_scratch.h,
│   │                       topk_collector.h, short_code_cache.h, output_composer.h,
│   │                       output_options.h, wubi_processor.h, wubi_translator.h,
│   │                       mixed_translator.h, punct_types.h
│   └── src/                engine.cc, pinyin_processor.cc, pinyin_translator.cc,
│                           pinyin_segmentor.cc, wubi_processor.cc, wubi_translator.cc,
│                           mixed_translator.cc, output_composer.cc,
│                           short_code_cache.cc, dict.cc, config.cc, context.cc,
│                           spellings_index.cc, syllabifier.cc, ascii_composer.cc
│
├── ipc/                    # IPC 层
│   ├── include/cxxime/     ipc_client.h, ipc_server.h
│   └── src/                ipc_client.cc, ipc_server.cc
│
├── tsf/                    # TSF 输入法 DLL（x64/x86 双架构）
│   ├── src/                dllmain.cpp, class_factory.cpp, text_service.cpp,
│   │                       text_service_candidate.cpp, text_service_composition.cpp,
│   │                       text_service_activation.cpp, text_service_trace.cpp,
│   │                       key_event_sink.cpp, edit_session.cpp,
│   │                       candidate_ui_element.cpp, reading_ui_element.cpp,
│   │                       display_attribute.cpp, language_bar.cpp, register.cpp,
│   │                       status_controller.h, status_controller.cc,
│   │                       resource_loader.h, resource_loader.cc,
│   │                       host_compatibility/ (宿主兼容性运行时检测),
│   │                       preedit_mode.h, pch.h, resource.h
│   └── CMakeLists.txt      按指针大小输出 cxxime_tsf_x64.dll / cxxime_tsf_x86.dll
│
├── server/                 # 服务端进程
│   └── src/                main.cc, server_app.cc, server_app.h,
│                           session_manager.cc, session_manager.h
│
├── settings/               # 配置编辑器 GUI
│   └── src/                main.cc, editor_app.cc, editor_app.h
│
├── ui/                     # 候选窗口 + 状态窗口（GDI/Direct2D）
│   ├── include/cxxime/     candidate_window.h, status_window.h,
│   │                       layout.h, renderer.h, render_context.h
│   └── src/                candidate_window.cc, status_window.cc,
│                           d2d_renderer.cc, gdi_renderer.cc, layout.cc, theme.cc
│
├── data/                   # 数据文件
│   ├── default.json        # 默认配置
│   ├── themes.json         # 主题预设
│   ├── pinyin.dict.bin     # 拼音主词典（~73 MB）
│   ├── pinyin.dict.idx     # 拼音整数 ID 索引（~48 MB）
│   ├── pinyin.topn.bin     # 拼音 Top-N 候选索引（DAT-16，~212 MB）
│   ├── pinyin.spellings.bin# Patricia trie 拼写索引（~2.9 MB）
│   ├── pinyin.dict.db.zip  # SQLite 源词典（压缩，git 提交）
│   ├── wubi86.dict.bin     # 五笔主词典（~2.6 MB）
│   ├── wubi86.dict.idx     # 五笔整数 ID 索引（~2.1 MB）
│   ├── schemas/            # pinyin.schema.yaml（拼写代数规则）
│   └── tools/              # Python 词典工具（fetch/convert/build/algebra）
│
├── test/                   # 22 个 C++ + 2 个 Python 测试（24 ctest 条目, 390+ TEST）
│   ├── util/testutil.h     # 自研轻量测试框架
│   └── {engine, segmentor, dict, config, layout, preedit_mode, ipc, wubi,
│         wubi_engine, candidate_window, candidate_quality, status_window,
│         benchmark, short_cache, trace, mpscq, config_monitor,
│         dictionary_monitor, output_composer, engine_source,
│         session_manager_status, session_manager_integration,
│         build_short_cache, stage_trace_tools (Python)}_test.{cc|py}
│
├── third_party/            # 第三方库
│   ├── sqlite3/            # SQLite amalgamation（FTS5 + JSON1）
│   ├── nlohmann/           # nlohmann/json（header-only）
│   └── darts-clone/        # Darts-clone (Double Array Trie，Top-N 索引)
│
├── resource/               # 图标 + 资源 DLL 素材
└── scripts/                # package.py, prepare_dict.py, cxxime-setup.nsi,
                            # verify_data_files.py, build_short_cache.py,
                            # check_query_bench.py, benchmark.bat
```

---

## 6. 词典数据架构

三层架构（详见 [词典系统设计](dictionary.md)）：

| 层 | 格式 | 用途 |
|----|------|------|
| **Spelling Algebra** | Python 构建时规则引擎（`pinyin.schema.yaml`） | 预计算缩写/模糊音变体 |
| **Prism**（SpellingsIndex） | Patricia trie 二进制堆加载 | 输入串→音节序列映射，前缀搜索 |
| **Table**（Dict） | 二进制堆加载（按音节 ID 序列索引） | 词条精确查询，二分查找 |

**SQLite 的角色：** 仅用于构建时源数据，运行时无 SQLite 依赖。

**词典来源：** rime-ice（雾凇拼音，~190 万词条）+ rime-wubi86-jidian（五笔 86）。

**主要二进制文件：**

| 文件 | 大小 | 说明 |
|------|------|------|
| `pinyin.dict.bin` | ~73 MB | 拼音主词典（按 syllable_ids 排序） |
| `pinyin.dict.idx` | ~48 MB | 拼音整数 ID 索引（音节→词条映射） |
| `pinyin.topn.bin` | ~212 MB | 拼音 Top-N 候选索引（DAT-16 格式，Darts trie 查找） |
| `pinyin.spellings.bin` | ~2.9 MB | Patricia trie 拼写索引 |
| `wubi86.dict.bin` | ~2.6 MB | 五笔主词典 |
| `wubi86.dict.idx` | ~2.1 MB | 五笔整数 ID 索引 |

---

## 7. 已实现能力总览

### 7.1 混合输入模式（PINYIN/WUBI/MIXED）

三种输入模式：`InputMode::PINYIN`（纯拼音）、`WUBI`（纯五笔）、`MIXED`（混合）。模式经服务端全局状态同步，所有 session 一致。

MIXED 模式下 `MixedTranslator` 同时向拼音和五笔引擎发起查询，按 `MixedOrder` 排序策略合并：
- **kPinyinFirst**：拼音候选优先
- **kWubiFirst**：五笔候选优先（四码全字母输入，且拼音首选不显著更强时）
- **kAmbiguousInterleave**：短输入交叉交错

### 7.2 候选字段与用户词学习

`Candidate` 携带 `code`（原始输入码）、`syllables`（冒号分隔音节）、`source`（`kPinyin`/`kWubi`）、`origin`（`kSystem`/`kUser`/`kCache`）字段。选词时通过 `update_frequency(text, code, syllables)` 学习：`syllables` 使用户词生成缩写键与混合键索引，简拼、混合码均可命中。

用户词按 `score_user_match()` 分层加分：精确匹配 2×10⁸、缩写/混合键 1.2×10⁸、前缀按接近程度 4000~8×10⁵，叠加词频（上限 5 万）与最近使用加成。详见 [用户词典设计](user-dictionary.md)。

### 7.3 长拼音查询页缓存

`PinyinTranslator` 为超过 6 字符的长拼音维护 LRU 页缓存（64 条），键为 `(input, page_index, page_size, user_dict_version)`。重复查询与翻页直接命中，避免重复分词与词典扫描；用户词典版本变化自动失效，deadline 命中的不完整结果不缓存。

### 7.4 服务端可见状态全局化

`SessionManager` 维护 `GlobalVisibleState`（`ImeStatus` + `base_chinese_mode` 基础模式，Caps 覆盖时派生 `chinese_mode`）。状态变更经 `commit_global_state()` 统一提交并递增 revision，各 session 在按键/查询等入口经 `align_session_to_global()` 对齐；TSF 客户端通过 IPC（GET_STATUS / 心跳）获取状态并驱动状态窗口与语言栏。跨窗口切换中英文/模式不再丢失。

### 7.5 词典 Manifest 与热重载

`DictionaryManifest`（JSON）描述词典文件集合（role/path/sha256/size/required）。`DictionaryMonitor` 轮询 manifest 变更并触发词典资源替换；`ConfigMonitor` 通过共享内存 + Event 接收 Settings 保存通知触发配置重载。详见 [共享资源预加载](shared-resources.md)。

### 7.6 双架构 TSF DLL + 独立资源 DLL

TSF DLL 按架构输出 `cxxime_tsf_x64.dll` / `cxxime_tsf_x86.dll`，安装包同时部署，系统按进程位数加载。输入法 profile 图标由独立的 `cxxime-resources.dll` 提供（`resource_loader.cc` 加载）。

### 7.7 Top-N 候选索引（pinyin.topn.bin，DAT-16 格式）

为 1-6 字符短输入预计算 Top-N 候选，采用 **DAT-16 格式**（magic `CXTOPN\x02`）：双数组 Trie（Darts-clone）做 key 查找，内联 16 字节候选条目（文本/频率/评分）。词典加载时一次性读入堆内存（~212 MB）。

`lookup_indexed_fast()` 在 Trie 命中的 key 上直接返回预计算候选页，完全跳过 Syllabifier 路径枚举与 Dict 扫描，短输入延迟从毫秒级降至个位数微秒。

构建流程：`build_short_cache.py`（SQLite → 中间文件）→ `topn_builder --format dat16`（中间文件 → DAT-16）。详见 [短输入快速路径](short-input-fast-path.md)。

### 7.8 五笔词典必选打包

`wubi86.dict.bin` / `wubi86.dict.idx` 在 manifest 中 `required: true`，打包缺源直接报错。五笔为完整输入法能力而非可选插件。

---

## 8. 后续规划

- [ ] IPC 协议版本协商、变长消息
- [ ] 双拼支持
- [ ] 候选窗口动画完善

---

## 9. 相关文档

- [候选词选词算法](candidate-selection.md) — 查询管道与路径枚举
- [查询预算与候选收集](query-control.md) — QueryBudget、TopKCollector、扫描限制、超时检查点
- [中英文切换机制](ascii-composer.md) — AsciiComposer 配置与状态同步链路
- [词典系统设计](dictionary.md) — 三层架构、二进制格式、查询流程
- [用户词典设计](user-dictionary.md) — 内存多路索引与 TSV 持久化
- [IPC 架构设计](ipc-architecture.md) — IOCP 事件循环、管道安全、测试与性能基准
- [共享资源预加载](shared-resources.md) — 共享资源、全局状态与热重载
- [短输入快速路径](short-input-fast-path.md) — ShortCodeCache 与 topn.bin 缓存
- [可观测性设计](observability.md) — QueryTrace、TSF 事件追踪、日志、benchmark
- [安装与卸载](installation.md) — 构建、打包、注册
- [设置指南](settings-guide.md) — 配置项与设置界面
- [性能基准记录](benchmark-data.md) — 各阶段优化的历史数据
- [路径解析](path_resolution.md) — 数据目录与脚本路径规则

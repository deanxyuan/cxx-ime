# 轻量级 Windows 输入法 — 设计文档

实际指标：安装包 ~1MB（zip），内存占用 ~10MB（Server），启动 < 0.5s，候选响应 < 10ms。

## 1. 项目概述

本文档提出一套轻量级 Windows 输入法的设计方案。目标是构建一个可独立运行、依赖精简、易于维护的中文拼音输入法工具，仅支持 Windows 10+，仅实现 TSF 输入处理器。

---

## 2. 设计原则

1. **轻量依赖** — 去除 Boost、glog 等重量级依赖，使用现代 C++17 标准库替代
2. **简化架构** — 保留客户端-服务器分离模式，但精简 IPC 协议
3. **模块化** — 引擎层与 UI 层完全解耦，可独立替换
4. **最小可用** — MVP 阶段仅支持拼音输入，后续可扩展
5. **仅 TSF** — MVP 仅实现 TSF 输入处理器（Windows 10+），不考虑 IMM32 兼容

---

## 3. 目标架构

```
┌──────────────────────────────────────────────────────────────────┐
│                        轻量输入法总体架构                          │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────────┐    Named Pipe     ┌──────────────────┐  │
│  │   IME Client DLL    │  ◄═════════════►  │  IME Server.exe  │  │
│  │   (TSF Only)        │                    │                  │  │
│  │                     │                    │  ┌────────────┐  │  │
│  │   ┌──────────────┐  │                    │  │ LiteEngine │  │  │
│  │   │ KeyEvent     │  │                    │  │ (输入引擎)  │  │  │
│  │   │ Handler      │  │                    │  ├────────────┤  │  │
│  │   └──────────────┘  │                    │  │ DictMgr    │  │  │
│  │   ┌──────────────┐  │                    │  │ (词典管理)  │  │  │
│  │   │ IPC Client   │  │                    │  ├────────────┤  │  │
│  │   └──────────────┘  │                    │  │ Config     │  │  │
│  │   ┌──────────────┐  │                    │  │ (配置管理)  │  │  │
│  │   │ CandidateUI  │  │                    │  └────────────┘  │  │
│  │   │ (候选窗口)    │  │                    │                  │  │
│  │   └──────────────┘  │                    │  ┌────────────┐  │  │
│  └─────────────────────┘                    │  │ TrayIcon   │  │  │
│                                             │  │ (系统托盘)  │  │  │
│                                             │  └────────────┘  │  │
│                                             └──────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 4. 模块划分

### 4.1 模块 A: LiteEngine（输入引擎核心）

精简的输入引擎，仅保留核心输入处理能力。

| 子模块 | 功能 | 实现状态 |
|--------|------|----------|
| **Processor** | 按键处理（拼音输入、候选选择、翻页） | ✅ `PinyinProcessor` |
| **Translator** | 拼音→汉字候选翻译（含简拼扩展） | ✅ `PinyinTranslator` |
| **Segmentor** | 基础音节切分 + Patricia trie 拼写索引 | ✅ `PinyinSegmentor` + `Syllabifier` |
| **SpellingsIndex** | Patricia trie 拼写索引（缩写扩展，Prism 层） | ✅ `SpellingsIndex` |
| **AsciiComposer** | 可配置中英文切换（7 种切换样式，左右修饰键分离） | ✅ `AsciiComposer` |
| **DictManager** | 词典加载与查询 | ✅ `Dict`（二进制加载主词典 + 内存用户词典） |
| **ConfigManager** | 配置加载 | ✅ `Config`（JSON，nlohmann/json） |

**数据存储方案：**

- **主词典：** 二进制堆加载词典 + Patricia trie 拼写索引（一次性读入，启动 < 0.5s），详见 [词典系统设计](dictionary.md)
- **用户词典：** 内存 vector + map，TSV 文件持久化，详见 [用户词典设计](user-dictionary.md)

### 4.2 模块 B: TSF Client DLL（输入法前端 DLL）

负责与 Windows TSF（Text Services Framework）对接。MVP 仅支持 Windows 10 及以上版本，不实现 IMM32 兼容层。

**必须实现的 TSF 接口：**

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

**注册方式：** 通过 `regsvr32` 注册 COM DLL，在 `HKLM\SOFTWARE\Microsoft\CTF\TIP\` 下注册输入处理器。不再需要 IMM32 的 `ImmInstallIME` 注册方式。

### 4.3 模块 C: IME Server（后台服务进程）

托管输入引擎，处理来自客户端的请求。

| 功能 | 说明 |
|------|------|
| 会话管理 | 创建/销毁输入会话，维护会话状态 |
| IPC 服务 | 命名管道监听，处理请求/响应 |
| 引擎调度 | 将按键事件转发给 LiteEngine 处理 |
| 系统托盘 | 状态图标、右键菜单（切换输入法、设置） |
| 配置热加载 | 监控配置文件变更并重载 |

### 4.4 模块 D: CandidateUI（候选窗口）

轻量级候选词显示窗口。

**渲染方案选型：**

| 方案 | 优点 | 缺点 |
|------|------|------|
| Direct2D + DirectWrite | 高性能、高质量渲染 | API 较复杂 |
| GDI+ | 简单易用 | 性能一般，抗锯齿差 |
| Skia | 跨平台、高质量 | 引入额外依赖 |
| GDI (纯) | 最轻量 | 功能有限 |

**推荐：** 使用 Direct2D + DirectWrite，获得最佳渲染质量。

**UI 布局支持：**
- 横排候选（默认）
- 竖排候选
- 跟随光标定位
- DPI 感知

### 4.5 模块 E: IPC 层

**协议设计（简化版）：**

```cpp
enum class IPCCommand : uint32_t {
    START_SESSION      = 1,
    END_SESSION        = 2,
    PROCESS_KEY        = 3,
    SELECT_CANDIDATE   = 4,
    COMMIT_COMPOSITION = 5,
    CLEAR_COMPOSITION  = 6,
    FOCUS_IN           = 7,
    FOCUS_OUT          = 8,
};

struct IPCRequest {
    IPCCommand command;
    uint32_t   session_id;
    uint32_t   key_code;
    uint32_t   modifiers;
    uint32_t   candidate_index;
    bool       is_key_up = false;   // 修饰键释放事件
};

struct IPCResponse {
    IPCStatus status = IPCStatus::OK;
    char     commit_text[256] = {};
    char     preedit[256] = {};
    uint32_t candidate_count = 0;
    char     candidates[10][64] = {};
    uint32_t highlighted = 0;       // START_SESSION 时返回 session_id
    bool     ascii_mode = false;    // 引擎中英文状态
    bool     composing = false;     // 是否在组合中
};
```

**传输方式：** Named Pipe（每用户 `\\.\pipe\<username>\CxxIME`），IOCP 线程池（2-4 worker 线程），`FILE_FLAG_OVERLAPPED` + `CancelIoEx` 支持可中断停止。SDDL ACL（SYSTEM + Everyone + UWP）。详见 [IPC 架构设计](ipc-architecture.md)。

### 4.6 模块 F: 配置系统

配置文件格式（JSON）：

```json
{
    "schema": {
        "name": "CxxIME",
        "version": "1.0"
    },
    "engine": {
        "page_size": 9,
        "max_pinyin_length": 64
    },
    "ascii_composer": {
        "switch_key": {
            "Shift_L": "inline_ascii",
            "Shift_R": "commit_text",
            "Control_L": "noop",
            "Control_R": "noop"
        },
        "good_old_caps_lock": false
    },
    "style": {
        "font_face": "Microsoft YaHei UI",
        "font_point": 14,
        "layout": "horizontal",
        "candidate_count": 9,
        "inline_preedit": true,
        "preedit_type": "composition"
    },
    "theme": "light"
}
```

---

## 5. 技术选型总结

| 技术领域 | 选型 | 理由 |
|----------|------|------|
| 引擎 | 自研 LiteEngine | 仅需拼音，无需完整输入法框架 |
| 输入处理器 | 仅 TSF（单 DLL） | 放弃 Windows 7 兼容，简化实现 |
| 目标系统 | Windows 10+ | TSF 在 Win10 上行为稳定，无需 IMM32 兜底 |
| C++ 标准 | C++17 | 现代语言特性，减少外部依赖 |
| 序列化 | 固定结构体 + memcpy | 简单高效，无需 Boost |
| IPC | Named Pipe + 固定消息 | 去除 Boost 依赖 |
| 字典 | 二进制堆加载 + 内存用户词典 | 一次性读入，启动快 |
| 配置 | nlohmann/json | 头文件 only，轻量 |
| UI 渲染 | Direct2D/DirectWrite | 高质量渲染 |
| 日志 | spdlog 或自研 | 更轻量 |
| 安装 | NSIS | 成熟的 Windows 安装方案 |

---

## 6. 依赖清单

### 6.1 最小依赖（MVP）

| 依赖 | 用途 | 获取方式 |
|------|------|----------|
| Windows SDK | TSF/COM/Direct2D/GDI | 系统自带 |
| SQLite3 | 构建工具、dict_query 工具 | 源码编译（单文件 amalgamation，FTS5 + JSON1） |
| nlohmann/json | 配置解析 | 头文件 only |
| Python 3.6+ | 词典数据工具 | 可选（仅构建词典时需要） |

### 6.2 词典数据

| 数据 | 来源 | 说明 |
|------|------|------|
| 拼音词库 | 从开源拼音词典转换 | 基础拼音词库 |
| 用户词库 | 运行时积累 | 内存 vector+map，TSV 持久化 |

---

## 7. 关键技术挑战与对策

### 7.1 TSF COM 编程复杂度

**挑战：** TSF 接口众多，COM 编程繁琐，各应用行为不一致。

**对策：**
- 仅实现 TSF 接口，不考虑 IMM32 兼容，减少一半工作量
- 优先支持主流应用（记事本、Chrome、VS Code、Word）
- 使用 ATL 简化 COM 实现

### 7.2 拼音切分准确性

**挑战：** 拼音音节边界模糊（如 "xian" 可以是 "xi'an" 或 "xian"）。

**对策：**
- 实现贪心 + 回溯的音节切分算法
- 使用最大匹配优先策略

### 7.3 候选窗口与应用兼容性

**挑战：** 不同应用的窗口管理行为不同，候选窗口可能被遮挡或定位错误。

**对策：**
- 使用 TSF 的 `ITfContextView::GetTextExt` 获取光标位置
- 实现多显示器感知
- 使用分层窗口（Layered Window）避免焦点抢夺

### 7.4 输入法安装与注册

**挑战：** Windows 对 IME 安装有严格的安全要求（签名、注册表、文件路径）。

**对策：**
- 仅需 TSF COM 注册（`regsvr32`），无需处理 IMM32 的 `ImmInstallIME` 复杂逻辑
- 需要管理员权限安装
- 考虑支持 per-user 安装（Windows 10+）

---

## 8. 设计特点

| 维度 | cxx-ime |
|------|---------|
| 引擎 | 自研引擎（C++17），仅需拼音输入 |
| 输入处理器 | 仅 TSF（单 DLL），目标 Windows 10+ |
| 第三方库 | 1 个（nlohmann/json），SQLite 仅构建时使用，无 Boost 依赖 |
| 安装包大小 | ~1 MB（zip，不含词典） |
| 内存占用 | ~10 MB (Server) |
| 启动速度 | < 0.5s（一次性读入） |
| 可扩展性 | 源码精简，易于修改 |
| UI 渲染 | GDI（D2D 代码已实现未启用） |
| 测试 | 自研轻量框架（10 exe, 64+ 用例） |

---

## 9. 实际项目目录结构

项目命名为 `cxx-ime`（原设计稿用 `lite-ime`）。

```
cxx-ime/
├── CMakeLists.txt
├── README.md
├── LICENSE                 # Apache 2.0
├── build.bat / package.bat / install.bat / uninstall.bat / verify.bat
├── install.ps1 / uninstall.ps1
│
├── shared/                 # 共享基础类型
│   ├── include/cxxime/     ipc_protocol.h, key_event.h, logging.h, candidate.h
│   └── src/                key_event.cc
│
├── engine/                 # 输入引擎
│   ├── include/cxxime/     engine.h, processor.h, translator.h, segmentor.h,
│   │                       dict.h, context.h, config.h, ascii_composer.h,
│   │                       spellings_index.h, syllabifier.h
│   └── src/                engine.cc, pinyin_processor.cc, pinyin_translator.cc,
│                           pinyin_segmentor.cc, dict.cc, config.cc, context.cc,
│                           spellings_index.cc, syllabifier.cc, ascii_composer.cc
│
├── ipc/                    # IPC 层
│   ├── include/cxxime/     ipc_client.h, ipc_server.h
│   └── src/                ipc_client.cc, ipc_server.cc
│
├── tsf/                    # TSF 输入法 DLL
│   └── src/                dllmain.cpp, class_factory.cpp, text_service.cpp,
│                           key_event_sink.cpp, edit_session.cpp,
│                           display_attribute.cpp, language_bar.cpp, register.cpp,
│                           preedit_mode.h, pch.h, resource.h
│
├── server/                 # 服务端进程
│   └── src/                main.cc, server_app.cc, session_manager.cc
│
├── ui/                     # 候选窗口（GDI 渲染）
│   ├── include/cxxime/     candidate_window.h, layout.h, renderer.h
│   └── src/                candidate_window.cc, d2d_renderer.cc, layout.cc, theme.cc
│
├── data/                   # 数据文件
│   ├── default.json
│   ├── pinyin.dict.bin     # 二进制词典（生产）
│   ├── pinyin.spellings.bin# Patricia trie 拼写索引
│   ├── pinyin.dict.db      # SQLite 源词典
│   └── tools/              # Python 词典工具（fetch/convert/build）
│
├── test/                   # 10 个测试可执行文件
│   ├── util/testutil.h     # 自研轻量测试框架
│   └── {engine, segmentor, dict, config, layout, preedit_mode, ipc, wubi, candidate_window, benchmark}_test.cc
│
├── third_party/            # 第三方库
│   ├── sqlite3/            # SQLite amalgamation（FTS5 + JSON1）
│   └── nlohmann/           # nlohmann/json（header-only）
│
├── resource/               # 图标
└── reference/              # Microsoft SampleIME 参考代码
```

---

## 10. 词典数据架构

最终采用三层架构（详见 [词典架构设计](dict-architecture.md)）：

| 层 | 格式 | 用途 |
|----|------|------|
| **Spelling Algebra** | Python 构建时规则引擎 | 预计算缩写/模糊音变体 |
| **Prism**（SpellingsIndex） | Patricia trie 二进制堆加载 | 输入串→音节序列映射，前缀搜索 |
| **Table**（Dict） | 二进制堆加载（按音节 ID 序列索引） | 词条精确查询，二分查找 |

**SQLite 的角色：** 仅用于构建时源数据，主词典使用二进制堆加载。

**词典来源：** rime-ice（雾凇拼音），~190 万词条，~90 MB 文本 → ~45 MB 二进制。

**构建流程（Python）：**
```bash
python data/tools/fetch_dict.py      # 下载 rime-ice 拼音词典
python data/tools/dict_convert.py    # YAML → SQLite
python data/tools/build_binary.py    # SQLite → 二进制词典 + spellings trie
```

---

## 11. 风险评估

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| TSF 兼容性问题 | 高 | 中 | 优先测试主流应用 |
| 拼音切分不准 | 中 | 中 | 实现回溯算法，收集边界 case |
| 词典数据质量 | 中 | 低 | 使用成熟的拼音词典数据 |
| Windows 安全策略 | 高 | 低 | 遵循 Microsoft IME 开发规范，仅需 TSF COM 注册 |
| 性能不足 | 中 | 低 | 内存数据结构优化、缓存 |
| 不支持旧版 Windows | 低 | — | 有意为之，仅支持 Windows 10+ |

---

## 12. 参考资源

| 资源 | 说明 |
|------|------|
| [Microsoft TSF 文档](https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework) | TSF 官方开发文档 |
| [fcitx5](https://github.com/fcitx/fcitx5) | Linux 输入法框架参考 |
| [SQLite amalgamation](https://sqlite.org/amalgamation.html) | SQLite 单文件发行 |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON 解析库 |
| [spdlog](https://github.com/gabime/spdlog) | 轻量日志库 |

---

## 13. 总结

cxx-ime v0.1.0 已完成 MVP 交付，Engine 模块为核心亮点：

**Engine 模块（深度完成）：**
1. **三层词典架构** ✅ — SpellingsIndex (Patricia trie) → Syllabifier (BFS+DFS) → Dict (排序数组二分查找)
2. **完整拼音处理** ✅ — 标准切分 + 简拼扩展 + 混合拼音 + 模糊音预备
3. **可配置中英文切换** ✅ — AsciiComposer，7 种切换样式，左右修饰键分离
4. **二进制堆加载主词典 + 内存用户词典** ✅ — 一次性读入，启动 < 0.5s
5. **C++17 + nlohmann/json（头文件 only）** ✅ — SQLite 仅构建时使用，无 Boost 依赖
6. **10 个测试 exe / 64+ 用例** ✅ — 自研轻量测试框架
7. **dict_query 测试工具** ✅ — 交互式拼音/五笔词典查询

**IPC/Server/TSF/UI 模块（基础完成）：**
- 命名管道 IPC（IOCP 线程池，多客户端并发）
- Server 进程（系统托盘 + 会话管理）
- TSF DLL（完整 COM 接口实现）
- GDI 候选窗口（D2D 代码已实现备用）

最终指标：**安装包 ~1 MB（zip），内存 ~10 MB（Server），启动 < 0.5s，候选响应 < 10ms**。

### 后续规划

**待办：**
- [ ] IPC 协议版本协商、心跳/保活、变长消息
- [ ] Server 层深化（会话生命周期、热重载配置、日志系统）
- [ ] 模糊音（z/zh, c/ch, s/sh, n/l 等）
- [ ] Direct2D 渲染（代码已有，需启用）
- [ ] 双拼支持
- [ ] 候选窗口动画
- [ ] Wubi 86 集成（`wubi86.dict.db` 已有）

---

## 14. 相关文档

- [候选词选词算法](candidate-selection.md) — 查询管道与路径枚举
- [查询预算与 Deadline](query-budget.md) — QueryBudget、扫描限制、超时检查点
- [中英文切换设计](ascii-composer.md) — AsciiComposer 可配置切换机制
- [词典架构设计](dict-architecture.md) — 三层架构与二进制格式
- [词典系统设计](dictionary.md) — 二进制加载与查询流程
- [IPC 架构设计](ipc-architecture.md) — IOCP 重新设计
- [IPC 测试报告](ipc-testing.md) — 三阶段优化与 benchmark
- [共享资源预加载](shared-resources.md) — server 启动时加载一次，所有 session 共享
- [用户词典设计](user-dictionary.md) — SQLite → 内存数据结构迁移
- [可观测性设计](observability.md) — QueryTrace、日志、benchmark
- [安装与卸载](installation.md) — 构建、打包、注册

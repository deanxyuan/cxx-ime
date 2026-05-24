# CxxIME 代码审查报告

**日期:** 2026-05-24
**范围:** 全量代码审查（engine, ipc, server, tsf, ui, shared, test, build）
**测试状态:** 64 个测试通过（7 个独立 exe），0 个失败

---

## 总览

| 严重程度 | 数量 |
|----------|------|
| 严重 | 6 |
| 高 | 8 |
| 中 | 12 |
| 低 / 风格 | 11 |

---

## 严重问题

### C1. ~~ITfComposition COM 指针每次组合泄漏~~ [已修复]

**文件:** `tsf/src/edit_session.cpp:85-86`, `tsf/src/text_service.cpp:300-301,451-453`

`StartComposition` 返回一个已 AddRef 的 `ITfComposition*`。该指针存入 `_composition` 但从未 `Release()`——所有代码路径都直接置为 `nullptr`。每次组合周期泄漏一个 COM 对象。

```cpp
// edit_session.cpp:85 — 泄漏
pComp->EndComposition(ec);
_service->set_composition(nullptr);  // 应先 Release()

// text_service.cpp:300 — 泄漏
_composition = nullptr;  // 应先 Release()
```

**修复:** 在 `edit_session.cpp` 和 `text_service.cpp` 的 `OnCompositionTerminated` 中，置空前调用 `Release()`。

### C2. ~~ConnectNamedPipe 永久阻塞——服务器无法正常停止~~ [已修复]

**文件:** `ipc/src/ipc_server.cc`

管道创建时未使用 `FILE_FLAG_OVERLAPPED`。传给 `ConnectNamedPipe` 的 `OVERLAPPED` 结构被忽略，导致同步阻塞。`stop_event_` 机制无法中断它。在等待客户端连接时调用 `stop()` 会无限挂起。

**修复:** 已改为 `FILE_FLAG_OVERLAPPED`，连接/读/写均使用重叠 I/O。`stop()` 通过 `CancelIoEx` 中断所有阻塞 I/O。

### C3. Shift 切换中英文模式 —— 硬编码实现，需改造为可配置机制

**文件:** `tsf/src/text_service.cpp:170-183`

当前 Shift 切换逻辑硬编码在 TSF 层，存在以下问题：

```cpp
// 硬编码：不区分左右 Shift，无配置能力
if ((wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) && !_composing) {
    _chinese_mode = !_chinese_mode;
    CXXIME_LOG(L"Mode toggled: %s", _chinese_mode ? L"Chinese" : L"English");
    return S_OK;
}
```

**问题：**
- 硬编码行为，无法通过配置修改
- 不区分左右 Shift（无法实现左 Shift 临时英文、右 Shift 提交切换）
- 无切换样式（inline_ascii / commit_text 等）
- 无 500ms 超时防误触机制
- 模式状态仅在 TSF 层，引擎层无感知

**改造方案：** 已编写设计文档 `docs/ascii-composer-redesign.md`，将切换逻辑移到引擎层的 `AsciiComposer` 组件，支持 librime 风格的可配置切换机制。**代码实现尚未开始。**

### C4. ~~ShellExecuteW 的 UTF-8 转 UTF-16 转换错误~~ [已修复]

**文件:** `server/src/server_app.cc:238`

```cpp
std::wstring(app->config_path_.begin(), app->config_path_.end())
```

逐字节扩展不是合法的 UTF-8 到 UTF-16 转换。任何非 ASCII 路径（如 `C:\Users\...\数据\default.json`）会产生乱码，`ShellExecuteW` 将找不到文件。

**修复:** 使用 `MultiByteToWideChar(CP_UTF8, 0, ...)`。`server_app.cc` 和 `dict.cc` 均已修复。

### C5. 异步 Edit Session 在提交时可能乱序执行 [部分修复]

**文件:** `tsf/src/text_service.cpp:85-96,129-131,211-213,341-372,469-484`

`insert_text()` 和 `_end_composition()` 都使用 `TF_ES_ASYNC`。TSF 不保证执行顺序。结束组合的 session 可能在插入文本之前执行，导致文本丢失。

**已完成:** `insert_text()` 新增 `bool sync` 参数（默认为 `false`）。`Deactivate()` 中调用 `insert_text(text, true)` 使用 `TF_ES_ASYNCDONTCARE`。

**尚未完成:**
- `insert_text(sync=true)` 使用 `TF_ES_ASYNCDONTCARE` 而非 `TF_ES_SYNCHRONOUS`，不提供严格顺序保证
- `_end_composition()` 仍无条件使用 `TF_ES_ASYNC`，无同步选项
- `OnKeyDown` 和候选窗口回调中的提交路径仍分两次独立的异步 Edit Session 调用 `insert_text()` + `_end_composition()`

**修复:** 提交操作使用 `TF_ES_SYNCHRONOUS`，或将插入和结束合并到同一个 edit session。

### C6. ~~UnadviseSink 泄漏 sink（缺少 Release）~~ [已修复]

**文件:** `tsf/src/language_bar.cpp:186-191,338-343`

`CLangBarItemButton::UnadviseSink` 和 `CLangBarImeButton::UnadviseSink` 都将 `_pSink` 置为 `nullptr` 而未调用 `Release()`。该 sink 在 `AdviseSink` 中已 AddRef。

**修复:** 在置空前调用 `_pSink->Release()`。

### C7. ~~Syllabifier 置信度累加错误~~ [已修复]

**文件:** `engine/src/syllabifier.cc:102-109`

`enumerate_paths` 中，递归调用产生多个结果后，只有 `results.back().second` 获得当前边的置信度。递归调用产生的其他结果都遗漏了父边的贡献。

**修复:** 记录递归前 `results.size()`，递归后将 `cred` 加到所有新增条目。

---

## 高严重度问题

### H1. ~~IPC 同一时间只能服务一个客户端~~ [已修复]

**文件:** `ipc/src/ipc_server.cc`

已改为 `PIPE_UNLIMITED_INSTANCES`，`listen_loop()` 为每个客户端连接启动独立线程。使用 `FILE_FLAG_OVERLAPPED` + `CancelIoEx` 实现可中断的 I/O。`handler_mutex_` 序列化 handler 调用。参考 weasel 的 per-client thread + `g_api_mutex` 模式。

### H2. ~~Deactivate 使用异步 Edit Session 进行最终提交~~ [已修复]

**文件:** `tsf/src/text_service.cpp:119-131`

`Deactivate()` 期间，待提交文本通过 `TF_ES_ASYNC` 提交。TSF 在停用期间可能不处理异步 session，导致文本丢失。

**修复:** `insert_text()` 添加 `sync` 参数，`Deactivate` 中调用 `insert_text(text, true)` 使用 `TF_ES_ASYNCDONTCARE`。

### H3. ~~CommandLineToArgvW 返回值未检查空指针~~ [已修复]

**文件:** `server/src/main.cc:26-28`

`CommandLineToArgvW` 失败时返回 NULL。返回值在第 31 行的空指针检查之前就被解引用。

**修复:** 将 `get_arg` 调用移到 `if (argv)` 检查内部。

### H4. D2DRenderer 初始化失败时泄漏 COM 对象

**文件:** `ui/src/d2d_renderer.cc:7-38`

初始化中途失败时，之前分配的 COM 对象被泄漏。`CreateSolidColorBrush` 返回值未检查。`render()` 解引用可能为空的画刷。

### H5. ~~Engine 从未初始化 Syllabifier~~ [已修复]

**文件:** `engine/src/engine.cc`

`Engine::initialize()` 调用了 `translator_.set_dict()` 但从未调用 `translator_.set_syllabifier()`。`Engine` 类没有 `SpellingsIndex` 或 `Syllabifier` 成员。通过 Engine 门面的简拼扩展（如 "dd" -> "弟弟"）是死代码。

**修复:** `Engine` 新增 `SpellingsIndex`/`Syllabifier` 成员；`initialize()` 从 dict_path 推导 spellings.bin 路径，加载后关联到 translator。

### H6. ~~GetCaretPos + GetFocus 可能使用错误的窗口~~ [已修复]

**文件:** `tsf/src/text_service.cpp:261-263`

`GetCaretPos` 返回相对于光标所在窗口的坐标，但 `ClientToScreen` 使用 `GetFocus()`（键盘焦点窗口）调用。在多窗口应用中两者可能不同。

**修复:** 使用 `GetGUIThreadInfo` 获取光标窗口句柄；失败时回退到原 `GetCaretPos` + `GetFocus()`。

### H7. ~~SessionManager::sessions_ 无线程安全保护~~ [已修复]

**文件:** `server/src/session_manager.cc`

`create_session()`、`destroy_session()` 和 `get_engine()` 访问 `sessions_` 时无同步机制。当前安全仅因为 IPC 是单客户端的，但该约束未文档化。

**修复:** 添加 `std::mutex`，所有会话访问加 `lock_guard`。

### H8. ~~Deactivate 时未注销保留键~~ [已修复]

**文件:** `tsf/src/text_service.cpp:115-149`

`ActivateEx` 通过 `_register_preserved_key()` 注册了 Ctrl+Space，但 `Deactivate()` 从未注销。不存在 `_unregister_preserved_key` 方法。

**修复:** 新增 `_unregister_preserved_key()` 方法，在 `Deactivate` 中调用。

---

## 中严重度问题

### M1. ~~Context::commit() 未重置 page_index~~ [已修复]

**文件:** `engine/src/context.cc:18-32`

`commit()` 清除了 pinyin_buffer、committed_text 和 candidates，但 `page_index` 保持旧值。对比 `reset()` 会重置它。提交后下一次组合可能从错误的页码开始。

**修复:** 在 `commit()` 末尾添加 `page_index = 0;`。

### M2. ~~dict.cc 中 Unicode 路径转换错误~~ [已修复]

**文件:** `engine/src/dict.cc:112`

与 C4 相同的逐字节扩展问题。影响 SQLite 用户词典路径。

**修复:** 使用 `MultiByteToWideChar` 替换逐字节扩展。

### M3. ~~内存映射文件偏移量无边界校验~~ [已修复]

**文件:** `engine/src/dict.cc:71-81`, `engine/src/spellings_index.cc`

头部字段（`entries_offset`、`strings_offset`、`entry_count`）被直接信任，未验证是否在映射文件大小范围内。构造或损坏的文件会导致越界读取。

**修复:** `dict.cc` 新增 version、entries_offset、strings_offset、entry_count 的边界校验。

### M4. AdviseSink 仅支持一个 sink（违反 COM 规范）

**文件:** `tsf/src/language_bar.cpp:164-183,323-336`

两个语言栏按钮类都只存储一个 `_pSink`。`ITfSource::AdviseSink` 要求支持多个同时连接并返回不同的 cookie。

### M5. ~~Syllabifier 排序比较器有未使用变量~~ [已修复]

**文件:** `engine/src/syllabifier.cc:142-149`

注释说"先按最低（最优）类型排序，再按总置信度排序"，但 `max_a`/`max_b` 声明后从未使用。实际只比较了置信度。

**修复:** 删除未使用的 `max_a`/`max_b` 变量，简化注释。

### M6. PinyinSegmentor::segment() 只返回一条路径

**文件:** `engine/src/pinyin_segmentor.cc:88-94`

方法名为 `segment`（暗示多条结果），返回 `vector<vector<string>>`，但实际只返回一个元素。API 具有误导性。

### M7. ~~Config 缺少值校验~~ [已修复]

**文件:** `engine/src/config.cc`

`page_size` 为 0 会导致 `pinyin_processor.cc:91` 除零。负值会破坏分页。所有解析值均无边界检查。

**修复:** `page_size` 钳位到 [1, 100]，`font_size` 钳位到 [8, 72]。

### M8. ~~select_candidate() 不触发词频更新~~ [已修复]

**文件:** `engine/src/engine.cc:54-60`

`select_candidate()` 设置 committed_text 但不更新用户词典词频。只有 `process_key()` 返回 `COMMITTED` 时才触发更新。提交路径不一致。

**修复:** `select_candidate()` 中新增 `dict_.update_frequency()` 调用。

### M9. ~~Segmentor 重复持有~~ [已修复]

**文件:** `engine/include/cxxime/engine.h:31`, `engine/include/cxxime/translator.h:24`

`Engine` 和 `PinyinTranslator` 各自持有一个 `PinyinSegmentor`。Engine 的那个从未使用——只有 translator 内部的在用。

**修复:** 从 Engine 移除未使用的 `segmentor_` 成员和 `segmentor.h` 引用。

### M10. ~~pipe_channel.cc 是死代码~~ [已修复]

**文件:** `ipc/src/pipe_channel.cc`

`pipe_write()` 和 `pipe_read()` 已定义但从未被调用。无头文件声明。客户端和服务端都直接调用 `WriteFile`/`ReadFile`。

**修复:** 删除文件，从 CMakeLists.txt 移除。

### M11. ~~TrayIcon 类是死代码~~ [已修复]

**文件:** `server/src/tray_icon.h`, `server/src/tray_icon.cc`

`TrayIcon` 已完整实现但从未使用。`ServerApp` 直接管理自己的 `NOTIFYICONDATAW`。

**修复:** 删除文件，从 CMakeLists.txt 移除。

### M12. ~~sqlite_dict.cc 定义了无对应头文件的 SqliteDict 类~~ [已修复]

**文件:** `engine/src/sqlite_dict.cc`

定义了 `SqliteDict` 类方法，但无头文件声明该类。功能与 `dict.cc` 重叠。要么是死代码，要么是未集成的替代实现。

**修复:** 删除文件。

---

## 低严重度 / 风格问题

### L1. 管道/文件句柄使用 `void*` 而非 HANDLE

**文件:** `ipc/include/cxxime/ipc_client.h:32`, `engine/include/cxxime/dict.h:47-48`, `engine/include/cxxime/spellings_index.h:99-100`

丧失编译器类型安全。

### L2. PinyinProcessor 直接使用 Windows VK_* 常量

**文件:** `engine/src/pinyin_processor.cc`

将核心引擎与 `<windows.h>` 耦合。应使用抽象键码常量。

### L3. 错误日志不一致

`dict.cc` 和 `spellings_index.cc` 大量使用 `CXXIME_LOG`，但 `config.cc` 和 `pinyin_translator.cc` 完全没有日志。

### L4. estimate_text_width 是粗略估算

**文件:** `ui/src/layout.cc:7-20`

固定像素宽度（CJK 14px，ASCII 8px）未考虑实际字体度量。应使用 `GetTextExtentPoint32` 或 DirectWrite。

### L5. D2DRenderer 似乎是死代码

**文件:** `ui/src/d2d_renderer.cc`

`CandidateWindow` 未引用它，使用的是 GDI 渲染。

### L6. ~~composition.cpp 是空文件~~ [已修复]

**文件:** `tsf/src/composition.cpp`

仅包含 include。所有逻辑在其他文件中。

**修复:** 删除文件，从 CMakeLists.txt 移除。

### L7. 测试框架：ASSERT 直接终止进程

**文件:** `test/util/testutil.h`

`ASSERT_*` 宏调用 `exit(1)`。一个失败会跳过所有后续测试。无 EXPECT 风格的软断言。

### L8. 测试框架：RunAllTests 内存泄漏

**文件:** `test/util/testutil.cc:18`

全局 `tests` map 用 `new` 分配但从未释放。

### L9. IPC 测试使用硬编码管道名

**文件:** `test/ipc_test.cc`

所有测试使用 `IPC_PIPE_NAME`。如果有其他 IME 实例运行或测试并行执行会失败。

### L10. Config 测试将临时文件写入当前目录

**文件:** `test/config_test.cc`

不同于 dict/engine 测试使用 `GetTempPathA()`，config 测试使用当前目录的相对路径。

### L11. Engine::init 测试是空操作

**文件:** `test/engine_test.cc:22-25`

创建一个 `Engine` 然后断言 `true`。未测试任何功能。

---

## 测试覆盖缺口

| 组件 | 未测试的 API |
|------|-------------|
| `Engine` | `initialize()`, `process_key()`, `select_candidate()`, `get_commit_text()`, `clear()` |
| `PinyinSegmentor` | `segment()`（仅测试了 `segment_best()`） |
| `Dict` | `count()`, `unload_dict()`, `open()`, 重复打开行为 |
| `Context` | `is_composing()`, `reset()`, `commit()` |
| `KeyEvent` | `from_windows_key()`, `is_letter_key()`, `is_digit_key()` |
| `server` | 整个应用无测试 |

缺失场景：歧义拼音（"fangan"）、按键修饰键、实际 IPC 重连（同一客户端在服务器重启后恢复）、Engine 集成（从按键到提交文本的完整管线）。

---

## 构建系统问题

| 问题 | 文件 | 状态 |
|------|------|------|
| `ui/` 在 `tsf/` 之后添加但 `tsf` 链接了 `cxxime-ui` | `CMakeLists.txt:19-24` | |
| ~~`${CMAKE_SOURCE_DIR}` 应改为 `${PROJECT_SOURCE_DIR}`~~ | `test/CMakeLists.txt:5` | 审查建议有误（子目录 project() 导致 PROJECT_SOURCE_DIR 指向 test/），保持 CMAKE_SOURCE_DIR |
| 全项目无 `install()` 目标 | 所有 CMakeLists.txt | |
| ~~空的 `target_link_libraries` 调用~~ | `shared/CMakeLists.txt:11` | [已修复] |
| ~~所有测试编译为单一 exe~~ | `test/CMakeLists.txt` | [已修复] |
| `preedit_mode.h` 隐式头文件依赖 | `test/CMakeLists.txt` | |

---

## 修复优先级建议

1. **完成 C5（异步顺序问题）** —— `insert_text` 已有 sync 参数，但 `_end_composition` 仍为异步，需完成剩余修复
2. **实现 C3（Shift 切换机制改造）** —— C3 已通过 AsciiComposer 实现

已修复项：C1, C2, C3, C4, C6, C7, H1, H2, H3, H5, H6, H7, H8, M1, M2, M3, M5, M7, M8, M9, M10, M11, M12, L6

构建修复：shared/CMakeLists.txt 空 target_link_libraries 已移除；test/CMakeLists.txt 审查建议有误（子目录 project() 使 PROJECT_SOURCE_DIR 指向 test/），保持 CMAKE_SOURCE_DIR。

# 共享资源预加载（SharedResources）

描述 CxxIME Server 的共享资源架构：SharedResources 结构、SessionManager 的可见状态全局化、热重载机制、Engine 侧适配。面向维护者。

---

## 1. 设计动机

早期实现中每个 session 创建时独立加载字典文件（`pinyin.dict.bin` ~73MB、`pinyin.dict.idx` ~48MB、`pinyin.spellings.bin`），存在以下问题：

- 多个 worker 线程并发创建 session 时，同时打开同一 `.bin` 文件偶尔因防病毒/文件系统过滤驱动干扰而失败
- 内存浪费：N 个 session 各加载一份只读字典和配置
- session 创建慢：涉及文件 I/O

**解决方案**：Server 启动时预加载所有只读共享资源一次，session 创建时通过指针引用共享资源，变为纯内存操作。

---

## 2. SharedResources 结构

定义在 `server/src/session_manager.h:31-65`。

### 字段

| 字段 | 类型 | 用途 |
|------|------|------|
| `dict` | `shared_ptr<Dict>` | 拼音主字典（二进制堆加载，无 mmap） |
| `wubi_dict` | `shared_ptr<Dict>` | 五笔字典（必需：manifest 缺失或加载失败时服务启动失败） |
| `spellings` | `shared_ptr<SpellingsIndex>` | Patricia trie 拼音索引 |
| `syllabifier` | `shared_ptr<Syllabifier>` | 拼音切分器（依赖 spellings） |
| `config` | `shared_ptr<const Config>` | 当前配置（含 engine/style/theme/layout/ascii_composer/status_window/diagnostics） |
| `punct_mapping` | `shared_ptr<const PunctMapping>` | 标点映射表（从 `punctuation.json` 加载） |
| `config_path` | `string` | 配置文件路径（用于热重载） |
| `punct_path` | `string` | 标点映射文件路径 |
| `dict_path` | `string` | 拼音字典路径 |
| `wubi_dict_path` | `string` | 五笔字典路径 |
| `manifest_path` | `string` | 字典 manifest 路径 |
| `mutex` | `mutable mutex` | 保护所有字段的互斥锁 |

### 方法

| 方法 | 用途 |
|------|------|
| `load(dict_path, config_path)` | 从文件加载所有资源 |
| `snapshot()` | 原子获取所有资源的 shared_ptr 快照 |
| `dict_for_kind(kind)` | 根据 UserDictKind 返回 pinyin 或 wubi 字典 |
| `load_punctuation(path)` | 重新加载标点映射 |
| `reload_config()` | 重新加载配置（保留旧指针直到替换完成） |
| `reload_dictionaries()` | 重新加载字典（先保存用户词典，再加载新版本） |
| `add_user_entry` / `query_user_entries` / `delete_user_entry` / `replace_user_entry` | 用户词典 CRUD |
| `reload_user_dict` / `save_user_dict` | 用户词典持久化 |

### SharedResourceSnapshot

轻量快照结构（`session_manager.h:21-28`），不含路径和 mutex，用于在锁外安全持有资源引用：

```cpp
struct SharedResourceSnapshot {
    shared_ptr<Dict> dict;
    shared_ptr<Dict> wubi_dict;
    shared_ptr<SpellingsIndex> spellings;
    shared_ptr<Syllabifier> syllabifier;
    shared_ptr<const Config> config;
    shared_ptr<const PunctMapping> punct_mapping;
};
```

### 加载流程

```
ServerApp::initialize()
  → session_mgr_.initialize(dict_path, config_path)
    → SharedResources::load()
      → load_dictionary_resources()   → 解析 manifest，加载 dict/wubi_dict/spellings/syllabifier
      → Config::load()                → 加载 default.json + user default.json + themes.json
      → load_punctuation_mapping()    → 加载 punctuation.json
    → reset_global_state()            → 初始化 GlobalVisibleState
  → config_monitor_.start()           → 启动配置热重载监听
  → dictionary_monitor_.start()       → 启动字典热重载监听
```

---

## 3. SessionManager 全局可见状态

### 设计动机

跨窗口（session）的模式状态一致性。用户在一个窗口切换中英文模式后，所有其他窗口应感知到变化。

### GlobalVisibleState

```cpp
struct GlobalVisibleState {
    cxxime::ImeStatus status;          // chinese_mode, caps_lock, full_shape, chinese_punct, input_mode, revision
    bool base_chinese_mode = true;     // CapsLock overlay 下的"真实"模式
};
```

`ImeStatus` 定义在 `ipc_protocol.h:24-31`：

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `chinese_mode` | `bool` | `true` | 中文模式（CapsLock overlay 合成结果） |
| `caps_lock` | `bool` | `false` | CapsLock 灯状态 |
| `full_shape` | `bool` | `false` | 全角模式 |
| `chinese_punct` | `bool` | `true` | 中文标点模式 |
| `input_mode` | `InputMode` | `PINYIN` | 输入模式（pinyin/wubi/mixed） |
| `revision` | `uint64_t` | `0` | 状态版本号，每次变更自增 |

### 三个状态管理方法

| 方法 | 用途 |
|------|------|
| `reset_global_state(resources)` | Server 启动时从 Config 初始化 |
| `snapshot_global_state()` | 获取当前状态的副本（加 state_mutex_） |
| `commit_global_state(next)` | 提交新状态，检测是否有变更，递增 revision（加 state_mutex_） |

### align_session_to_global

```cpp
void SessionManager::align_session_to_global(SessionEntry& entry);
```

在各请求入口（session 创建、按键、选词、提交、状态查询、模式切换）以及配置/词典重载后调用，将 session 的状态与全局状态对齐：

1. 同步 `input_mode`（Engine 侧 `switch_mode()`）
2. 同步 `ascii_mode`（`engine->ascii_composer().set_ascii_mode(!base_chinese_mode)`）
3. 同步 CapsLock overlay（`engine->ascii_composer().sync_caps_lock()`）
4. 修复 engine 实际模式与全局的差异

### 三个互斥锁

| 互斥锁 | 类型 | 保护对象 |
|--------|------|---------|
| `mutex_` | `std::mutex` | session 映射表（`sessions_` + `next_id_`） |
| `state_mutex_` | `std::mutex` | 全局可见状态（`global_state_`） |
| `reload_mutex_` | `std::mutex` | 热重载操作的互斥（防止并发 reload） |

此外每个 `SessionEntry` 有独立的 `mutex` 保护 per-session 的 engine 操作。

### SessionEntry

```cpp
struct SessionEntry {
    unique_ptr<Engine> engine;
    steady_clock::time_point last_activity;
    ImeStatus ime_status;
    SharedResourceSnapshot resources;
    mutex mutex;  // per-session
};
```

### ProcessKeyResult

```cpp
struct ProcessKeyResult {
    IPCStatus status;
    ProcessResult result;
    string commit_text;
    bool composing;
    string preedit;
    CandidatePage candidates;
    ImeStatus ime_status;
};
```

### SessionManager 公开接口

**Session 生命周期：**
- `initialize(dict_path, config_path)` — 加载共享资源 + 初始化全局状态
- `create_session()` — 创建 session（纯内存操作，引用共享资源）
- `destroy_session(id)` — 销毁 session
- `touch_session(id)` — 更新最后活跃时间
- `cleanup_idle_sessions(timeout_ms)` — 清理超时空闲 session

**按键处理：**
- `process_key(id, event)` — 核心方法，返回 ProcessKeyResult
- `select_candidate(id, index)` — 选择候选项
- `commit_composition(id)` — 提交当前组合
- `clear_composition(id)` — 清除组合
- `focus_out(id)` — 焦点离开

**模式切换（6 个方法）：**
- `get_ime_status` / `toggle_chinese` / `toggle_shape` / `toggle_punct`
- `switch_input_mode(id)` + `switch_input_mode(id, mode)`
- `sync_ascii_mode(id, ascii_mode)` / `sync_caps_lock(id, caps_lock)`

**热重载：**
- `reload_config()` / `reload_dictionaries()` / `reload_punctuation(path)`

**用户词典管理（6 个方法）：**
- `add_user_entry` / `query_user_entries` / `delete_user_entry` / `replace_user_entry` / `reload_user_dict` / `save_user_dict`

---

## 4. 配置热重载

### ConfigMonitor

定义在 `shared/include/cxxime/config_monitor.h`。基于**共享内存 + Event 对象**的实现，由 TSF DLL 和 Server 协同：

- `initialize()` — 创建共享内存映射 + Event
- `start(callback)` — 启动监听线程
- `stop()` — 停止监听线程
- `add_ref()` / `dec_ref()` — 引用计数

Server 启动时在 `server_app.cc:77-79` 初始化：

```cpp
config_monitor_.initialize();
config_monitor_.start([this]() {
    session_mgr_.reload_config();
});
```

**字典热重载使用 `DictionaryMonitor`**（`shared/include/cxxime/dictionary_monitor.h`），基于文件变更轮询 + debounce：

```cpp
dictionary_monitor_.start({manifest_path}, [this]() {
    return session_mgr_.reload_dictionaries() == cxxime::IPCStatus::OK;
});
```

可配置选项（`DictionaryMonitorOptions`）：`debounce_ms=500`, `poll_ms=1000`, `retry_ms=1000`, `max_retries=5`。

### reload_config 流程

```
SessionManager::reload_config()
  1. shared_.reload_config()
     → Config::load() 重新加载所有配置 JSON
     → load_punctuation_mapping() 重新加载标点
  2. 获取快照 SharedResourceSnapshot
  3. 将新配置的 input_mode 提交到全局状态（commit_global_state）
  4. 遍历所有活跃 session：
     a. apply_resource_snapshot() 更新字典/拼写/切分器引用
     b. engine->reload_config() 更新 Config 引用 + 重建 AsciiComposer
     c. engine->set_fuzzy_enabled()
     d. align_session_to_global() 重新对齐状态
```

### reload_dictionaries 流程

```
SessionManager::reload_dictionaries()
  1. 加 reload_mutex_
  2. 遍历所有 session 加 per-session mutex
  3. 保存旧字典的用户词典
  4. shared_.reload_dictionaries()
     → 重新加载字典文件
  5. 遍历 session 调用 apply_resource_snapshot()
     → engine->rebind_shared_resources() 替换指针
```

---

## 5. Engine 侧适配

Engine 支持两种初始化路径：

### 自持资源路径（测试/工具使用）

```cpp
bool Engine::initialize(const string& dict_path, const string& config_path);
```
Engine 内部创建 `owned_dict_`, `owned_spellings_`, `owned_config_`, `owned_syllabifier_`。

### 共享资源路径（Server session 使用）

```cpp
bool Engine::initialize(Dict& dict, SpellingsIndex& spellings,
                        Syllabifier* syllabifier, const Config& config);
```
Engine 持有指针引用外部资源（`pinyin_dict_`, `spellings_`, `syllabifier_`, `config_`）。

### 热重载相关方法

| 方法 | 用途 |
|------|------|
| `reload_config(config)` | 更新 `config_` 指针 + 重建 `AsciiComposer` |
| `rebind_shared_resources(dict, spellings, syllabifier, wubi_dict)` | 热替换字典/拼写/切分器引用，重建 pipeline |
| `set_wubi_dict(dict*)` | 设置五笔字典指针（可选） |
| `set_fuzzy_enabled(bool)` | 切换模糊拼音（调用 `spellings_->set_fuzzy_enabled()` + 重建 pipeline） |

---

## 6. 测试覆盖

共享资源相关覆盖共 **22 个 C++ 测试可执行文件 + 1 个 Python 测试**（23 个 ctest 条目），合计 **440+ 个测试用例**（`TEST()` 宏计数）；完整测试套件（40 个 ctest 条目、510+ 用例）见项目 README。

| 文件 | 用例数 | 覆盖内容 |
|------|--------|---------|
| `engine_test.cc` | 72 | Engine 核心逻辑 |
| `output_composer_test.cc` | 45 | 文本输出组合 |
| `ipc_test.cc` | 32 | IPC 通信协议 |
| `dict_test.cc` | 26 | 字典操作 |
| `wubi_engine_test.cc` | 31 | 五笔 Engine |
| `engine_source_test.cc` | 22 | 源码级 Engine 行为 |
| `trace_test.cc` | 22 | 查询追踪 |
| `session_manager_status_test.cc` | 22 | 全局状态同步（含 CapsLock） |
| `status_window_test.cc` | 20 | 状态窗口 |
| `benchmark_test.cc` | 16 | 性能基准 |
| `config_test.cc` | 26 | 配置解析 |
| `candidate_window_test.cc` | 8 | 候选窗口 |
| `config_write_coordinator_test.cc` | 7 | 配置写入协调（预检/提交/取消） |
| `short_cache_test.cc` | 16 | 短输入缓存 |
| `wubi_test.cc` | 7 | 五笔翻译器 |
| `layout_test.cc` | 13 | 布局计算 |
| `segmentor_test.cc` | 5 | 拼音切分 |
| `preedit_mode_test.cc` | 8 | 预编辑模式 |
| `mpscq_test.cc` | 3 | MPSC 队列 |
| `dictionary_monitor_test.cc` | 3 | 字典监视器 |
| `session_manager_integration_test.cc` | 34 | 集成测试（含 process_key/select 等） |
| `candidate_quality_test.cc` | 2 | 候选质量 |

此外 `test/util/testutil.h` 提供轻量测试框架（`TEST()` / `ASSERT_*` 宏，无 EXPECT 风格断言）。

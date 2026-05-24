# 并发 Engine::initialize 优化

## Context

当前每个 session 创建时都执行 `Engine::initialize(dict_path, config_path)`，包含：
- `CreateFileA` + `CreateFileMappingA` + `MapViewOfFile` 打开 `pinyin.dict.bin`（~73MB）
- `CreateFileA` + mmap 打开 `pinyin.dict.idx`（~48MB）
- `CreateFileA` + mmap 打开 `pinyin.spellings.bin`
- SQLite 打开 `%APPDATA%\CxxIME\user.db`

多个 worker 线程并发创建 session 时，同时打开同一 .bin 文件偶尔失败（防病毒/文件系统过滤驱动干扰），导致 session 创建失败。

对于 IME 来说，dict/spellings/config 是**只读共享数据**，应在 server 启动时加载一次，而非每个 session 各加载一份。这消除并发问题、节省内存、且 session 创建变为纯内存操作（瞬时）。

## 设计方案

### 新增 SharedResources

```cpp
// session_manager.h
struct SharedResources {
    cxxime::Dict dict;
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    std::unique_ptr<cxxime::Syllabifier> syllabifier;

    bool load(const std::string& dict_path, const std::string& config_path);
};
```

### Engine 新增引用重载

```cpp
// engine.h — 新增重载，保留旧接口
class Engine {
public:
    // 旧接口：Engine 自持所有资源（tests / tools 使用）
    bool initialize(const std::string& dict_path, const std::string& config_path = "");

    // 新接口：共享资源引用（server session 使用）
    bool initialize(cxxime::Dict& dict, cxxime::SpellingsIndex& spellings,
                    cxxime::Syllabifier* syllabifier, const cxxime::Config& config);
    // ...
};
```

### SessionManager 改动

```cpp
class SessionManager {
public:
    bool initialize(const std::string& dict_path, const std::string& config_path);
    uint32_t create_session();  // 不再需要 dict_path/config_path 参数
    // ...

private:
    SharedResources shared_;
    std::unordered_map<uint32_t, SessionEntry> sessions_;
    uint32_t next_id_ = 1;
    std::mutex mutex_;
};
```

### ServerApp 改动

```cpp
bool ServerApp::initialize(const std::string& dict_path, const std::string& config_path) {
    // ... window / tray icon 初始化 ...
    if (!session_mgr_.initialize(resolved_dict, config_path_))  // 启动时加载一次
        return false;
    // ... ipc server start ...
}

cxxime::IPCResponse ServerApp::handle_request(const cxxime::IPCRequest& request) {
    case IPCCommand::START_SESSION: {
        uint32_t id = session_mgr_.create_session();  // 不再传路径
        // ...
    }
}
```

### 调用流程对比

```
旧：create_session → engine.init(path, path) → { Dict.open, Spellings.load, Config.load } × N 次
新：server 启动 → SharedResources.load() → { Dict.open, Spellings.load, Config.load } × 1 次
    create_session → engine.init(dict&, spellings&, syllabifier*, config&) → 纯内存赋值
```

### 修改文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `server/src/session_manager.h` | 修改 | 添加 SharedResources、initialize()、create_session() 改签名 |
| `server/src/session_manager.cc` | 修改 | SharedResources::load()、create_session 简化 |
| `engine/include/cxxime/engine.h` | 修改 | 新增引用重载 initialize() |
| `engine/src/engine.cc` | 修改 | 实现新旧两个 initialize() |
| `server/src/server_app.h` | 修改 | 去掉 handler 中的 dict_path_/config_path_ 传递 |
| `server/src/server_app.cc` | 修改 | startup 调用 session_mgr_.initialize()，handler 简化 |

**不修改的文件：**
- `test/ipc_test.cc` — mock handler，不创建真实 Engine
- `test/engine_test.cc` 等 — 使用旧 `Engine::initialize(path, path)` 接口
- `tools/dict_query/main.cc` — 使用旧接口
- `tools/ipc_test/main.cc` — 不涉及

## 验证

1. `build.bat debug` ✅ 编译通过
2. `ctest -C Debug` ✅ 全部 8 项通过
3. session 创建不再需要文件 I/O，变为纯内存操作（瞬时）
4. 内存占用降低：N 个 session 共享同一份 Dict/SpellingsIndex mmap（不再各加载一份）
5. 并发 session 创建的间歇性文件打开失败消除；完整的并发可靠性还需要 `IpcClient::connect()` 重试循环（见 IPC 重新设计文档）

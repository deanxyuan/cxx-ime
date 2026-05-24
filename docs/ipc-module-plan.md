# CxxIME IPC 模块深化设计

> **状态：** ✅ 已完成（2026-05-24）。测试工具 `ipc_tool` 位于 `tools/ipc_test/`，使用说明见 `tools/README.md`。

## 1. 现状分析

### 1.1 当前 IPC 实现

cxx-ime IPC 层提供基础的命名管道通信，已实现：

| 功能 | 状态 | 说明 |
|------|------|------|
| 命名管道 | ✅ | `\\.\pipe\CxxIME`，固定管道名 |
| 重叠 I/O | ✅ | `FILE_FLAG_OVERLAPPED` + Event 对象 |
| 多客户端 | ✅ | `PIPE_UNLIMITED_INSTANCES`，每客户端独立线程 |
| 可中断停止 | ✅ | `CancelIoEx` + `SetEvent` 唤醒等待线程 |
| 自动重连 | ✅ | 客户端失败后自动重试一次 |
| 消息模式 | ✅ | `PIPE_TYPE_MESSAGE` / `PIPE_READMODE_MESSAGE` |
| 测试 | ✅ | 14 个测试用例 |

当前缺陷：

| 问题 | 严重度 | 说明 |
|------|--------|------|
| 无安全 ACL | 高 | 管道无访问控制，任何进程可连接 |
| 无协议版本 | 中 | 客户端/服务端版本不匹配时无检测 |
| 无心跳 | 中 | 客户端异常断开时服务端无法感知 |
| 固定消息大小 | 低 | commit_text[256]、candidates[10][64] 硬限制 |
| 管道名固定 | 中 | 不支持多用户会话隔离 |
| 错误码粗糙 | 低 | 仅 uint32_t status，无语义 |

### 1.2 weasel IPC 参考

weasel 的 IPC 设计要点：

- **管道命名**：`\\.\pipe\<username>\WeaselNamedPipe`，每用户独立管道
- **安全 ACL**：SDDL 描述符，允许低完整性进程访问（UWP/IE 保护模式）
- **消息格式**：`PipeMessage{cmd, wParam, lParam}` 头部 + 可变长度文本体
- **会话管理**：基于 Echo() 做活性检查，StartSession 传客户端元数据
- **并发控制**：全局 mutex 串行化请求处理（librime 非线程安全）
- **错误处理**：异常机制，Win32 错误→DWORD 异常
- **单实例**：命名 mutex 保证每用户单服务器实例

## 2. 改进设计

### 2.1 管道安全（ACL）

参考 weasel 的 SDDL 方案，为管道添加安全描述符：

```cpp
// SDDL: 允许 SYSTEM、所有用户、所有应用包（UWP）
// 低完整性级别，NO_WRITE_UP
constexpr wchar_t IPC_SDDL[] =
    L"D:"
    L"(A;;GA;;;SY)"       // SYSTEM: 完全访问
    L"(A;;GA;;;WD)"       // Everyone: 完全访问
    L"(A;;GA;;;AC)"       // 所有应用包 (UWP): 完全访问
    L"S:(ML;;NW;;;LW)";   // 强制标签: 低完整性, 禁止向上写入
```

`SecurityAttributes` 类封装 SDDL→SECURITY_ATTRIBUTES 转换，供 `CreateNamedPipe` 使用。

### 2.2 协议版本协商

在 `START_SESSION` 时引入版本交换：

**客户端→服务端**（START_SESSION 请求附加）：
```
uint32_t protocol_version = 1   // 客户端支持的最高版本
```

**服务端→客户端**（START_SESSION 响应）：
```
uint32_t protocol_version        // 协商后的版本（min(client, server)）
uint32_t session_id              // 分配的会话 ID（0 = 失败）
```

版本不兼容（客户端版本低于服务端最低支持版本）时返回 session_id=0。

当前版本常量：
```cpp
constexpr uint32_t IPC_PROTOCOL_VERSION = 1;
constexpr uint32_t IPC_PROTOCOL_MIN_VERSION = 1;
```

### 2.3 请求序列号

在 IPCRequest 中添加 `sequence_number` 字段：

```cpp
struct IPCRequest {
    IPCCommand command;
    uint32_t session_id;
    uint32_t sequence_number;  // 单调递增，服务端检测重复/乱序
    uint32_t key_code;
    uint32_t modifiers;
    uint32_t candidate_index;
    bool is_key_up;
};
```

服务端对每个会话跟踪 `last_sequence_number`，检测：
- **重复**：seq == last_seq → 忽略（幂等）
- **倒退**：seq < last_seq → 记录警告，仍处理
- **跳跃**：seq > last_seq + 1 → 记录警告（可能有丢失消息）

IPCResponse 中回显 sequence_number 供客户端校验。

### 2.4 心跳/保活

添加 `HEARTBEAT` 命令（替代 weasel 的 Echo 模式）：

**客户端**：可定期发送 `HEARTBEAT` 请求
**服务端**：响应 `HEARTBEAT_ACK`，同时可用于检测客户端存活

服务端侧：为每个会话记录 `last_activity_time`，后台线程定期扫描超时会话（默认 60 秒无活动），自动清理。

客户端侧：`IpcClient` 提供 `ping()` 方法，返回 RTT（微秒）。

### 2.5 错误码体系

替换裸 `uint32_t status` 为语义错误码：

```cpp
enum class IPCStatus : uint32_t {
    OK = 0,
    // 协议错误
    ERR_UNKNOWN_COMMAND = 1,
    ERR_INVALID_SESSION = 2,
    ERR_PROTOCOL_VERSION = 3,
    ERR_SEQUENCE_OUT_OF_ORDER = 4,
    // 引擎错误
    ERR_ENGINE_NOT_INITIALIZED = 100,
    ERR_ENGINE_PROCESS_FAILED = 101,
    // 通信错误
    ERR_PIPE_WRITE_FAILED = 200,
    ERR_PIPE_READ_FAILED = 201,
    ERR_PIPE_DISCONNECTED = 202,
};
```

### 2.6 管道命名（多用户支持）

```cpp
inline std::wstring GetPipeName() {
    wchar_t username[256] = {};
    DWORD len = 256;
    GetUserNameW(username, &len);
    return std::wstring(L"\\\\.\\pipe\\") + username + L"\\CxxIME";
}
```

与 weasel 一致：`\\.\pipe\<username>\CxxIME`。

### 2.7 性能监控

在 IpcServer 中添加可选统计：

```cpp
struct IpcStats {
    uint64_t total_requests = 0;
    uint64_t total_responses = 0;
    uint64_t total_errors = 0;
    uint64_t avg_latency_us = 0;    // 滑动平均
    uint64_t max_latency_us = 0;
    uint32_t active_connections = 0;
    uint32_t peak_connections = 0;
};
```

服务端提供 `get_stats()` 方法，供测试工具查询。

## 3. 实现计划

### 3.1 修改 shared/include/cxxime/ipc_protocol.h

- 添加 `IPCStatus` 枚举
- 添加 `HEARTBEAT` / `HEARTBEAT_ACK` 命令
- 添加 `IPC_PROTOCOL_VERSION` 常量
- `IPCRequest` 添加 `sequence_number`、`protocol_version`
- `IPCResponse` 添加 `sequence_number`、`protocol_version`，status 改用 `IPCStatus`

### 3.2 修改 ipc/ — 安全 + 版本 + 心跳

**新增 `ipc/src/security_attributes.h`**：
- `SecurityAttributes` 类，封装 SDDL→SECURITY_ATTRIBUTES

**修改 `ipc/src/ipc_server.cc`**：
- `CreateNamedPipe` 使用安全属性
- `handle_client` 增加序列号校验
- 添加心跳超时检测（后台线程）
- 添加性能统计
- `listen_loop` 添加版本协商

**修改 `ipc/src/ipc_client.cc`**：
- `start_session` 发送协议版本
- 添加 `ping()` 方法
- 序列号递增

### 3.3 修改 server/ — 适配新协议

- `SessionManager` 添加 `last_activity_time` 跟踪
- `ServerApp::handle_request` 适配新错误码
- 添加 `HEARTBEAT` 处理

### 3.4 新增 tools/ipc_test/ — IPC 测试工具

类似 `dict_query` 的交互式测试工具：
- `connect <pipe_name>` — 连接
- `session start/end` — 会话管理
- `key <vk_code> [modifiers]` — 发送按键
- `select <index>` — 选择候选
- `ping` — 延迟测试
- `stress <count> <concurrency>` — 压力测试
- `bench <count>` — 基准测试（吞吐量）
- `stats` — 查询服务端统计
- `monitor` — 实时监控模式

### 3.5 测试扩展

从当前 14 个测试扩展到约 30 个：

| 分类 | 新增测试 |
|------|----------|
| Security | ACL 拒绝无权限连接（低完整性模拟） |
| Version | 版本协商（相同/不兼容/客户端更高） |
| Heartbeat | ping/pong、超时检测、自动清理 |
| Sequence | 重复序列号、倒退序列号、跳跃序列号 |
| Error | 无效命令、无效会话、管道中断恢复 |
| Stats | 统计计数器正确性 |
| Stress | 1000 次连续请求、多客户端并发压力 |

## 4. 兼容性

向后兼容策略：
- 新客户端连接旧服务端：版本协商失败 → session_id=0 → 客户端报告"服务端版本过旧"
- 旧客户端连接新服务端：旧请求不含 protocol_version → 服务端默认按 v1 处理
- 旧客户端（不含 sequence_number，即 0）：服务端跳过序列号校验
- 旧版管道名（`\\.\pipe\CxxIME`）：服务端同时监听新旧两个管道名，过渡期后移除旧名

## 5. 文件清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 修改 | `shared/include/cxxime/ipc_protocol.h` | 协议结构体、枚举、常量 |
| 新增 | `ipc/src/security_attributes.h` | 管道安全 ACL |
| 修改 | `ipc/include/cxxime/ipc_server.h` | 统计、心跳配置 |
| 修改 | `ipc/include/cxxime/ipc_client.h` | ping、版本 |
| 修改 | `ipc/src/ipc_server.cc` | 安全、版本协商、心跳、统计 |
| 修改 | `ipc/src/ipc_client.cc` | 版本协商、ping、序列号 |
| 修改 | `server/src/server_app.cc` | 适配新协议 |
| 修改 | `server/src/session_manager.h` | 活跃时间跟踪 |
| 修改 | `server/src/session_manager.cc` | 超时清理 |
| 新增 | `tools/ipc_test/main.cc` | IPC 测试工具 |
| 新增 | `tools/ipc_test/CMakeLists.txt` | 测试工具构建 |
| 修改 | `test/ipc_test.cc` | 新增测试用例 |

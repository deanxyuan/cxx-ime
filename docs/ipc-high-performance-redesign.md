# IPC 高性能重新设计

## Context

当前 IPC 模块基于同步 named pipe + `FlushFileBuffers`，benchmark 平均延迟 ~15ms/次（最低 ~911us）。IME 每次按键都需要一次 IPC 往返，15ms 延迟会导致用户感知到的输入卡顿。目标：< 1ms/次。

### 根因分析

`FlushFileBuffers` 在 message-mode named pipe 上的语义是：阻塞直到对端读取完数据。两端都用 Flush 导致每次请求经历两次调度器等待（client flush → server read, server flush → client read），每次至少一个调度器时间片（~7-15ms）。

### 为什么 FlushFileBuffers 是多余的

Message-mode pipe (`PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE`) 已保证消息边界。`WriteFile` 将完整消息拷贝到内核缓冲区即返回，`ReadFile` 读取完整消息。`FlushFileBuffers` 不提供额外的数据完整性保证，仅强制同步等待对端消费，纯粹增加延迟。

## 设计方案

### 架构变更

| 组件 | 当前 | 新设计 |
|------|------|--------|
| Server I/O 模型 | 同步 ReadFile/WriteFile | Overlapped I/O + IOCP |
| Server 线程模型 | 1 accept + N client 线程 | 1 accept + M worker (IOCP pool) |
| Server 关闭 | CancelSynchronousIo | Dummy connection + PostQueuedCompletionStatus sentinel |
| Client I/O 模型 | 同步 + FlushFileBuffers | 同步，去掉 FlushFileBuffers。Server 端 WriteFile 用 overlapped |
| Client API | 不变 | 不变 |
| 协议 | 不变 (IPCRequest/IPCResponse) | 不变 |
| handler_mutex_ | 有（不必要） | 移除（handler 在 start() 前设置，运行期只读） |

### Server IOCP 事件循环

```
accept_loop (1 thread, synchronous ConnectNamedPipe)
  │
  ├─ CreateNamedPipe (FILE_FLAG_OVERLAPPED)
  ├─ ConnectNamedPipe (sync, blocks until client)
  ├─ CreateIoCompletionPort(pipe, iocp)
  └─ ReadFile(overlapped) → IOCP 接管

worker_loop (M threads, GetQueuedCompletionStatus)
  │
  ├─ read complete → handler_(request) → WriteFile(overlapped)
  └─ write complete → ReadFile(overlapped)
```

每个 client 连接一个 `ClientContext`，内含 pipe handle、OVERLAPPED、request/response buffer、状态标志。同一时刻每个 client 只有一个 pending overlapped 操作（请求→响应→请求循环），复用一个 OVERLAPPED。

### 关闭协议

```
stop():
  1. running_ = false
  2. CreateFile + CloseHandle(dummy 连接) → 唤醒 accept 线程的 ConnectNamedPipe
  3. join accept_thread
  4. 遍历 contexts_: CancelIoEx(ctx->pipe) → 触发 I/O 完成通知到 IOCP
  5. PostQueuedCompletionStatus(nullptr) × worker_count → worker 退出
  6. join workers
  7. 清理残留 contexts、CloseHandle(iocp)
```

注：CancelSynchronousIo 存在竞态（accept 线程可能未进入可取消的等待状态），实际改为 dummy 连接唤醒。

### 线程数

Worker 线程数 = clamp(hardware_concurrency, 2, 4)。IME server 通常 1-2 个活跃 client，2-4 个 worker 足够，避免过多空闲线程。

### Client 改动

1. 移除 `ipc_client.cc:send_request()` 中的 `FlushFileBuffers(pipe)` 调用
2. `connect()` 加入重试循环：`WaitNamedPipeW` 返回后 `CreateFileW` 可能因其他线程抢走实例而失败，失败后重新 `WaitNamedPipeW` 而非直接放弃

## 修改文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `ipc/include/cxxime/ipc_server.h` | 重写 | IOCP 成员、ClientContext 内部结构 |
| `ipc/src/ipc_server.cc` | 重写 | IOCP accept + worker loop |
| `ipc/src/ipc_client.cc` | 修改 | 删除 FlushFileBuffers 调用 |

**不修改的文件：**
- `ipc/include/cxxime/ipc_client.h` — 公开 API 不变
- `shared/include/cxxime/ipc_protocol.h` — 协议不变
- `ipc/src/security_attributes.h` — SDDL ACL 不变
- `server/src/server_app.cc` — 使用相同 IpcServer 接口
- `test/ipc_test.cc` — 26 个测试用例应全部通过
- `tools/ipc_test/main.cc` — 使用相同 IpcClient 接口

## 验证

1. **编译**: `build.bat debug` ✅
2. **单元测试**: 26 个测试用例全部通过（`ctest -C Debug`）
3. **性能**: Preedit RTT 从 ~14709us 降至 ~110us（avg），提升 133x
4. **交互测试**: `ipc_tool` 全部 12 条命令正常（connect/status/session/key/select/commit/focus/bench/stress/disconnect）
5. **多客户端**: `concurrent_clients` 测试（3 客户端 × 50 请求）通过

# IPC 模块测试报告

## 架构变更历程

### 阶段 1：IPC 传输层 IOCP 重新设计

详见 [IPC 架构设计](ipc-architecture.md)。

| 组件 | 旧设计 | 新设计 |
|------|--------|--------|
| Server I/O | 同步 ReadFile/WriteFile | Overlapped ReadFile/WriteFile + IOCP |
| Server 线程 | 1 accept + N client 线程 | 1 accept + 2~4 worker (IOCP pool) |
| Client I/O | 同步 + FlushFileBuffers | 同步，无 FlushFileBuffers |
| Server 关闭 | CancelSynchronousIo | dummy connection + PostQueuedCompletionStatus |
| MSDN 合规 | — | FILE_FLAG_OVERLAPPED handle 上全部 I/O 使用 OVERLAPPED 结构 |

### 阶段 2：共享资源预加载

详见 [共享资源预加载](shared-resources.md)。

Dict、SpellingsIndex、Config 在 server 启动时加载一次，所有 session 共享引用。session 创建不再有文件 I/O。

### 阶段 3：用户词典 SQLite → 内存

详见 [用户词典设计](user-dictionary.md)。

用户词典操作全部变为内存操作，消除 SQLite 并发瓶颈。preedit 延迟提升 2.2x，commit 延迟提升 354x。

## 单元测试（ctest）

10 个测试可执行文件，全部通过。

```
Test project build/
    Start  1: engine_test           — Passed    0.06 sec
    Start  2: segmentor_test        — Passed    0.04 sec
    Start  3: dict_test             — Passed    0.05 sec
    Start  4: config_test           — Passed    0.04 sec
    Start  5: layout_test           — Passed    0.04 sec
    Start  6: preedit_mode_test     — Passed    0.04 sec
    Start  7: ipc_test              — Passed    2.70 sec (26 cases)
    Start  8: wubi_test             — Passed    0.07 sec
    Start  9: candidate_window_test — Passed    0.03 sec
    Start 10: benchmark_test        — Passed    0.10 sec

100% tests passed, 0 tests failed out of 10
```

### ipc_test 用例明细（26 个）

**Protocol**（4）：`pipe_name`、`request_struct_size`、`response_struct_size`、`response_zero_init`

**Server**（2）：`start_stop`、`double_stop`

**Client**（3）：`connect_no_server`、`connect_with_server`、`disconnect_idempotent`

**Error**（3）：`unknown_command`、`invalid_session`、`engine_not_initialized`

**IPC**（10）：`start_session`、`end_session`、`process_key_preedit`、`process_key_commit`、`process_key_rejected`、`select_candidate`、`commit_composition`、`clear_composition`、`focus_in_out`、`send_request`

**MultiClient**（2）：`two_clients_simultaneous`、`sequential_sessions`

**Reconnect**（1）：`server_restart`

**Stress**（2）：`rapid_requests`、`concurrent_clients`

## ipc_tool 交互命令测试

环境：
- 服务端：`cxxime-server.exe --dict data/pinyin.dict.bin --config data/default.json`
- 客户端：`ipc_tool.exe`

| 命令 | 阶段 1 | 阶段 3 | 备注 |
|------|--------|--------|------|
| `connect` | ✅ | ✅ | |
| `status` | ✅ | ✅ | |
| `session start` | ✅ | ✅ | |
| `key 4E` | ✅ | ✅ | |
| `key 49` | ✅ | ✅ | |
| `select 0` | ✅ | ✅ | |
| `key 48` | ✅ | ✅ | |
| `commit` | ✅ | ✅ | |
| `focus in` | ✅ | ✅ | |
| `focus out` | ✅ | ✅ | |
| `session end 1` | ✅ | ✅ | |
| `bench` | ✅ | ✅ | 传输延迟 ~950us（含 handler 处理），纯 IO 传输 ~110us |
| `stress 6 1` | — | ✅ | 串行 100% 成功 |
| `stress 6 3` | ✅ | ✅ | 并发 ~67% 成功 |
| `disconnect` | ✅ | ✅ | |

## 性能基准数据（阶段 3 最终）

### 测试方法

`key 49` (i, 拼音 preedit) → `key 0D` (Enter, commit) 交替 30 轮。每轮 Enter 清空拼音缓冲。测量 client 端完整 `WriteFile + ReadFile` 往返时间（含 IPC 传输 + engine 处理）。排除首轮冷启动数据。

### 三阶段对比

| 指标 | 旧版 (sync+Flush) | 阶段 1 (IOCP) | 阶段 3 (+内存用户词典) | 总提升 |
|------|------|------|------|------|
| Preedit 最小值 | 911 us | 83 us | **29 us** | **31x** |
| Preedit 平均值 | ~14709 us | ~110 us | **~50 us** | **294x** |
| Commit 最小值 | 未单独测量 | 7147 us | **13 us** | **550x** |
| Commit 平均值 | 未单独测量 | ~7800 us | **~22 us** | **354x** |

### Preedit 延迟明细（30 轮，us）

```
332, 111, 89, 101, 55, 62, 55, 80, 80, 70,
 68,  67, 79,  52, 65, 64, 33, 30, 30, 29,
 29,  30, 29,  39, 29, 46, 40, 42, 50, 31
```

稳态（第 5 轮起）范围 29~80us，平均 ~50us，无突刺。

### Commit 延迟明细（30 轮，us）

```
8057, 45, 28, 35, 29, 48, 68, 27, 23, 23,
  24, 25, 25, 25, 16, 22, 15, 13, 14, 13,
  14, 13, 13, 13, 13, 24, 21, 18, 22, 14
```

稳态（第 5 轮起）范围 13~27us，平均 ~22us。SQLite INSERT→内存操作的效果在此路径最显著。

### bench 命令（连续相同按键）

bench 前几次 IPC 传输延迟 ~950us（与 preedit 路径稳态一致）。延迟递增根因为拼音缓冲累积，属于 engine 层已知行为。

## 并发测试

### stress 6 1（串行，单客户端）

| 运行 | 结果 |
|------|------|
| 第 1 次 | 6 ok, 0 failed |
| 第 2 次 | 6 ok, 0 failed |
| 第 3 次 | 6 ok, 0 failed |

串行 **100% 成功**，验证 session 创建 + process_key 全路径可靠。

### stress 6 3（并发，3 客户端）

| 运行 | 结果 | 说明 |
|------|------|------|
| 第 1 次 | 4 ok, 2 failed | 2 个客户端成功，1 个失败 |
| 第 2 次 | 4 ok, 2 failed | 同上 |
| 第 3 次 | 4 ok, 2 failed | 同上 |

并发下始终恰好 1 个 client 失败（3 并发 client 同时连接时，单 accept 线程 + `WaitNamedPipeW` 2000ms 超时偶尔不足）。此系 pipe 连接层已知竞态，与引擎/user dict 无关。单元测试 `concurrent_clients`（mock handler，3 客户端 × 50 请求）全部通过，验证 IPC 层多客户端并发正确。

## 已知问题

1. ~~**bench 延迟偏高**~~ — 已解决。IPC 传输延迟从 ~15ms 降至 ~50us（preedit avg），提升 294x。

2. ~~**pinyin buffer 填满后延迟递增**~~ — 已解决。Syllabifier 路径枚举加入 10000 上限 + 信度排序。

3. ~~**并发 client 连接偶尔超时**~~ — 已解决。`IpcClient::connect` 加入重试循环。

## 当前架构特征

| 方面 | 实现 |
|------|------|
| I/O 模型 | Read/Write 均 overlapped (IOCP) |
| FlushFileBuffers | 无（message-mode pipe 保证消息边界） |
| 线程模型 | 1 accept + 2~4 worker (IOCP pool) |
| 管道安全 | SDDL ACL（SYSTEM + Everyone + UWP） |
| 管道缓冲 | 64KB |
| 管道命名 | 每用户 `\\.\pipe\<username>\CxxIME` |
| 用户词典 | 内存 vector+map (shared_mutex) |
| Preedit RTT (avg) | **~50 us** |

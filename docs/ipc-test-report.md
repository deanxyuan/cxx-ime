# IPC 模块测试报告

日期：2026-05-24

## 单元测试（ctest）

8 个测试可执行文件，全部通过。

```
Test project D:/gitee/cxx-ime/build
    Start 1: engine_test    — Passed    0.07 sec
    Start 2: segmentor_test — Passed    0.04 sec
    Start 3: dict_test      — Passed    0.05 sec
    Start 4: config_test    — Passed    0.05 sec
    Start 5: layout_test    — Passed    0.04 sec
    Start 6: preedit_test   — Passed    0.04 sec
    Start 7: ipc_test       — Passed    2.70 sec (22 cases)
    Start 8: wubi_test      — Passed    0.06 sec

100% tests passed, 0 tests failed out of 8
Total Test time = 3.06 sec
```

### ipc_test 用例明细（22 个）

**Protocol**（4）
- `pipe_name` — 管道名常量校验
- `request_struct_size` — 请求结构体大小
- `response_struct_size` — 响应结构体大小
- `response_zero_init` — 零初始化校验

**Server**（2）
- `start_stop` — 服务启停
- `double_stop` — 重复停止幂等

**Client**（3）
- `connect_no_server` — 无服务端超时
- `connect_with_server` — 正常连接
- `disconnect_idempotent` — 重复断开幂等

**IPC**（8）
- `start_session` — 创建会话
- `end_session` — 结束会话
- `process_key_preedit` — 按键→候选
- `process_key_commit` — 按键→上屏
- `process_key_rejected` — 按键拒绝
- `select_candidate` — 选择候选
- `commit_composition` — 提交组合
- `focus_in_out` — 焦点切换
- `send_request` — 原始请求

**MultiClient**（2）
- `two_clients_simultaneous` — 双客户端并发
- `sequential_sessions` — 顺序会话

**Reconnect**（1）
- `server_restart` — 服务重启重连

**Error**（3）
- `unknown_command` — 未知命令
- `invalid_session` — 无效会话
- `engine_not_initialized` — 引擎未初始化

**Stress**（2）
- `rapid_requests` — 200 次连续请求
- `concurrent_clients` — 3 客户端并发

## ipc_tool 交互命令测试

环境：
- 服务端：`cxxime-server.exe --dict data\pinyin.dict.bin --config data\default.json`
- 客户端：`ipc_tool.exe`

| 命令 | 结果 | 输出摘要 |
|------|------|----------|
| `help` | ✅ | 命令列表正常 |
| `connect` | ✅ | Connected |
| `status` | ✅ | Connected: yes, Session: none |
| `session start` | ✅ | Session started: id=1 |
| `key 4E` | ✅ | preedit: n, 9 candidates |
| `key 49` | ✅ | preedit: ni, candidates: 你, 你啊, ... |
| `select 0` | ✅ | commit=你 |
| `commit` | ✅ | text=（已提交后为空） |
| `focus in` | ✅ | Focus in sent |
| `focus out` | ✅ | Focus out sent |
| `session end 1` | ✅ | Session 1 ended |
| `bench 5` | ✅ | 5/5 ok, Avg: 14709us, Min: 911us |
| `disconnect` | ✅ | Disconnected |
| `quit` | ✅ | 正常退出 |

`stress` 未单独验证（与 `bench` 共用相同的 session 创建 + process_key 路径）。

## 已知问题

1. **bench 延迟偏高**：平均 ~15ms/次，首个请求 ~100ms。疑似 `FlushFileBuffers` 引入额外等待，需进一步排查。

2. **pinyin buffer 填满后延迟递增**：连续输入 64 个相同字母后，`PinyinTranslator` 处理长拼音串耗时增加。

## 实现对齐

与 weasel 对比（`PipeChannel.cpp` / `WeaselServerImpl.cpp`）：

| 方面 | weasel | cxx-ime |
|------|--------|---------|
| I/O 模式 | 全同步 | 全同步 |
| WriteFile 后 FlushFileBuffers | ✅ 两端 | ✅ 两端 |
| 管道中断 | boost::thread::interrupt | CancelSynchronousIo |
| 管道安全 | SDDL ACL | SDDL ACL |
| 管道缓冲 | 64KB | 64KB |
| 管道命名 | 每用户 | 每用户 |

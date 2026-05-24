# CxxIME 开发工具

以下命令基于 `cxx-ime/` 目录，可直接拷贝执行。

前置：先编译。

```cmd
build.bat debug
```

---

## dict_query — 词典查询工具

拼音模式（需 `.bin` + `.spellings.bin`）：

```cmd
.\build\tools\dict_query\Debug\dict_query.exe --mode pinyin --dict .\data\pinyin.dict.bin --spellings .\data\pinyin.spellings.bin
```

五笔模式（需 `.bin`）：

```cmd
.\build\tools\dict_query\Debug\dict_query.exe --mode wubi --dict .\data\wubi86.dict.bin
```

| 输入 | 说明 |
|------|------|
| `sdf` | 查候选词 |
| `:s sdf` | 查看音节切分（仅拼音模式） |
| `:q` | 退出 |

---

## sqlite_query — SQLite .db 直读工具

```cmd
.\build\tools\sqlite_query\Debug\sqlite_query.exe .\data\wubi86.dict.db
```

| 输入 | 说明 |
|------|------|
| `sdf` | 按 code 前缀查词 |
| `:q` | 退出 |

---

## ipc_tool — IPC 测试工具

### 启动服务端

```cmd
.\build\server\Debug\cxxime-server.exe --dict .\data\pinyin.dict.bin --config .\data\default.json
```

### 启动客户端

```cmd
.\build\tools\ipc_test\Debug\ipc_tool.exe
```

### 命令列表

| 命令 | 说明 |
|------|------|
| `connect` | 连接服务端 |
| `disconnect` | 断开连接 |
| `status` | 显示连接状态 |
| `session start` | 创建会话 |
| `session end <id>` | 结束会话 |
| `key <vk> [mods]` | 发送按键（VK 十六进制） |
| `select <index>` | 选择候选（0-based） |
| `commit` | 提交组合文本 |
| `focus in/out` | 焦点事件 |
| `bench <n>` | 基准测试 n 次（自建 session） |
| `stress <n> [c]` | 压力测试 n 请求 c 并发 |
| `help` | 帮助 |
| `quit` | 退出 |

VK 参考：A-Z=`41`-`5A`、Space=`20`、Enter=`0D`、Backspace=`08`、Esc=`1B`
修饰键：Shift=1、Ctrl=2、Alt=4（可叠加）

### 输出字段

```
status=0 rtt=74us ascii=0 composing=1
  preedit: nihao
  candidates (5):
    1. 你好 <===
    2. 拟好
```

| 字段 | 说明 |
|------|------|
| `status` | 0=OK, 1=未知命令, 2=无效会话, 101=引擎处理失败 |
| `rtt` | 往返延迟（微秒） |
| `preedit` | 拼音缓冲区 |
| `candidates` | 候选列表，`<===` 为高亮项 |
| `commit` | 上屏文本 |

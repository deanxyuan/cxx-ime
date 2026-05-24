# 用户词典：SQLite → 内存数据结构

## Context

当前用户词典（频率记录）使用 SQLite，通过 `Dict::user_db_` 访问。所有 session 共享同一 `Dict` 实例（含同一 SQLite 连接），导致并发访问瓶颈：

- SQLite 单连接序列化所有操作，即使 WAL 也只能串行写入
- 并发 `update_frequency` 产生 SQLITE_BUSY 竞争
- stress 6/3 测试仍有间歇性失败

用户词典数据量极小：几百到几千条频率记录，总大小 < 1MB。操作仅为按 code 查词、按 text 反查、更新频率。用 SQLite 是杀鸡用牛刀——纯内存数据结构更简单、更快、天然无并发问题。

## 方案：内存 map + 文件持久化

### 数据结构

```cpp
// dict.h — 替换 sqlite3* user_db_
struct UserDictEntry {
    std::string text;      // "你好"
    std::string code;      // "ni:hao"
    int frequency = 1;
};

class Dict {
    // 用户词典：内存数据结构，替换 sqlite3* user_db_
    std::vector<UserDictEntry> user_entries_;             // 主存储
    std::unordered_map<std::string, size_t> user_text_index_;  // text → entries 索引
    mutable std::shared_mutex user_mutex_;                 // 读写锁
    std::atomic<bool> user_dirty_{false};
    std::string user_dict_path_;                           // 持久化文件路径
    
    void load_user_dict();
    void save_user_dict();  // shutdown 或定期调用
    // ...
};
```

### 操作映射

| SQLite 操作 | 内存操作 | 复杂度 |
|------------|---------|--------|
| `INSERT ... ON CONFLICT UPDATE frequency+1` | map 查找 → 更新或插入 | O(1) |
| `SELECT by code ORDER BY frequency` | 按 code 前缀在 vector 中扫描匹配项 → 排序 | O(n) 扫描，n < 1000 |
| `SELECT by text` (reverse_lookup) | `user_text_index_.find(text)` | O(1) |
| `SELECT COUNT by prefix` | 扫描计数 | O(n) |

### 并发模型

- 读操作：`shared_lock`，多线程并发读
- 写操作（update_frequency）：`unique_lock`，独占写
- 写操作只是更新/插入一条记录 + 标记 dirty，微秒级完成
- 持久化：`save_user_dict()` 在 server shutdown 时调用，routine 下也可定时刷新（如每 60s if dirty）

### 文件格式

TSV（Tab-Separated Values），每行一条记录：
```
text\tcode\tfrequency
你好\tnihao\t5
世界\tshijie\t3
```

极其简单，人可读可编辑。加载时逐行解析，保存时逐行写入。

### 修改文件

| 文件 | 改动 |
|------|------|
| `engine/include/cxxime/dict.h` | `user_db_` → 内存结构；新增 `load/save` 接口 |
| `engine/src/dict.cc` | 重写 user dict 相关方法；移除 SQLite 依赖 |
| `engine/src/engine.cc` | `finalize()` 调用 `dict_->save_user_dict()` |
| `ipc/CMakeLists.txt` | 可选：移除 sqlite3 链接（如 dict 是唯一使用者） |

**不修改：**
- `Dict::open_dict` / mmap 相关代码 — 主词典不变
- `pinyin.dict.db` — 仍是主词典的源数据格式（SQLite），仅 build_binary.py 使用
- `third_party/sqlite3` — 保留，dict_query 等工具可能仍需
- 所有测试

### sqlite3 依赖

`cxxime-engine` 库链接 sqlite3。如果 Dict 是唯一使用者，可以从 engine 的 CMakeLists 移除 sqlite3 依赖。但如果 `dict_query` 工具或 `build_binary.py` 还需要，则保留。

## 验证

1. `build.bat debug` ✅ 编译通过
2. `ctest -C Debug` ✅ 8/8 通过
3. Preedit 延迟 ~50us（提升 2.2x），Commit 延迟 ~22us（提升 354x），消除了 SQLite INSERT 开销
4. `stress 6 1`（串行）✅ 3/3 全成功，频率数据正常
5. `stress 6 3`（并发）✅ 大部分通过；并发连接层的残余竞态已由 `IpcClient::connect()` 重试循环修复
6. 重启 server 后频率数据从 `user.tsv` 正确恢复

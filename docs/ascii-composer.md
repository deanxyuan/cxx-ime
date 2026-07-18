# 中英文切换机制 — AsciiComposer

描述 CxxIME 的 AsciiComposer 模块：职责、配置、切换行为、状态同步链路。

---

## 1. 职责与定位

AsciiComposer 是 Engine 内部的一个**修饰键追踪与模式切换组件**，负责：

- 追踪 Shift / Ctrl / Alt / Win 修饰键的按下与释放状态
- 在修饰键释放时根据配置的切换风格执行中英文模式切换
- 处理 CapsLock 的 ASCII 覆盖（overlay）机制
- 管理 `ascii_mode_` 和 `temporary_ascii_` 状态供 Engine 决策

**关键设计约束：**

- AsciiComposer 不消费按键事件 — `process_key()` 始终返回 `false`
- 模式切换产生的结果通过 `Context` 传达（设置 `committed_text` 或清空 composition）
- 真正的 ASCII 模式字符处理（字母直接上屏、空格、标点）在 `Engine::process_key()` 的 Phase 2.4–2.5 完成

### 在 Engine Pipeline 中的位置

```
Engine::process_key()
  ├── Phase 0: 初始化 trace
  ├── Phase 1: 重置 scratch 缓冲区
  ├── Phase 2: ascii_composer_.process_key()  ← 修饰键追踪 + 模式切换
  │   └── 可能设置 context_.committed_text
  ├── Phase 2.3: 键盘快捷键（Shift+Space 全角/半角切换, Ctrl+. 中英文标点切换）
  ├── Phase 2.4: 未配置 CapsLock 时保持原生 OS 行为（字母翻转大小写直接上屏）
  ├── Phase 2.5: 英文全宽数字拦截
  ├── ASCII 模式处理：字母/空格/回车/标点直接上屏或穿透
  ├── Phase 4: processor_->process_key()（PinyinProcessor）
  └── Phase 5: translator_->translate()（候选查询）
```

---

## 2. 配置项

配置位于 `default.json` 的 `ascii_composer.switch_key` 节，在 `Config` 结构体中对应 `std::unordered_map<std::string, std::string> ascii_switch_key`（`config.h:55`）。

### 默认配置

```json
{
    "ascii_composer": {
        "switch_key": {
            "Shift_L": "code",
            "Shift_R": "set_ascii_mode",
            "Control_L": "noop",
            "Control_R": "noop",
            "Caps_Lock": "clear"
        }
    }
}
```

### 可配置的按键

| 配置键名 | 对应 Windows VK |
|----------|-----------------|
| `Shift_L` | `VK_LSHIFT` |
| `Shift_R` | `VK_RSHIFT` |
| `Shift` | `VK_SHIFT`（左右 Shift 通用回退） |
| `Control_L` | `VK_LCONTROL` |
| `Control_R` | `VK_RCONTROL` |
| `Control` | `VK_CONTROL`（通用回退） |
| `Caps_Lock` | `VK_CAPITAL` |
| `Alt_L` | `VK_LMENU` |
| `Alt_R` | `VK_RMENU` |
| `Alt` | `VK_MENU` |
| `Super_L` | `VK_LWIN` |
| `Super_R` | `VK_RWIN` |

### 切换风格枚举及语义

定义在 `ascii_composer.h:15-24`：

| 枚举值 | 配置字符串 | 行为 |
|--------|-----------|------|
| `NOOP` | `"noop"` | 禁用该键的切换功能 |
| `INLINE_ASCII` | `"inline_ascii"` | 切换模式，若正在组合则设 `temporary_ascii_`，组合结束时自动恢复中文 |
| `CODE` | `"code"` | 提交原始编码（`pinyin_buffer`）后切换模式 |
| `CLEAR` | `"clear"` | 清除当前组合后切换模式 |
| `SET_ASCII_MODE` | `"set_ascii_mode"` | 强制切换到英文模式（单向） |
| `UNSET_ASCII_MODE` | `"unset_ascii_mode"` | 强制切换到中文模式（单向） |
| `CANDIDATE` | `"candidate"` | 提交第一个候选词后切换模式 |
| `APPEND` | `"append"` | 仅用于 CapsLock：不切换模式，字母处理延迟到 Engine Phase 2.4 |

**CapsLock 特殊处理**（`ascii_composer.cc:65-71`）：若配置为 `inline_ascii` / `set_ascii_mode` / `unset_ascii_mode`，自动降级为 `clear`，因为这些风格与 CapsLock 的翻转特性不兼容。

### TSF 通用键码回退

TSF 框架有时发送通用 `VK_SHIFT` 而非 `VK_LSHIFT`/`VK_RSHIFT`（类似地 `VK_CONTROL`/`VK_MENU`）。`get_binding()` 方法（`ascii_composer.cc:156-171`）在找不到精确绑定时回退到左键绑定。

---

## 3. 关键行为

### 3.1 修饰键追踪与切换

`process_key()` 核心逻辑（`ascii_composer.cc:77-154`）：

```
process_key(key_code, is_key_up, ctx, caps_lock):
    1. 多修饰键同时按下 → 重置所有追踪位，不切换
    2. CapsLock → apply_caps_lock_overlay()
    3. Shift/Ctrl/Alt/Win 按下 → 设置对应追踪位
    4. Shift/Ctrl/Alt/Win 释放 → 调用 toggle_mode()
    5. 非修饰键 → 取消待切换状态（清除所有追踪位）
    6. 始终返回 false
```

切换无超时限制：`toggle_mode()` 在修饰键释放时无条件调用（按下期间未夹杂其他按键即触发）。

### 3.2 切换执行

`toggle_mode()`（`ascii_composer.cc:173-235`）根据风格执行操作：

- **CODE**: 正在组合时，`committed_text = pinyin_buffer`，然后切换模式
- **CANDIDATE**: 正在组合时，`committed_text = candidates[0].text`，清空 buffer，然后切换
- **CLEAR**: 正在组合时 `ctx.reset()`，然后切换模式
- **INLINE_ASCII**: 切换模式，若正在组合则设 `temporary_ascii_ = true`
- **SET_ASCII_MODE / UNSET_ASCII_MODE**: 单向切换，不依赖当前模式

`set_ascii_mode_from_switch()`（`ascii_composer.cc:237-241`）设置新模式并清理临时状态：
```cpp
void AsciiComposer::set_ascii_mode_from_switch(bool mode) {
    ascii_mode_ = mode;
    temporary_ascii_ = false;
    caps_lock_overlay_active_ = false;
}
```

### 3.3 临时英文态（INLINE_ASCII）

INLINE_ASCII 风格的切换会设置 `temporary_ascii_`，Engine 在以下条件自动恢复中文模式：

- 字母键上屏后：`ascii_composer_.set_ascii_mode(false)`（`engine.cc:217`）
- 组合结束时（`COMMITTED + is_temporary_ascii()`）：同上（`engine.cc:309-311`）

### 3.4 CapsLock Overlay

`apply_caps_lock_overlay()`（`ascii_composer.cc:243-299`）：

```
CapsLock 灯亮 → 进入 overlay：
  1. 记录当前 ascii_mode 到 ascii_mode_before_caps_lock_
  2. 设置 caps_lock_overlay_active_ = true
  3. 强制 ascii_mode_ = true
  4. 根据 CapsLock 配置风格执行动作（CODE/CLEAR/CANDIDATE/APPEND）

CapsLock 灯灭 → 退出 overlay：
  1. 恢复 ascii_mode_ = ascii_mode_before_caps_lock_
  2. 清除 caps_lock_overlay_active_ 和 temporary_ascii_
```

CapsLock overlay 激活时，修饰键切换被抑制（`ascii_composer.cc:118-124`），防止 CapsLock 英文态下误切模式。

### 3.5 成员变量

定义在 `ascii_composer.h:50-58`：

| 成员 | 类型 | 初始值 | 用途 |
|------|------|--------|------|
| `bindings_` | `unordered_map<uint32_t, AsciiModeSwitchStyle>` | 空 | 按键→切换风格映射 |
| `ascii_mode_` | `bool` | `false` | 当前是否英文模式 |
| `temporary_ascii_` | `bool` | `false` | 临时英文态（INLINE_ASCII） |
| `caps_lock_overlay_active_` | `bool` | `false` | CapsLock overlay 激活中 |
| `ascii_mode_before_caps_lock_` | `bool` | `false` | CapsLock 前的模式（用于恢复） |
| `shift_pressed_` | `bool` | `false` | 左/右 Shift 按下 |
| `ctrl_pressed_` | `bool` | `false` | 左/右 Ctrl 按下 |
| `alt_pressed_` | `bool` | `false` | 左/右 Alt 按下 |
| `win_pressed_` | `bool` | `false` | Win 键按下 |

---

## 4. 与 TSF / Server 的状态同步链路

### 4.1 数据流向

```
TSF 按键 → IPC → ServerApp::handle_request()
  → SessionManager::process_key()
    → align_session_to_global()      // 全局可见状态 → Engine
    → engine.process_key()
      → ascii_composer_.process_key()
        → 可能修改 context + ascii_mode_
    → 读取 engine.ascii_composer().is_ascii_mode()
    → 更新 GlobalVisibleState.base_chinese_mode
  → 构造 ProcessKeyResult
    → ime_status 含 chinese_mode/caps_lock/full_shape/...
  → IPCResponse
    → ascii_mode = !ime_status.chinese_mode
    → ime_status 完整字段
  → TSF 更新语言栏图标 + 状态窗口
```

### 4.2 GlobalVisibleState 与 align_session_to_global

`SessionManager::GlobalVisibleState`（`session_manager.h:126-129`）包含：

```cpp
struct GlobalVisibleState {
    cxxime::ImeStatus status;          // chinese_mode, caps_lock, full_shape, chinese_punct, input_mode, revision
    bool base_chinese_mode = true;     // CapsLock overlay 下的"真实"模式
};
```

`align_session_to_global()`（`session_manager.cc:315-330`）在每次处理按键前同步 session：

1. 根据 `base_chinese_mode` 设置 engine 的 ascii_mode（`set_ascii_mode(!base_chinese_mode)`）
2. 根据 `status.caps_lock` 调用 `sync_caps_lock()` 应用或解除 overlay
3. 同步 `input_mode`（输入模式切换）
4. 处理模式切换后的状态修复（当 engine 不支持某模式时回退）

### 4.3 commit_global_state 的 CapsLock 合成

`commit_global_state()`（`session_manager.cc:302-313`）在发布状态时考虑 CapsLock：

```cpp
next.status.chinese_mode = next.status.caps_lock ? false : next.base_chinese_mode;
```

即：CapsLock 灯亮时 `chinese_mode` 强制为 `false`，灯灭时等于 `base_chinese_mode`。

### 4.4 ServerApp 的 ascii_mode 输出

`server_app.cc:155`：
```cpp
response.ascii_mode = !r.ime_status.chinese_mode;
```

`ascii_mode` 从 `ImeStatus.chinese_mode` 取反得到。IPCResponse 同时包含 `ime_status` 完整字段（`ipc_protocol.h:96`）。

### 4.5 Ctrl+Space 保留键

Ctrl+Space 仍通过 TSF Preserved Key 机制注册（`text_service.cpp:943`），触发 `SessionManager::toggle_chinese()` 而非走 AsciiComposer 流程。

### 4.6 TSF 侧状态同步

TSF 的 `_chinese_mode` 在 `_sync_ime_status()` 中从 `ImeStatus.chinese_mode` 更新，不再独立管理模式切换逻辑。

---

## 5. 测试覆盖

无独立的 AsciiComposer 单元测试文件。AsciiComposer 的 CapsLock overlay 行为通过集成测试 `session_manager_status_test.cc` 覆盖，包括：

- `sync_caps_lock_sets_current_state` — CapsLock 状态同步
- `sync_caps_lock_enables_ascii_overlay` — CapsLock 激活后字母直接上屏
- `first_key_with_caps_lock_on_enables_ascii_overlay` — 首次按键时 CapsLock 已开
- `caps_lock_key_off_restores_chinese_overlay` — CapsLock 关灯恢复中文
- `caps_lock_key_up_does_not_override_key_down_state` — key-up 不覆盖 key-down 状态
- `caps_lock_global_overlay_restores_base_mode` — 跨 session 的 CapsLock 同步

普通 Shift/Ctrl 切换行为通过 `session_manager_integration_test.cc` 覆盖（组合中的键处理结果验证）。

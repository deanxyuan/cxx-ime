# CxxIME 中英文切换机制改造方案

**日期:** 2026-05-24
**状态:** 待评审
**目标:** 将当前硬编码的 Shift 切换改为 librime 风格的可配置 `ascii_composer` 机制

---

## 1. 现状分析

### 1.1 当前实现（text_service.cpp:166-182）

```cpp
// Shift key alone toggles Chinese/English mode (when not composing)
if ((wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) && !_composing) {
    _chinese_mode = !_chinese_mode;
    return S_OK;
}

// Ctrl+Space also toggles mode
if (wParam == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000) && !_composing) {
    _chinese_mode = !_chinese_mode;
    *pfEaten = TRUE;
    return S_OK;
}
```

**问题：**
- 硬编码行为，无法通过配置修改
- 不区分左右 Shift（librime 支持不同行为）
- 无切换样式（inline_ascii / commit_text 等）
- 无 500ms 超时防误触机制
- 模式状态仅在 TSF 层（`_chinese_mode`），引擎层无感知
- 无法支持 CapsLock 切换

### 1.2 librime 的 ascii_composer 机制

librime 通过 `AsciiComposer` processor 实现可配置的中英文切换：

```yaml
# schema.yaml 或 default.yaml
ascii_composer:
  switch_key:
    Shift_L: inline_ascii      # 左Shift: 临时英文（组合中生效，退出组合自动恢复）
    Shift_R: commit_text       # 右Shift: 提交已选候选并切换
    Control_L: noop            # 左Ctrl: 禁用
    Control_R: noop            # 右Ctrl: 禁用
    Caps_Lock: clear           # CapsLock: 清除组合并切换
```

**切换样式（6种）：**

| 样式 | 行为 |
|------|------|
| `inline_ascii` | 临时 ASCII 模式，当前编码变为英文输入，提交后自动恢复中文 |
| `commit_text` | 提交已选中的候选文字，然后切换 |
| `commit_code` | 提交当前原始编码（不选候选），然后切换 |
| `clear` | 清除当前组合，然后切换 |
| `set_ascii_mode` | 强制切换到 ASCII 模式（单向） |
| `unset_ascii_mode` | 强制切换到中文模式（单向） |
| `noop` | 禁用该键的切换功能 |

**核心特性：**
- 500ms 超时：单独按下修饰键并在 500ms 内释放才触发
- 防误触：按下修饰键后又按其他键则不触发切换
- 左右分离：不同修饰键可绑定不同行为

---

## 2. 改造方案

### 2.1 架构设计

```
┌─────────────────────────────────────────────────────────────────┐
│                         TSF DLL 层                              │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  TextService                                            │   │
│  │  - 不再直接处理 Shift 切换逻辑                           │   │
│  │  - 将所有按键（含修饰键 up/down）转发给引擎              │   │
│  │  - 从 IPC 响应中获取 ascii_mode 状态                    │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │ IPC
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        Server 层                               │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  ServerApp                                              │   │
│  │  - 转发按键事件到引擎                                    │   │
│  │  - 返回 ascii_mode 状态                                 │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        Engine 层                               │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Engine                                                 │   │
│  │  ┌─────────────────────────────────────────────────┐   │   │
│  │  │  AsciiComposer (新增)                            │   │   │
│  │  │  - 加载 ascii_composer/switch_key 配置           │   │   │
│  │  │  - 处理修饰键 up/down 事件                       │   │   │
│  │  │  - 500ms 超时检测                                │   │   │
│  │  │  - 管理 ascii_mode 状态                          │   │   │
│  │  │  - 执行切换样式（inline/commit/clear 等）        │   │   │
│  │  └─────────────────────────────────────────────────┘   │   │
│  │  ┌─────────────────────────────────────────────────┐   │   │
│  │  │  PinyinProcessor (修改)                          │   │   │
│  │  │  - 在处理前检查 ascii_mode                       │   │   │
│  │  │  - ascii_mode=true 时跳过拼音处理               │   │   │
│  │  └─────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 模块改动

#### 2.2.1 新增：`engine/include/cxxime/ascii_composer.h`

```cpp
#ifndef CXXIME_ASCII_COMPOSER_H_
#define CXXIME_ASCII_COMPOSER_H_

#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <string>

namespace cxxime {

class Context;
class Config;

// 切换样式（与 librime 对齐）
enum class AsciiModeSwitchStyle {
    NOOP,              // 禁用
    INLINE_ASCII,      // 临时英文，退出组合自动恢复
    COMMIT_TEXT,       // 提交已选候选
    COMMIT_CODE,       // 提交原始编码
    CLEAR,             // 清除组合
    SET_ASCII_MODE,    // 强制英文
    UNSET_ASCII_MODE,  // 强制中文
};

// Windows 虚拟键码（与 librime 的 XK_* 对齐）
enum class ModifierKey : uint32_t {
    SHIFT_L   = 0xA0,
    SHIFT_R   = 0xA1,
    CTRL_L    = 0xA2,
    CTRL_R    = 0xA3,
    CAPS_LOCK = 0x14,
};

class AsciiComposer {
public:
    // 加载配置
    void load_config(const Config& config);

    // 处理按键事件，返回 true 表示事件已被消费
    // key_code: Windows VK_* 虚拟键码
    // is_key_up: 是否是按键释放
    // ascii_mode: 当前 ascii_mode 状态（输出）
    bool process_key(uint32_t key_code, bool is_key_up, bool& ascii_mode);

    // 获取当前 ascii_mode 状态
    bool is_ascii_mode() const { return ascii_mode_; }

    // 设置 ascii_mode 状态（用于 global_ascii 同步）
    void set_ascii_mode(bool mode) { ascii_mode_ = mode; }

private:
    // 执行切换
    void switch_mode(bool new_mode, AsciiModeSwitchStyle style, Context& ctx);

    // 配置：按键 -> 切换样式
    std::unordered_map<uint32_t, AsciiModeSwitchStyle> bindings_;

    // CapsLock 配置
    AsciiModeSwitchStyle caps_lock_style_ = AsciiModeSwitchStyle::NOOP;
    bool good_old_caps_lock_ = false;

    // 状态
    bool ascii_mode_ = false;
    bool shift_pressed_ = false;
    bool ctrl_pressed_ = false;
    bool toggle_with_caps_ = false;

    // 500ms 超时
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    TimePoint toggle_expired_;
    static constexpr int TOGGLE_TIMEOUT_MS = 500;
};

} // namespace cxxime

#endif // CXXIME_ASCII_COMPOSER_H_
```

#### 2.2.2 新增：`engine/src/ascii_composer.cc`

实现要点：
- 解析配置中的 `ascii_composer/switch_key`
- 处理修饰键的 down/up 事件
- 实现 500ms 超时检测
- 根据切换样式执行相应操作

#### 2.2.3 修改：`engine/include/cxxime/engine.h`

```cpp
class Engine {
public:
    // ... 现有接口 ...

    // 新增：获取 ascii_composer
    AsciiComposer& ascii_composer() { return ascii_composer_; }
    const AsciiComposer& ascii_composer() const { return ascii_composer_; }

private:
    // ... 现有成员 ...
    AsciiComposer ascii_composer_;  // 新增
};
```

#### 2.2.4 修改：`engine/src/engine.cc`

```cpp
bool Engine::process_key(const KeyEvent& event) {
    // 1. 先让 AsciiComposer 处理（可能消费修饰键事件）
    bool ascii_mode = ascii_composer_.is_ascii_mode();
    if (ascii_composer_.process_key(event.keycode, event.is_key_up, ascii_mode)) {
        // 事件被 AsciiComposer 消费（模式切换）
        return true;
    }

    // 2. 如果是 ASCII 模式，跳过拼音处理
    if (ascii_mode) {
        return false;  // 直接上屏
    }

    // 3. 正常拼音处理
    // ... 现有逻辑 ...
}
```

#### 2.2.5 修改：`shared/include/cxxime/ipc_protocol.h`

```cpp
struct IPCResponse {
    uint32_t status = 0;
    char commit_text[256] = {};
    char preedit[256] = {};
    uint32_t candidate_count = 0;
    char candidates[10][64] = {};
    uint32_t highlighted = 0;
    bool ascii_mode = false;      // 新增：当前是否为 ASCII 模式
    bool composing = false;       // 新增：当前是否在组合中
};
```

#### 2.2.6 修改：`tsf/src/text_service.cpp`

```cpp
// 删除：硬编码的 Shift 切换逻辑（第 166-182 行）

// 修改：OnKeyDown 不再特殊处理 Shift
STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;

    // 不再在这里处理 Shift 切换，全部交给引擎层

    uint32_t modifiers = _get_modifiers();
    cxxime::IPCResponse response = {};
    bool ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response);

    // ... 处理响应 ...

    // 新增：从响应中更新 ascii_mode 状态
    if (ok) {
        _chinese_mode = !response.ascii_mode;
    }

    return S_OK;
}

// 修改：_should_eat_key 根据引擎返回的 ascii_mode 决定
bool TextService::_should_eat_key(WPARAM vk) const {
    // ASCII 模式下，字母键不拦截
    if (!_chinese_mode && !_composing) {
        return false;
    }
    // ... 其余逻辑不变 ...
}
```

#### 2.2.7 修改：`tsf/src/text_service.h`

```cpp
class TextService : ... {
    // 删除：不再需要保留 Ctrl+Space 的 preserved key
    // HRESULT _register_preserved_key();  // 移除

    // 保留 _chinese_mode，但由引擎层驱动
    bool _chinese_mode = true;
};
```

#### 2.2.8 修改：`server/src/server_app.cc`

```cpp
case cxxime::IPCCommand::PROCESS_KEY: {
    auto* engine = session_mgr_.get_engine(request.session_id);
    if (!engine) {
        response.status = 1;
        break;
    }

    cxxime::KeyEvent event;
    event.keycode = request.key_code;
    event.modifiers = request.modifiers;
    event.is_key_up = false;

    auto result = engine->process_key(event);

    // 新增：始终返回 ascii_mode 状态
    response.ascii_mode = engine->ascii_composer().is_ascii_mode();
    response.composing = !engine->context().pinyin_buffer.empty();

    // ... 其余逻辑不变 ...
}
```

### 2.3 配置文件格式

#### 2.3.1 修改：`data/default.json`

```json
{
    "schema": {
        "name": "CxxIME",
        "version": "1.0",
        "description": "CxxIME default pinyin schema"
    },
    "engine": {
        "page_size": 9,
        "max_pinyin_length": 64
    },
    "ascii_composer": {
        "switch_key": {
            "Shift_L": "inline_ascii",
            "Shift_R": "commit_text",
            "Control_L": "noop",
            "Control_R": "noop"
        },
        "good_old_caps_lock": false
    },
    "style": {
        "font_face": "Microsoft YaHei UI",
        "font_point": 14,
        "layout": "horizontal",
        "candidate_count": 9,
        "inline_preedit": true,
        "preedit_type": "composition"
    },
    "theme": "light"
}
```

#### 2.3.2 配置字段说明

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `ascii_composer.switch_key.Shift_L` | string | `"inline_ascii"` | 左 Shift 切换样式 |
| `ascii_composer.switch_key.Shift_R` | string | `"commit_text"` | 右 Shift 切换样式 |
| `ascii_composer.switch_key.Control_L` | string | `"noop"` | 左 Ctrl 切换样式 |
| `ascii_composer.switch_key.Control_R` | string | `"noop"` | 右 Ctrl 切换样式 |
| `ascii_composer.switch_key.Caps_Lock` | string | (无默认) | CapsLock 切换样式 |
| `ascii_composer.good_old_caps_lock` | bool | `false` | 是否保留 CapsLock 的大小写切换功能 |

**切换样式可选值：**
- `"inline_ascii"` — 临时英文，退出组合自动恢复
- `"commit_text"` — 提交已选候选并切换
- `"commit_code"` — 提交原始编码并切换
- `"clear"` — 清除组合并切换
- `"set_ascii_mode"` — 强制英文（单向）
- `"unset_ascii_mode"` — 强制中文（单向）
- `"noop"` — 禁用

---

## 3. 关键实现细节

### 3.1 修饰键处理算法（与 librime 对齐）

```
process_key(key_code, is_key_up):
    // 多修饰键组合时忽略（如 Shift+Ctrl+A）
    if (shift_pressed + ctrl_pressed > 1):
        shift_pressed = ctrl_pressed = false
        return false

    // CapsLock 处理
    if (key_code == VK_CAPITAL):
        return handle_caps_lock(is_key_up)

    // 判断是否是修饰键
    is_shift = (key_code == VK_LSHIFT || key_code == VK_RSHIFT)
    is_ctrl = (key_code == VK_LCONTROL || key_code == VK_RCONTROL)

    if (is_shift || is_ctrl):
        if (is_key_up):
            // 按键释放
            if (shift_pressed || ctrl_pressed):
                // 检查是否在 500ms 内
                if (now < toggle_expired):
                    toggle_mode(key_code)
                shift_pressed = ctrl_pressed = false
                return false
        else:
            // 按键按下（首次）
            if (!(shift_pressed || ctrl_pressed)):
                if (is_shift): shift_pressed = true
                if (is_ctrl): ctrl_pressed = true
                toggle_expired = now + 500ms
        return false  // 修饰键事件不消费

    // 其他键：清除修饰键状态
    shift_pressed = ctrl_pressed = false
    return false
```

### 3.2 inline_ascii 模式实现

当切换样式为 `inline_ascii` 时：
1. 如果当前在组合中，将 `ascii_mode` 设为 `true`
2. 后续按键直接作为英文字符输入（不经过拼音处理）
3. 当组合结束（提交或清除）时，自动将 `ascii_mode` 恢复为 `false`

```cpp
void AsciiComposer::switch_mode(bool new_mode, AsciiModeSwitchStyle style, Context& ctx) {
    if (ctx.is_composing()) {
        if (style == AsciiModeSwitchStyle::INLINE_ASCII) {
            // 临时英文模式：记住需要恢复
            need_restore_ = true;
        } else if (style == AsciiModeSwitchStyle::COMMIT_TEXT) {
            // 提交当前候选
            // ... 触发提交逻辑 ...
        } else if (style == AsciiModeSwitchStyle::CLEAR) {
            ctx.reset();
        }
    }
    ascii_mode_ = new_mode;
}
```

### 3.3 TSF 层与引擎层状态同步

```
TSF 层 (_chinese_mode)  ←→  Engine 层 (ascii_mode_)
```

- 按键处理后，从 IPC 响应中获取 `ascii_mode` 状态
- 不再由 TSF 层自行维护切换逻辑
- 语言栏图标根据 `ascii_mode` 状态显示

---

## 4. 改动文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `engine/include/cxxime/ascii_composer.h` | 新增 | AsciiComposer 类定义 |
| `engine/src/ascii_composer.cc` | 新增 | AsciiComposer 实现 |
| `engine/include/cxxime/engine.h` | 修改 | 添加 AsciiComposer 成员 |
| `engine/src/engine.cc` | 修改 | 集成 AsciiComposer 处理流程 |
| `engine/src/config.cc` | 修改 | 解析 ascii_composer 配置 |
| `shared/include/cxxime/ipc_protocol.h` | 修改 | 响应中添加 ascii_mode 字段 |
| `server/src/server_app.cc` | 修改 | 返回 ascii_mode 状态 |
| `tsf/src/text_service.cpp` | 修改 | 移除硬编码切换逻辑 |
| `tsf/src/text_service.h` | 修改 | 移除 preserved key 相关 |
| `data/default.json` | 修改 | 添加 ascii_composer 配置 |

---

## 5. 测试计划

### 5.1 单元测试

| 测试 | 说明 |
|------|------|
| `ascii_composer_test.cc` | 测试各种切换样式 |
| | - Shift_L inline_ascii: 组合中按左Shift，输入英文，提交后恢复中文 |
| | - Shift_R commit_text: 组合中按右Shift，提交候选并切换 |
| | - 500ms 超时: 按住 Shift 超过 500ms 释放，不触发切换 |
| | - 防误触: Shift+A 不触发切换 |
| | - CapsLock 切换 |
| | - noop 禁用 |

### 5.2 集成测试

| 场景 | 预期 |
|------|------|
| 输入拼音 → 按左Shift → 输入英文 → 按空格 | 英文上屏，恢复中文模式 |
| 输入拼音 → 按右Shift | 已选候选上屏，切换到英文模式 |
| 英文模式下按 Ctrl+Space | 切换回中文模式 |
| 配置 Shift_L=noop | 左 Shift 不触发切换 |

### 5.3 配置测试

| 配置 | 预期 |
|------|------|
| 缺少 ascii_composer 字段 | 使用默认值（Shift_L=inline_ascii, Shift_R=commit_text） |
| 配置无效值 | 忽略该绑定，使用默认值 |
| 配置 set_ascii_mode | Shift 只能切到英文，不能切回 |

---

## 6. 兼容性考虑

### 6.1 向后兼容

- 默认配置与当前行为相似（Shift 切换中英文）
- 但左右 Shift 行为不同（当前是相同行为）
- 如果用户需要旧行为，可配置 `Shift_L: set_ascii_mode, Shift_R: set_ascii_mode`

### 6.2 与 librime 的差异

| 特性 | librime | CxxIME (改造后) |
|------|---------|-----------------|
| 配置格式 | YAML | JSON |
| 处理位置 | 引擎层 Processor | 引擎层 AsciiComposer |
| inline_ascii | 支持 | 支持 |
| 500ms 超时 | 支持 | 支持 |
| 左右分离 | 支持 | 支持 |
| CapsLock | 支持 | 支持 |

---

## 7. 实施步骤

1. **Phase 1: 核心实现**
   - 新增 `AsciiComposer` 类
   - 实现配置加载
   - 实现基本切换逻辑（noop, set/unset）

2. **Phase 2: 高级特性**
   - 实现 500ms 超时
   - 实现 inline_ascii 模式
   - 实现 commit_text/commit_code/clear 样式

3. **Phase 3: 集成**
   - 修改 Engine 集成 AsciiComposer
   - 修改 IPC 协议
   - 修改 TSF 层移除硬编码逻辑

4. **Phase 4: 测试**
   - 单元测试
   - 集成测试
   - 配置测试

# CxxIME 设置指南

## 概述

本文档覆盖 CxxIME 的两种配置方式：**设置窗口**（`cxxime-settings.exe`，面向日常使用）与**配置文件**（`default.json`，面向手工编辑，见文末「配置文件格式」——部分取值仅能通过配置文件设置）。

设置窗口采用 Win32 原生控件构建。窗口左侧为导航栏，右侧为对应设置面板，底部提供"确定"、"取消"、"应用"三个按钮。

- **确定**：保存配置并关闭窗口
- **取消**：不保存直接关闭
- **应用**：保存配置但不关闭窗口，方便继续调整

配置文件保存位置：`%USERPROFILE%\cxxime\default.json`

设置窗口启动后自动加载程序目录下的 `default.json`（默认值），再叠加用户目录下的 `default.json`（用户自定义值）。

---

## 面板一：输入

### 输入模式

**配置项**：`engine.input_mode`（0=拼音，1=五笔，2=混输）

| 选项 | 说明 |
|------|------|
| 拼音 | 全拼输入，如 `nihao` → 你好 |
| 五笔 | 86 版五笔字型输入 |
| 混输 | 拼音和五笔混合输入，自动识别编码类型 |

切换输入模式后，点"确定"或"应用"会立即通知服务端切换，无需重启输入法。

### 内联显示

**配置项**：`style.inline_preedit`（布尔值）

控制 TSF 内联文本是否写入应用文本区（如记事本）。

| 状态 | 行为 |
|------|------|
| 关闭（默认） | 应用文本区无内容。候选窗口显示拼音 + 候选列表（composition 模式） |
| 开启 | 应用文本区显示编码或首选候选。候选窗口仅显示候选列表（OFF 模式） |

### 显示内容

**配置项**：`style.preedit_type`（枚举：`composition` / `preview`）

仅在"内联显示"开启时生效。控制内联文本的内容：

| 选项 | 应用文本区显示 | 示例 |
|------|---------------|------|
| 编码 (ni'hao) | 原始拼音 | `ni'hao` |
| 首选 (你好) | 第一个候选词 | `你好` |

当"内联显示"关闭时，此选项不生效，但选择会被保存。重新开启"内联显示"后立即生效。

**行为矩阵**：

| 内联显示 | 显示内容 | 候选窗口 | 应用文本区 |
|----------|----------|----------|------------|
| 关 | 编码 | 上=拼音，下=候选 | 无 |
| 关 | 首选 | 上=拼音，下=候选 | 无 |
| 开 | 编码 | 仅候选列表 | `ni'hao` |
| 开 | 首选 | 仅候选列表 | `你好` |

### 模糊拼音

**配置项**：`engine.fuzzy_pinyin`（布尔值）

启用后，部分声母/韵母可互通，例如：

- `z`/`zh`、`c`/`ch`、`s`/`sh`
- `n`/`l`、`f`/`h`、`r`/`l`
- `an`/`ang`、`en`/`eng`、`in`/`ing`

适合发音不标准的用户。关闭后要求精确拼音。

### 候选数量

**配置项**：`engine.page_size`（整数，出厂配置 7）

每页显示的候选词数量，范围建议 5–15。设置后按翻页键可查看超出部分。

### 五笔四码上屏

**配置项**：`engine.wubi_auto_commit`（布尔值）

仅在五笔/混输模式下生效。启用后，输入四码且唯一候选时自动上屏，无需按空格。

### 候选学习

**配置项**：`engine.candidate_learning`（布尔值）

控制选词后是否自动学习词频与音节键。启用后选中的候选会被记录到用户词典，后续可通过简拼、混合码等方式命中。

---

## 面板二：候选窗口

### 主题

**配置项**：`theme`（字符串）

从 `themes.json` 中加载的预设配色方案。下拉框列出所有可用主题，选择后预览区实时更新。

内置主题包括 `azure`、`ink`、`dark` 等，每个主题定义以下颜色：

- 背景色、文字色、边框色
- 候选文字色、标签文字色
- 高亮候选文字色、高亮背景色
- 注释文字色、翻页按钮色

### 字体

**配置项**：`style.font_face`（字符串，出厂配置 `Microsoft YaHei UI`）

点击 `...` 按钮打开系统字体选择对话框。可同时修改字体名称和字号。

### 候选字号

**配置项**：`style.font_point`（整数，出厂配置 14，最小 8）

候选词的显示字号（磅值）。与字体选择器联动——在字体对话框中修改字号后自动同步。

### 编码字号

**配置项**：`layout.label_font_point`（整数，出厂配置 0）

候选窗口中拼音编码（preedit）和序号标签的字号。

- `0`：自动比候选字号小 2 磅
- 其他值：使用指定字号

### 布局方向

**配置项**：`style.layout`（字符串：`horizontal` / `vertical`）

| 选项 | 说明 |
|------|------|
| 横向 | 候选词水平排列，一行放满后换行（默认） |
| 纵向 | 候选词垂直排列，每行一个 |

### 渲染后端

**配置项**：`style.render_backend`（字符串：`d2d` / `gdi`）

| 选项 | 说明 |
|------|------|
| D2D | Direct2D + DirectWrite 渲染，支持抗锯齿和 ClearType（默认） |
| GDI | 传统 GDI 渲染，兼容性更好，性能略低 |

### 状态窗口

**配置项**：`status_window.enable`（布尔值）

控制是否显示独立的状态窗口（中/英状态指示器）。关闭后仅通过语言栏图标指示状态。

### 外观预览

面板底部实时显示候选窗口预览，反映当前主题、字体、字号、布局方向的组合效果。预览内容固定为 7 个候选词（你好、您好、昵称、尼采、拟态、腻烦、匿藏），含拼音编码 `ni'hao`。

---

## 面板三：布局参数

该面板提供 13 个数值参数，精细控制候选窗口的尺寸和间距。面板底部有实时预览，修改任意参数后预览立即更新。

### 尺寸参数

| 参数 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 最小宽度 | `layout.min_width` | 160 | 候选窗口最小宽度（像素） |
| 最大宽度 | `layout.max_width` | 0 | 最大宽度，0 表示不限制 |
| 最大高度 | `layout.max_height` | 0 | 最大高度，0 表示不限制 |

### 边距和间距

| 参数 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 水平边距 | `layout.margin_x` | 12 | 窗口内容区左右内边距 |
| 垂直边距 | `layout.margin_y` | 12 | 窗口内容区上下内边距 |
| 间距 | `layout.spacing` | 10 | 编码区与候选区之间的间距 |
| 候选间距 | `layout.candidate_spacing` | 11 | 候选词之间的间距 |

### 高亮区域

| 参数 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 高亮内边距X | `layout.hilite_padding_x` | 5 | 高亮矩形水平内边距 |
| 高亮内边距Y | `layout.hilite_padding_y` | 2 | 高亮矩形垂直内边距 |
| 高亮间距 | `layout.hilite_spacing` | 4 | 高亮区域内标签与文字的间距 |
| 圆角半径 | `layout.round_corner` | 4 | 高亮矩形的圆角半径 |

### 窗口外观

| 参数 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 窗口圆角 | `layout.round_corner_ex` | 4 | 候选窗口整体的圆角半径 |
| 边框宽度 | `layout.border_width` | 1 | 候选窗口边框宽度 |

---

## 面板四：快捷键

控制中英文模式切换的按键行为。每个按键独立配置。

### Shift 键行为

设置界面下拉提供 6 个选项：

| 选项 | 说明 |
|------|------|
| `inline_ascii` | 按 Shift 临时进入英文模式，松开后恢复中文。composing 时直接上屏已输入编码 |
| `code` | 提交编码原文并切换到英文模式 |
| `candidate` | 提交首个候选词并切换到英文模式 |
| `clear` | 清空输入缓冲区并切换到英文模式 |
| `append` | 追加到输入缓冲区，不切换模式 |
| `noop` | 不处理，仅作为修饰键 |

左右 Shift 键可分别配置不同行为。

> 配置文件中还接受 `set_ascii_mode` / `unset_ascii_mode`（单向切换到英文/中文，不提交内容）。出厂配置的 `Shift_R` 即为 `set_ascii_mode`；这两个值暂无法通过设置界面下拉选择，直接编辑配置文件可用。

### Control 键行为

可选项与 Shift 相同。左右 Control 键可分别配置。

### Caps Lock 行为

**配置项**：`ascii_composer.switch_key.Caps_Lock`

CapsLock 键在输入法中的行为模式。可选值：

| 选项 | CapsLock 键 | CapsLock+字母 | 说明 |
|------|------------|---------------|------|
| `clear` | 空闲：切换中/英；composing：清空缓冲区+切英 | 直接上屏大写字母 | 传统行为 |
| `code` | 空闲：切换中/英；composing：提交编码原文+切英 | 直接上屏大写字母 | 保留已输入编码 |
| `candidate` | 空闲：切换中/英；composing：提交首个候选词+切英 | 直接上屏大写字母 | 智能提交 |
| `append` | 切换中/英（composing 时不做处理） | 追加到输入缓冲区 | 适合混合大小写输入 |

**append 模式示例**：

1. 输入 `nihao` → 候选窗口出现
2. 按 CapsLock → 不清空、不切换
3. 输入 `SD` → 缓冲区变为 `nihaoSD`
4. 按空格 → `nihaoSD` 整体上屏

**CapsLock + Shift + 字母**：在所有模式下，CapsLock+Shift 抵消效果，输入小写字母。

> **注**：CapsLock 模式仅影响 A-Z 字母键。数字键、标点键在所有模式下行为一致。若配置文件将 `Caps_Lock` 设为 `inline_ascii` / `set_ascii_mode` / `unset_ascii_mode`（与 CapsLock 的开关性质不兼容），引擎会自动降级为 `clear`。

---

## 面板五：词库

### 数据目录

显示当前数据文件的存储路径：

- **生产环境**：`%USERPROFILE%\cxxime\`
- **开发环境**：项目 `data/` 目录

### 词典文件

| 文件 | 说明 |
|------|------|
| `pinyin.dict.bin` | 拼音二进制词典（堆加载格式，由 SQLite 源词典构建） |
| `wubi86.dict.bin` | 五笔 86 版二进制词典 |
| `user_pinyin.tsv` / `user_wubi.tsv` | 用户自定义词典（TSV 格式，自动学习 + 手动添加） |

### 快捷造词

手动添加用户词条。添加/保存/删除操作后，面板底部状态栏会即时反馈操作结果（如"已新增词条，列表已刷新""已保存修改，列表已刷新""已删除词条，列表已刷新"）。需要输入法服务端正在运行（通过 IPC 通信）。

1. 在"词语"框输入要添加的词（如 `你好世界`）
2. 在"编码"框输入对应的编码（如 `nihaoshijie`）
3. 点击"添加"按钮

添加成功后词条立即生效，写入 `user_pinyin.tsv` 或 `user_wubi.tsv`。如果服务端未运行，会弹出连接失败提示。

### 导入与导出

- **导入**：选择 `.tsv` 文件后，面板状态栏提示"导入完成，列表已刷新"。若服务未能立即重新加载，会弹出警告提示"文件已导入但服务未能立即重新加载"。
- **导出**：先通过 IPC 请求服务端保存内存状态，然后复制到目标路径。若服务端保存失败，导出结果对话框会附加注意说明"服务未能立即保存最新内存状态"。

---

## 面板六：关于

显示 CxxIME 的版本信息和项目链接：

- 版本号：0.1.0
- 许可证：Apache License 2.0
- 项目地址：
  - Gitee: https://gitee.com/shadowyuan/cxx-ime
  - GitHub: https://github.com/deanxyuan/cxx-ime

### 导出诊断包

点击"导出诊断包"按钮后，在后台等待 `collect_diagnostics.ps1` 脚本执行完成，完成后弹出结果对话框：

- 脚本返回成功：提示"诊断包导出完成。请检查桌面的 cxxime-diagnostics-*.zip。"
- 脚本返回失败：提示"诊断导出已结束，但脚本返回失败。请查看打开的 PowerShell 窗口输出。"

---

## 配置文件格式

配置以 JSON 格式存储（嵌套结构），出厂配置示例：

```json
{
    "schema": {
        "name": "CxxIME",
        "version": "1.0"
    },
    "engine": {
        "page_size": 7,
        "max_pinyin_length": 64,
        "fuzzy_pinyin": true,
        "wubi_auto_commit": true,
        "candidate_learning": true
    },
    "style": {
        "font_face": "Microsoft YaHei UI",
        "font_point": 14,
        "layout": "horizontal",
        "render_backend": "d2d",
        "inline_preedit": false,
        "preedit_type": "composition"
    },
    "layout": {
        "min_width": 160,
        "max_width": 0,
        "max_height": 0,
        "margin_x": 12,
        "margin_y": 12,
        "spacing": 10,
        "candidate_spacing": 11,
        "hilite_spacing": 4,
        "hilite_padding_x": 5,
        "hilite_padding_y": 2,
        "round_corner": 4,
        "round_corner_ex": 4,
        "label_font_point": 0,
        "border_width": 1
    },
    "ascii_composer": {
        "switch_key": {
            "Shift_L": "code",
            "Shift_R": "set_ascii_mode",
            "Control_L": "noop",
            "Control_R": "noop",
            "Caps_Lock": "clear"
        }
    },
    "theme": "azure",
    "status_window": {
        "enable": true,
        "x": -1,
        "y": -1,
        "show_on_startup": true
    },
    "diagnostics": {
        "trace_mode": "normal"
    }
}
```

> `diagnostics` 节还包含日志轮转与慢查询阈值等字段，详见 [可观测性设计](observability.md)。输入模式（`engine.input_mode`，0=拼音 1=五笔 2=混输）由服务端在切换时写回用户目录的配置文件持久化（程序目录可能只读）。

### 配置加载优先级

1. 程序目录 `default.json`（内置默认值）
2. `%USERPROFILE%\cxxime\default.json`（用户自定义，覆盖同名字段）

### 配置生效机制

点击"确定"或"应用"后：

1. 将当前面板的控件值写入内存中的 `Config` 对象
2. 保存到 `%USERPROFILE%\cxxime\default.json`
3. 如果修改了输入模式，通过 IPC 通知服务端立即切换
4. 调用 `notify_config_changed()` 通知 TSF/Server 重新加载配置

---

## 命令行参数

```
cxxime-settings.exe [选项]
```

| 参数 | 说明 |
|------|------|
| `--quick-phrase` | 启动后直接跳转到"词库"面板的快捷造词功能 |
| `--data <目录>` | 指定数据目录（开发调试用），覆盖默认数据路径 |

---

## DPI 支持

设置窗口支持 Per-Monitor DPI 感知（`PROCESS_PER_MONITOR_DPI_AWARE`）。在高 DPI 显示器上：

- 所有控件尺寸按 DPI 比例缩放
- 字体大小自动适配
- 切换显示器时窗口自动调整布局（响应 `WM_DPICHANGED`）

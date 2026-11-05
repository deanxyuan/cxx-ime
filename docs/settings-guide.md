# CxxIME 设置指南

## 概述

CxxIME 提供两种配置方式：**设置窗口**（`cxxime-settings.exe`，面向日常使用）和**配置文件**（`default.json`，面向手工编辑）。部分设备相关字段只写入配置文件，不在设置窗口中显示。

设置窗口使用 Win32 原生控件构建，左侧导航依次为：

1. 输入
2. 界面
3. 高级布局
4. 快捷键
5. 词库管理
6. 特殊符号
7. 备份与导入
8. 故障排查
9. 关于

窗口底部提供“确定”“取消”“应用”三个按钮：

- **确定**：保存配置并关闭窗口。
- **取消**：放弃尚未保存的表单修改。已经立即执行的词库、备份等操作不会被撤销。
- **应用**：保存配置但不关闭窗口。没有未保存修改时按钮为禁用状态。

同一用户目录只能打开一个设置窗口。再次启动设置程序时，会激活已有窗口；使用 `--panel dictionary` 启动时会直接跳到“词库管理”。

配置文件位置：

- 程序默认值：`<安装目录>\data\default.json`
- 用户覆盖值：`%USERPROFILE%\cxxime\default.json`

设置窗口先加载程序默认值，再叠加用户配置。配置保存、词库管理和备份操作通过后台服务完成。安装后的 TSF 会在需要时自动启动服务；如果设置窗口提示无法连接后台，先切换到一次 CxxIME 再重试，或运行安装目录中的 `cxxime-server.exe`。

---

## 面板一：输入

### 输入模式

**配置项**：`engine.input_mode`（`0`=拼音，`1`=五笔，`2`=混输）

| 选项 | 说明 |
|------|------|
| 拼音 | 按所选拼音方案输入，如全拼 `nihao` → 你好 |
| 五笔 | 86 版五笔字型输入 |
| 混输 | 同时识别拼音和五笔编码 |

切换模式后点击“确定”或“应用”，服务端会立即切换，无需重启输入法。

### 拼音方案

**配置项**：`engine.pinyin_scheme`（出厂配置 `full_pinyin`）

| 选项 | 配置值 | 示例 |
|------|--------|------|
| 全拼 | `full_pinyin` | `ni'hao` |
| 微软双拼 | `microsoft_shuangpin` | `ni'hk` |
| 小鹤双拼 | `xiaohe_shuangpin` | `ni'hc` |
| 自然码双拼 | `ziranma_shuangpin` | `ni'hk` |
| 搜狗双拼 | `sogou_shuangpin` | `ni'hk` |

拼音方案在拼音和混输模式下可用，在纯五笔模式下禁用。切换方案后，内联内容示例和候选编码提示会同步变化。

### 混输排序

**配置项**：`engine.mixed_candidate_preference`（`auto` / `wubi`，出厂配置 `auto`）

仅在混输模式下生效：

| 选项 | 说明 |
|------|------|
| 智能排序 | 按输入长度、候选来源和综合评分混合排列 |
| 五笔首选 | 让五笔候选在混输结果中优先交错排列 |

### 内联显示

**配置项**：`style.inline_preedit`（布尔值，出厂配置 `false`）

控制 TSF 是否把预编辑文本写入应用文本区。

| 状态 | 行为 |
|------|------|
| 关闭 | 应用文本区不显示预编辑文本；候选窗口显示编码和候选 |
| 开启 | 应用文本区显示编码或首选词；候选窗口只显示候选列表 |

### 内联内容

**配置项**：`style.preedit_type`（`composition` / `preview`）

只在内联显示开启时可编辑：

| 选项 | 应用文本区显示 | 示例 |
|------|----------------|------|
| 编码 | 当前输入编码 | `ni'hao` |
| 首选词 | 第一个候选 | `你好` |

关闭内联显示时，此选项会保留，重新开启后立即按选择生效。

### 模糊拼音

**配置项**：`engine.fuzzy_pinyin`（布尔值）

启用后允许部分声母、韵母混用，例如：

- `z` / `zh`、`c` / `ch`、`s` / `sh`
- `n` / `l`、`f` / `h`、`r` / `l`
- `an` / `ang`、`en` / `eng`、`in` / `ing`

五笔模式下禁用；混输模式下仍可设置。

### 候选数量

**配置项**：`engine.page_size`（出厂配置 `7`，保存时限制为 1–100）

控制候选窗口每页显示的候选数。常用值为 5–15，较小的值翻页更频繁，较大的值会占用更多横向或纵向空间。

### 五笔设置

五笔选项在五笔或混输模式下可用；其中“第五码作为新编码”只在纯五笔模式下可用。

| 选项 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 四码唯一上屏 | `engine.wubi_auto_commit` | 开 | 输入四码且只有一个候选时自动上屏 |
| 第五码首选上屏 | `engine.wubi_commit_first_on_fifth_key` | 开 | 四码存在多个候选且首选为五笔候选时，输入第五个字母会先上屏首选，再把第五码作为新编码 |
| 显示最短补码 | `engine.wubi_code_hint` | 关 | 未满四码时显示最短补码提示 |
| 第五码作为新编码 | `engine.wubi_restart_on_fifth_after_miss` | 开 | 纯五笔模式下，四码没有任何候选时，第五个字母直接作为新编码重新开始 |

“第五码首选上屏”和“四码唯一上屏”在混输模式下也参与五笔候选处理，但只对五笔来源的候选生效。

### 候选学习

**配置项**：`engine.candidate_learning`（布尔值，出厂配置 `false`）

界面选项为“记忆选词偏好”。启用后，选中的候选会记录为本机学习偏好，用于后续排序，不写入用户词库。

关闭学习后，已有偏好不再参与排序，但仍可在“词库管理 → 选词偏好”中查看和清理。完整排序优先级见 [候选排序设计](candidate-ordering.md)。

### 初始状态

控制新输入会话的默认状态：

| 界面选项 | 配置项 | 出厂值 | 说明 |
|----------|--------|--------|------|
| 英文标点 | `initial_state.chinese_punct` | 未勾选 | 勾选表示启动时使用英文标点；未勾选对应 `chinese_punct=true` |
| 全角字符 | `initial_state.full_shape` | 未勾选 | 启动时使用全角字符 |

---

## 面板二：界面

该面板控制候选窗口的主题、图片背景、字体、布局和预览。修改会立即反映到预览窗口，点击主窗口“确定”或“应用”后才持久化。

### 主题

**配置项**：`theme`

从安装目录的 `data\themes.json` 加载预设主题。内置 12 套配色：

`moon_light`、`moon_dark`、`sky_light`、`sky_dark`、`jade_light`、`jade_dark`、`amber_light`、`amber_dark`、`coral_light`、`coral_dark`、`iris_light`、`iris_dark`。

默认主题为 `moon_light`。每套主题定义背景、边框、候选文字、标签、高亮、注释、翻页按钮、预编辑光标以及预编辑高亮颜色。

### 图片背景

点击“图片背景…”打开独立取景窗口。背景功能对应配置节 `background`。

取景窗口支持：

- 选择、更换图片，并勾选“启用图片背景”。
- “填满”或“完整显示”两种适配方式。
- 分开调整“横排取景”和“竖排取景”。切换候选窗口布局方向时，会使用对应方向的取景结果。
- 在预览中拖动图片，使用鼠标滚轮缩放。
- 25%–400% 的缩放滑杆，以及 0%–100% 的图片强度滑杆。
- “确认”把结果带回设置窗口，“取消”放弃本次取景修改。

默认图片强度为 15%。高对比度模式下图片暂不显示。取景窗口的“确认”只更新设置窗口中的待保存配置，仍需点击主窗口“确定”或“应用”才会写入配置。

`background` 的核心字段如下：

| 字段 | 说明 |
|------|------|
| `enabled` | 是否启用图片背景 |
| `source` / `name` | 背景资源标识和用户选择的文件名 |
| `source_width` / `source_height` | 原图尺寸 |
| `horizontal` / `vertical` | 横排、竖排各自的取景配置 |
| `strength` | 0–1 的图片强度 |

每个方向都有 `fit`（`cover` / `contain`）、`zoom`（0.25–4.0）、`x` / `y`（0–1 取景中心）和内部裁剪矩形。通常使用取景窗口修改，不建议手工编辑资源标识和裁剪字段。

### 字体与字号

| 界面选项 | 配置项 | 出厂值 | 说明 |
|----------|--------|--------|------|
| 字体 | `style.font_face` | `Microsoft YaHei UI` | 点击 `...` 打开系统字体选择对话框 |
| 候选字号 | `style.font_point` | `14` | 候选文字磅值，保存时限制为 8–72 |
| 预编辑字号 | `layout.label_font_point` | `0` | `0` 表示自动比候选字号小 2 磅 |

在字体对话框中修改字号会同步到“候选字号”；字体对话框只改变当前待保存配置。

### 布局方向

**配置项**：`style.layout`

| 选项 | 说明 |
|------|------|
| 横向 | 候选水平排列，一行放满后换行 |
| 纵向 | 候选每行一个，垂直排列 |

切换方向后，图片背景会使用对应的横排或竖排取景。

### 视觉风格

这些下拉框是对高级布局参数的快捷调整，选择后会同步改写相应数值：

| 界面选项 | 作用 |
|----------|------|
| 内容密度（紧凑 / 标准 / 宽松） | 调整窗口边距、预编辑间距、候选间距和确认前缀间距 |
| 焦点区域（紧凑 / 标准 / 宽松） | 调整候选高亮和预编辑高亮的留白与内部间距 |
| 圆角风格（直角 / 轻微 / 圆润） | 调整候选高亮、窗口和预编辑高亮圆角 |
| 窗口边框（无 / 细 / 明显） | 将边框宽度设为 0、1 或 2 |
| 窗口宽度（自动 / 限制） | 自动不限制最大宽度；“限制”将 `layout.max_width` 设为 420 |

“恢复默认”会恢复主题、字体、字号、图片背景、布局方向、渲染方式、状态窗口和全部布局参数；不会修改输入模式、五笔选项或快捷键。

### 渲染方式

**配置项**：`style.render_backend`

| 选项 | 说明 |
|------|------|
| 默认渲染 (D2D) | Direct2D + DirectWrite，支持抗锯齿和 ClearType |
| 兼容渲染 (GDI) | 传统 GDI 渲染，兼容性更好，性能略低 |

### 状态窗口

**配置项**：`status_window.enable`（布尔值）

控制是否显示独立的中英文状态窗口。关闭后仍可通过 Windows 语言栏查看当前输入法状态。

状态窗口坐标 `status_window.x` / `status_window.y` 和 `status_window.show_on_startup` 属于设备相关字段，设置窗口不直接编辑；坐标会写入用户配置，并可通过“备份与导入 → 窗口位置和诊断设置”迁移。

### 候选预览

“候选预览”可选择以下示例：

- 拼音整词
- 拼音分段
- 确认前缀
- 五笔编码
- 混输五笔

点击“打开预览”显示独立候选窗口，按钮会切换为“关闭预览”。预览实时反映当前主题、字体、布局、背景和高级布局参数，但不会修改输入法配置。

---

## 面板三：高级布局

该面板提供 19 个数值参数，点击“应用推荐布局”可载入按横排/竖排区分的推荐值，点击“打开预览”实时查看结果。

### 尺寸与间距

| 参数 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 最小宽度 | `layout.min_width` | 160 | 候选窗口最小宽度（像素） |
| 最大宽度 | `layout.max_width` | 0 | 最大宽度，0 表示不限制 |
| 最大高度 | `layout.max_height` | 0 | 最大高度，0 表示不限制 |
| 水平边距 | `layout.margin_x` | 12 | 内容区左右内边距 |
| 垂直边距 | `layout.margin_y` | 12 | 内容区上下内边距 |
| 预编辑间距 | `layout.spacing` | 10 | 编码区与候选区之间的间距 |
| 候选间距 | `layout.candidate_spacing` | 11 | 候选词之间的间距 |

### 候选高亮

| 参数 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 高亮横向留白 | `layout.hilite_padding_x` | 5 | 候选高亮矩形水平内边距 |
| 高亮纵向留白 | `layout.hilite_padding_y` | 2 | 候选高亮矩形垂直内边距 |
| 高亮内部间距 | `layout.hilite_spacing` | 4 | 高亮区域内标签与文字的间距 |
| 高亮圆角 | `layout.round_corner` | 4 | 候选高亮圆角半径 |

### 窗口外观

| 参数 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 窗口圆角 | `layout.round_corner_ex` | 4 | 候选窗口整体圆角半径 |
| 边框宽度 | `layout.border_width` | 1 | 候选窗口边框宽度 |

### 预编辑高亮

| 参数 | 配置项 | 出厂值 | 说明 |
|------|--------|--------|------|
| 预编辑横向留白 | `layout.preedit_highlight_padding_x` | 4 | 当前预编辑分区水平留白 |
| 预编辑纵向留白 | `layout.preedit_highlight_padding_y` | 2 | 当前预编辑分区垂直留白 |
| 确认前缀间距 | `layout.preedit_confirmed_gap` | 6 | 已确认文本与当前输入之间的间距 |
| 音节边界间距 | `layout.preedit_boundary_gap` | 1 | 当前输入与后续音节标记之间的间距 |
| 预编辑圆角 | `layout.preedit_highlight_corner` | 3 | 预编辑高亮分区圆角 |
| 预编辑边框 | `layout.preedit_highlight_border_width` | 1 | 预编辑高亮分区边框宽度 |

参数保存时会按控件类型限幅。推荐值来自安装目录的 `data\settings_presets.json`；文件缺失或格式无效时使用内置推荐值。预览同样提供拼音整词、拼音分段、确认前缀、五笔编码和混输五笔五种示例。

---

## 面板四：快捷键

### Shift 和 Control 键行为

左 Shift、右 Shift、左 Ctrl、右 Ctrl 可分别配置：

| 配置值 | 界面选项 | 行为 |
|--------|----------|------|
| `inline_ascii` | 临时英文 | 临时进入英文输入；组合中的按键按该模式处理 |
| `code` | 提交编码并切换 | 提交编码原文并切换到英文模式 |
| `candidate` | 提交首选并切换 | 提交首个候选并切换到英文模式 |
| `clear` | 清空编码并切换 | 清空输入缓冲区并切换到英文模式 |
| `set_ascii_mode` | 切换到英文 | 进入英文模式，不提交内容 |
| `unset_ascii_mode` | 切换到中文 | 进入中文模式，不提交内容 |
| `noop` | 不处理 | 仅作为修饰键使用 |

### Caps Lock 行为

**配置项**：`ascii_composer.switch_key.Caps_Lock`

| 配置值 | 空闲时 | 组合输入时 | 说明 |
|--------|--------|------------|------|
| `code` | 切换中/英 | 提交编码原文并切英 | 保留已输入编码 |
| `candidate` | 切换中/英 | 提交首选并切英 | 智能提交 |
| `clear` | 切换中/英 | 清空缓冲区并切英 | 传统行为 |
| `append` | 切换中/英 | 不处理组合输入 | 字母继续追加到输入缓冲区 |
| `noop` | 不处理 | 不处理 | 保留系统 Caps Lock 行为 |

Caps Lock 模式只影响 A–Z 字母键；数字、标点和功能键行为不变。Caps Lock+Shift+字母会输入小写字母。手工把 Caps Lock 配成 `inline_ascii`、`set_ascii_mode` 或 `unset_ascii_mode` 时，引擎会降级为 `clear`。

### 全局快捷键

**配置项**：

- `shortcuts.input_mode_switch`：切换拼音、五笔、混输。
- `shortcuts.activate_ime`：切换到 CxxIME。

两个快捷键出厂均为 `disabled`，可分别勾选“启用”并录入组合键。输入模式快捷键和 CxxIME 激活快捷键不能相同；CxxIME 激活快捷键还必须能被系统成功注册，若已被其他程序占用，保存会失败。

快捷键可使用 F1–F11，或使用 Ctrl/Alt（可同时搭配 Shift）与字母、数字、常用标点或空格组合。

---

## 面板五：词库管理

顶部有“词条 / 候选排序 / 选词偏好 / 自定义短语”四个视图。

- **词条**：查询系统词和用户词，管理用户词、隐藏或恢复系统词。
- **候选排序**：为某个输入编码手动固定候选顺序。
- **选词偏好**：查看、删除或清空候选学习记录。
- **自定义短语**：管理完整编码触发的共享短语。

前三个视图使用“拼音 / 五笔”下拉框选择词典；“自定义短语”不分拼音和五笔，由所有输入模式共用。底部状态栏显示查询或操作结果；需要后台服务的数据操作在服务不可用时会给出失败提示。

### 内置与用户数据文件

内置词典位于安装目录的 `data\`：

| 文件 | 说明 |
|------|------|
| `pinyin.dict.bin` / `pinyin.dict.idx` | 拼音词典数据和整数 ID 索引 |
| `pinyin.spellings.bin` 及双拼拼写表 | 全拼和四种双拼方案的拼写转换表 |
| `pinyin.topn.bin` | 拼音 Top-N 短码索引（CXTOPN v4） |
| `wubi86.dict.bin` / `wubi86.dict.idx` | 五笔词典数据和完整前缀索引 |
| `pinyin.reverse.idx` / `wubi86.reverse.idx` | 设置页拼音/五笔反查索引 |

用户在 `%USERPROFILE%\cxxime\` 下的数据文件：

| 文件 | 说明 |
|------|------|
| `user_pinyin.tsv` / `user_wubi.tsv` | 手工用户词库 |
| `custom_phrases.tsv` | 自定义编码短语 |
| `candidate_order_pinyin.tsv` / `candidate_order_wubi.tsv` | 手动候选顺序 |
| `learning_pinyin.tsv` / `learning_wubi.tsv` | 候选学习偏好 |
| `learning_composition.tsv` | 整句学习数据 |
| `disabled_pinyin.tsv` / `disabled_wubi.tsv` | 停用系统词列表 |

用户文件通常由设置窗口或输入法自动维护，手工编辑前建议先创建备份。

### 词条

在“词条”视图中：

1. 输入词语，选择编码。编码下拉框会根据词语给出候选。
2. 点击“新增”添加当前词典用户词。
3. 选中用户词后可“保存修改”或“删除用户词”。
4. 选中系统词后可“隐藏系统词”；已在停用列表中的词可“恢复系统词”。
5. 使用“新增”右侧下拉菜单可把当前词条同时加入拼音和五笔用户词库。

用户词写入当前词典对应的 `user_pinyin.tsv` 或 `user_wubi.tsv`。纯符号和表情不能作为普通词条，符号应通过 `\` 符号输入。

### 候选排序

“候选排序”视图把某个编码的候选固定为用户指定顺序，优先级高于学习和默认排序。

1. 选择拼音或五笔。
2. 在“词语”框输入候选，在“编码”下拉框选择目标编码。
3. 列表显示该编码的全部候选，并标注 `随包默认`、`用户词库`、`学习偏好` 或 `手动固定`。

选中候选后可执行：

- **固定到首位**
- **追加固定**
- **固定上移 / 固定下移**
- **取消固定**
- **恢复默认**

每个编码最多固定 16 个候选。“恢复默认”需要二次确认，会清除该编码的固定顺序以及该编码下的学习偏好。保存采用版本检查；若顺序已被其他位置修改，会提示刷新后重试。

### 选词偏好

启用“记忆选词偏好”后，选中的候选会记录在当前词典的 `learning_*.tsv` 中。

- “删除选中”删除单条或多条偏好。
- “清空全部偏好”清除当前词典全部学习记录，需要二次确认。
- 学习开关关闭后，已有偏好仍会显示，但不再参与候选排序。

偏好仅影响本地候选排序，不改变用户词库。

### 自定义短语

自定义短语用于“完整编码 → 固定文本”的输入，例如编码 `jtdz` 上屏“家庭地址”。它不依赖拼音或五笔词典，所有输入模式共用同一份 `custom_phrases.tsv`。

在“自定义短语”视图中：

1. 输入编码，例如 `jtdz`。
2. 输入单行内容，例如地址、邮箱或常用文本。
3. 点击“新增”。同一编码可以保存多个短语。
4. 选中记录后可修改、删除，或用“上移 / 下移”调整同一编码下的候选顺序。
5. 可使用“编码或内容”搜索框查询，并通过“上一页 / 下一页”浏览结果。

限制如下：

- 编码为 1–64 个小写英文字母。
- 内容必须是单行文本，不允许控制字符。
- 内容 UTF-8 编码长度不能超过 255 字节。
- 同一编码和内容不能重复。
- 短语在完整输入编码后进入候选；不会在编码前缀阶段触发。

“导入”只接受 TSV 文件，按 `编码<TAB>内容` 逐行合并，重复项保留原记录并计入跳过；“导出”会输出当前全部自定义短语。单个导入文件上限 4 MiB，最多 4096 条短语。

### 用户词库导入与导出

“词条”视图底部提供用户词库的“导入 / 导出”：

- **导入**：选择 `.tsv` 文件后整体替换当前拼音或五笔用户词库，操作前会二次确认。
- **导出**：先请求后台保存最新内存状态，再复制用户词库文件。

用户词库导入文件上限为 64 MiB；任一行编码、音节或文本非法时整体失败。自定义短语请使用“自定义短语”视图自己的导入/导出，不要混用。

---

## 面板六：特殊符号

该面板只读浏览安装目录的 `data\symbols.json`：

- 下拉框选择符号分类，例如“补充标点”。
- 列表显示该分类的全部符号。
- 状态栏显示对应助记码，例如 `\bd`。
- 点击“复制符号”把选中符号复制到剪贴板。

系统符号已从普通词典候选剥离，统一通过 `\` 输入。详见 [组合输入、内联 ASCII 与符号输入](ascii-composer.md)。

---

## 面板七：备份与导入

备份文件扩展名为 `.cxxime-backup`，可包含用户配置和用户数据。导出和导入均通过后台服务异步执行；操作期间相关按钮以及主窗口“确定 / 取消”会临时禁用。

### 备份内容

导出时可独立勾选：

| 选项 | 包含内容 |
|------|----------|
| 设置 | 可跨设备迁移的配置，不包含窗口坐标和设备诊断字段 |
| 手工词库 | 拼音、五笔用户词库及自定义短语 |
| 候选排序 | 拼音、五笔手动固定顺序 |
| 学习记录 | 拼音、五笔学习偏好及整句学习 |
| 停用系统词 | 拼音、五笔停用系统词列表 |
| 窗口位置和诊断设置 | 状态窗口坐标、诊断日志等设备相关配置 |

“设置”“手工词库”“候选排序”“学习记录”“停用系统词”默认勾选；“窗口位置和诊断设置”默认不勾选。

### 导出

1. 勾选至少一项备份内容。
2. 点击“导出备份...”。
3. 选择保存位置。若勾选了设置或设备设置，程序会先保存当前设置，再生成备份。
4. 完成后显示备份文件路径。

默认文件名形如 `CxxIME-backup-YYYYMMDD-HHmm.cxxime-backup`。

### 导入

1. 点击“选择备份...”，选择文件后等待检查。
2. 查看版本、创建时间、文件数和大小。
3. 备份中可用且希望导入的项目会变为可勾选。
4. 勾选至少一项，点击“导入所选内容”。
5. 完成后显示成功合并的条目数；无法应用或重复的数据会计入跳过。

配置项使用合并方式导入；用户词库、自定义短语、候选顺序、学习记录和停用列表也按条目合并，不会整体覆盖现有数据。导入设置或设备设置后，设置窗口会重新加载配置。

---

## 面板八：故障排查

- **启用诊断日志**：对应 `diagnostics.trace_mode`。勾选从 `off` 切换到 `normal`，取消勾选回到 `off`。诊断日志可能包含输入编码。
- **打开常规日志目录**：打开 `%USERPROFILE%\cxxime\logs\`。
- **PackagedApp 日志目录**：界面会提示对应 `LocalState\cxxime\logs` 位置。
- **导出诊断包**：运行安装目录中的 `collect_diagnostics.ps1`，完成后提示桌面的 `cxxime-diagnostics-*.zip` 路径。
- **清理历史日志**：删除常规目录和 PackagedApp 目录中未被占用的历史日志。正在使用的文件会跳过；诊断开启时，运行中的应用仍会生成新日志。

日志轮转、采样率和慢查询阈值等字段属于程序包诊断配置，详见 [可观测性设计](observability.md)。

---

## 面板九：关于

显示 CxxIME 版本、许可证和项目主页：

- 版本：构建时由仓库根目录 `VERSION` 生成到二进制。
- 许可证：Apache License 2.0。
- Gitee：https://gitee.com/shadowyuan/cxx-ime
- GitHub：https://github.com/deanxyuan/cxx-ime

点击链接会在默认浏览器打开，右键链接可复制地址。

---

## 配置文件格式

配置文件使用嵌套 JSON。设置窗口保存时会写出完整用户配置；下面是主要字段示例，默认值与 `data\default.json` 对齐。

```json
{
    "schema": {
        "name": "CxxIME",
        "version": "1.0",
        "description": "CxxIME default configuration"
    },
    "engine": {
        "page_size": 7,
        "input_mode": 0,
        "pinyin_scheme": "full_pinyin",
        "max_pinyin_length": 64,
        "fuzzy_pinyin": true,
        "wubi_auto_commit": true,
        "wubi_commit_first_on_fifth_key": true,
        "wubi_restart_on_fifth_after_miss": true,
        "wubi_code_hint": false,
        "candidate_learning": false,
        "mixed_candidate_preference": "auto"
    },
    "initial_state": {
        "full_shape": false,
        "chinese_punct": true
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
        "border_width": 1,
        "preedit_highlight_padding_x": 4,
        "preedit_highlight_padding_y": 2,
        "preedit_confirmed_gap": 6,
        "preedit_boundary_gap": 1,
        "preedit_highlight_corner": 3,
        "preedit_highlight_border_width": 1
    },
    "theme": "moon_light",
    "background": {
        "enabled": false,
        "source": "",
        "name": "",
        "source_width": 0,
        "source_height": 0,
        "horizontal": {
            "resource": "",
            "generation": 0,
            "fit": "cover",
            "zoom": 1.0,
            "x": 0.5,
            "y": 0.5,
            "left": 0,
            "top": 0,
            "width": 0,
            "height": 0
        },
        "vertical": {
            "resource": "",
            "generation": 0,
            "fit": "cover",
            "zoom": 1.0,
            "x": 0.5,
            "y": 0.5,
            "left": 0,
            "top": 0,
            "width": 0,
            "height": 0
        },
        "strength": 0.15
    },
    "status_window": {
        "enable": true,
        "x": -1,
        "y": -1,
        "show_on_startup": true
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
    "shortcuts": {
        "input_mode_switch": "disabled",
        "activate_ime": "disabled"
    },
    "diagnostics": {
        "trace_mode": "off"
    }
}
```

说明：

- `engine.input_mode` 由服务端在切换输入模式时写回用户配置；程序目录中的默认配置可能不包含该字段。
- `style.preedit_type` 为 `composition` 或 `preview`。
- `background.horizontal` 与 `background.vertical` 保存各自的取景结果；图片关闭时资源字段可以为空。
- 用户配置只持久化 `diagnostics.trace_mode`；日志轮转和慢查询阈值来自程序包配置。
- `status_window.x` / `y` 属于设备相关字段，跨设备迁移时可选择不导入。

### 配置加载优先级

1. `<安装目录>\data\default.json`：内置默认值。
2. `%USERPROFILE%\cxxime\default.json`：用户覆盖值，按同名字段覆盖。
3. 服务端解析主题并生成运行时快照，广播给已连接的 TSF 客户端。

### 配置生效机制

点击“确定”或“应用”后：

1. 设置窗口从当前控件读取配置。
2. 通过控制通道向服务端提交完整用户配置。
3. 服务端写入 `%USERPROFILE%\cxxime\default.json`，重新加载引擎并生成配置快照。
4. 配置快照广播到所有 TSF 客户端，已打开的输入会话按新配置更新。

词库、自定义短语、候选排序、选词偏好和备份文件操作会立即通过 IPC 执行，不依赖主窗口的“应用”。若服务不可用，相关操作会提示失败。

---

## 命令行参数

```text
cxxime-settings.exe [选项]
```

| 参数 | 说明 |
|------|------|
| `--panel dictionary` | 启动后直接跳转到“词库管理” |
| `--data <目录>` | 指定数据目录，主要用于开发调试 |

---

## DPI 支持

设置窗口启用 Per-Monitor DPI 感知（`PROCESS_PER_MONITOR_DPI_AWARE`）：

- 控件尺寸按系统 DPI 缩放。
- 字体大小随 DPI 适配。
- 窗口跨显示器移动时响应 `WM_DPICHANGED` 并重新布局。
- 候选预览和图片背景取景窗口同样按 DPI 缩放。

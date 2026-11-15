# PenDown · 轻量 Windows 便签

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![wxWidgets](https://img.shields.io/badge/wxWidgets-3.2-3B6EA5)
![License](https://img.shields.io/badge/license-MIT-green)

PenDown 是一个便携的 Windows 桌面便签工具，只做便签这一件事。便签正文保存为 UTF-8 `.txt`，字体、颜色、粗细等样式保存在同名的 `.style.json` 中；编辑区使用 `wxStyledTextCtrl`（Scintilla）直接显示和编辑富文本，不需要输入 Markdown 语法。数据直接以文件夹和文件保存，不依赖数据库。

- **正文与样式分离**：`.txt` 只存文字，`.style.json` 保存 UTF-8 字节区间样式
- **直接排版**：选中文字即可设置颜色、背景色、粗体、斜体、下划线、字体和字号
- **轻量**：无数据库，默认离线可用；Git 同步是可选功能
- **桌面友好**：系统托盘 + 全局快捷键 + 单实例运行
- **中英双语**：界面支持中文和 English，可跟随系统或手动指定
- **格式入口集中在标题栏**：颜色盘、样式库，以及清除样式和格式刷两个快捷按钮
- **可脱离主窗口的弹窗**：颜色盘、样式库都是浮动工具窗口，可以拖到编辑窗口外

## 功能

### 主窗口

- 窄而高的便签布局，顶部图标工具栏，下方多级分组树
- 启动时默认在主显示器居中打开；如果上次在副屏，下次会回到主屏
- 分组可以无限嵌套；便签是叶子节点
- 右键菜单支持新建子分组、新建便签、重命名、移动到分组、删除
- 删除便签或分组会移到 Windows 回收站，误删后可以还原；删除分组会连同其中的子分组和便签一起移动
- 主窗口和便签窗口都可以拖动四周边缘或四角调整大小
- `Del` 删除，`F2` 重命名 / 打开便签，`F5` 刷新
- `Ctrl+N` 新建便签，`Ctrl+Shift+N` 新建分组

### 语言

设置窗口顶部可以切换界面语言：

- **跟随系统**：中文系统显示中文，其他系统显示英文
- **中文** / **English**：固定使用对应语言

切换后需要重启 PenDown 生效。语言只影响界面文案和错误提示；磁盘上的默认标题、默认分组名和文件名保持稳定，不会因为切换语言而改变。

界面文案不在代码里写死，而是放在 exe 同级的 `lang/` 目录：

```text
lang/
  zh-CN.lang
  en-US.lang
```

代码里只保留消息 key，例如 `UiText("main.settings")`、`i18n::Tr("note_store.invalid_note_path")`。语言文件是 UTF-8 文本，格式如下：

```text
# lang/zh-CN.lang
main.settings="设置"
count.notes="%ld 条"

# lang/en-US.lang
main.settings="Settings"
count.notes.one="%ld note"
count.notes.many="%ld notes"
```

- 值必须用双引号包围；换行写 `\n`，引号写 `\"`，反斜杠写 `\\`
- 修改 `.lang` 文件后重启程序即可生效，不需要重新编译
- 新增语言时，复制一份语言文件并在设置里增加对应选项即可；目前只内置中文和英文
- 分发时请把 `lang/` 和 `PenDown.exe` 一起打包；CMake 构建会把语言文件复制到输出目录，`install` 也会安装到 `lang/`
- 可以用 `tools/check-i18n.ps1` 检查代码里的 key 是否都存在于两套语言文件

### 便签编辑

- 便签打开为独立窗口，可以脱离主窗口自由拖动
- 单窗富文本编辑，不分左右栏；正文就是最终显示效果，不需要输入 Markdown
- 正文第一行自动作为便签标题和文件名
- `Ctrl+S` 保存；关闭未保存内容会二次确认
- 编辑区按普通文本编辑器处理：左侧显示行号，边距很小、段落和行距紧凑
- 支持中文、英文、数字和 Emoji
- 左侧行号使用 Scintilla 内置数字边距，只绘制可见行；行数进入下一位数时自动加宽，右侧有细分隔线
- 标题栏左侧：颜色盘、样式库、清除样式、格式刷；右侧：保存、删除、最小化、最大化、关闭

常用快捷键：

| 快捷键 | 作用 |
| --- | --- |
| `Ctrl+S` | 保存 |
| `Ctrl+B` / `Ctrl+I` / `Ctrl+U` | 加粗 / 斜体 / 下划线 |
| `Ctrl+0` | 当前行恢复正文 |
| `Ctrl+F` / `Ctrl+H` | 查找 / 替换 |
| `Ctrl+Z` / `Ctrl+Y` | 撤销 / 重做文本 |
| `Ctrl+A` | 全选 |

### 颜色盘

窄长条浮动面板，直接放在编辑区侧边。颜色入口在标题栏的颜色盘按钮，不在样式库中。

- 24 个常用颜色，按 4 列排满，不留半行
- 第一个色块用于清除当前目标颜色
- 顶部可切换“文字颜色 / 背景颜色”
- 自定义颜色使用 RGB 滑杆，**没有确定按钮**，拖动后直接应用
- 先选中文字，再点颜色，选区文字立即更新；没有选区时设置后续输入颜色

颜色盘和样式库可以同时打开；同时打开时样式库会自动放到颜色盘左侧。

### 样式库

样式库是窄长的图标面板，所有样式平铺在一起，每行两个；选中文字后直接点击图标即可应用，不需要确定；悬停图标可以查看样式名称。

包含（2 列 × 4 行）：

```text
加粗        斜体
下划线      删除线
字号加      字号减
超链接      字体字号选择
```

- **超链接**：先选中文字，点击超链接图标并输入网址；按住 Ctrl 点击链接文字会用系统浏览器打开；
- 颜色统一从标题栏颜色盘设置；
- 样式按钮是一次性动作，应用后不会保持选中。

### 标题栏格式按钮

清除样式和格式刷放在标题栏左侧，紧跟样式库按钮：

- **清除样式**：恢复默认字体、字号、颜色、背景和链接；
- **格式刷**：第一次点击复制当前光标位置的字符格式，之后在正文中选中目标文字并松开鼠标，格式会立即应用，不需要再点一次；等待应用期间按钮保持选中，应用完成后自动取消；再次点击按钮会取消格式刷；
- 格式刷只改外观，目标文字上原有的链接会一并清除，避免留下看不见、点不到的链接；
- 保存和删除按钮与最小化、最大化、关闭一起放在标题栏右侧。

### 字体与字号

- 选中文字后可通过样式库的字体按钮打开系统字体对话框，一次设置字体、字号、粗体、斜体和下划线
- 字号增减按钮每次调整 1 磅
- 没有选区时，样式会应用到后续输入的文字

### 样式存储

正文和样式分开保存：

```text
购物清单.txt
购物清单.txt.style.json
```

- `.txt` 是 UTF-8 纯文本，不包含 Markdown 或 HTML 标记
- `.style.json` 记录样式区间、链接、颜色、字体、字号和格式版本
- 样式区间和链接区间都使用 UTF-8 字节偏移，中文和 Emoji 不会错位
- 保存时正文和样式分别写临时文件，再原子替换；样式文件带正文长度和 hash，不匹配时只丢弃样式，不会把样式套到错误位置
- 编辑停止数秒后会写 `*.autosave`（大文件间隔更长）；下次打开便签时如果发现自动保存内容，会询问是否恢复

> 这是纯文本 + 字符样式编辑器，不支持内嵌图片、表格和 Markdown 语法高亮。超链接以蓝色下划线显示，URL 单独保存在样式 JSON 的 `links` 数组中，按住 Ctrl 点击链接文字可打开。链接不再占用样式槽：不同 URL 可以共用同一个样式组合。Scintilla 的 32–39 号样式是预定义样式（33 用于行号），正文样式会跳过这一段，因此最多支持 247 种自定义组合；样式表会自动压缩和去重，但同一时刻的样式组合仍受这个上限约束，超出时新样式会退化为最接近的已有样式。Ctrl+Z/Ctrl+Y 当前只撤销/重做正文，样式和链接操作本身不进入撤销栈；大文件撤销不再逐字节重建样式，正常输入不受影响。

### 托盘与全局快捷键

关闭主窗口不会退出程序，而是隐藏到系统托盘；已经打开的便签窗口会保持打开。托盘右键“关闭”才是真正退出，此时才会关闭所有便签窗口。

默认全局快捷键：

| 快捷键 | 作用 |
| --- | --- |
| `Ctrl+Alt+H` | 显示主窗口 |
| `Ctrl+Alt+N` | 新建便签 |

在设置（齿轮图标）→“快捷键设置”中可以修改。支持 Ctrl / Alt / Shift 的任意组合加字母或数字，也支持 F1–F12 单独使用。新建便签的快捷键只打开编辑窗口，不会把主窗口带出来。

### 数据与备份

默认数据目录是 exe 同级的 `data`：

```text
PenDown/
├─ PenDown.exe
└─ data/
   ├─ 购物清单.txt        ← 根目录便签，显示在列表最上方
   ├─ 购物清单.txt.style.json
   └─ 工作/
      ├─ 周会纪要.txt
      ├─ 周会纪要.txt.style.json
      └─ 项目 A/
         └─ 接口联调记录.txt
```

- 一个目录就是一个分组，可以继续嵌套
- 所有便签正文都是 UTF-8 `.txt`，样式保存在同名的 `.txt.style.json`
- 便签可以直接放在 `data` 根目录，不要求必须属于某个分组
- 保存时用正文第一行生成文件名；修改第一行后再次保存会自动改名，样式文件一起移动
- 旧版 `.md` / `.wxr` 文件不再列出，也不会被删除；需要继续使用请手动把内容转到 `.txt` 便签
- 启动参数 `--data <目录>` 可以指定其他数据目录
- 数据目录优先级：`--data` > 设置中的目录 > exe 同级的 `data`
- 设置（齿轮图标）→“数据存储”可以修改数据目录；修改时可选择复制现有便签，旧目录会保留作为备份
- 启用 Git 同步后，数据目录中会出现 `.git` 目录；它不会显示为分组或便签，请不要手动删除

升级程序时，只替换 `PenDown.exe`、同目录 DLL 和 `icons/`，不要覆盖或删除 `data/`。建议升级前备份整个 `data` 文件夹。

### Git 同步

Git 同步是可选功能，普通使用不需要安装 Git。

1. 安装 [Git for Windows](https://git-scm.com/download/win)。
2. 打开设置 →“Git 同步”，填写：
   - **仓库地址**：HTTPS、SSH 或本地路径都可以，例如 `https://github.com/user/notes.git`
   - **Git 程序**：可填写本地 `git.exe` 的实际路径；留空会自动从 PATH 和常见安装位置查找
   - **用户名 / 密码或令牌**：公开仓库可以留空；使用 HTTPS 私有仓库时填写用户名和访问令牌
   - **分支**：默认 `main`，需要与远程仓库分支一致
3. 关闭所有便签窗口。
4. 点击“推送到远程”或“从远程拉取”。

同步规则：

- 数据目录本身会成为一个 Git 仓库，首次推送或拉取时在 `data/` 中创建 `.git`
- 推送和拉取针对整个 `data` 目录：所有分组、`.txt` 便签、`.style.json` 样式文件和其他文件都会一起同步；`*.autosave`、`*.autosave.style.json`、`*.bak`、`*.tmp` 等本程序自动维护的临时文件由 `.gitignore` 排除，不会提交到远程
- 空分组会通过隐藏文件 `.pendown-group` 保留
- 推送先提交本地改动，再执行强制推送；远程和本地不一致时，**以本地为准**
- 拉取会先备份本地便签到 `%LOCALAPPDATA%\PenDown\backups`，再用远程分支强制覆盖本地；**以远程为准**
- 不做 merge、rebase 或冲突合并
- 访问令牌使用 Windows DPAPI 加密后保存在本机
- 拉取会保留 `data/.git`，但会删除不在远程分支里的本地文件（备份中仍可找回）

## 下载与运行

从 Releases 下载最新的 `PenDown-*-win64.zip`，解压后双击 `PenDown.exe`。

- 系统要求：**Windows 10 / 11 x64**
- 不需要安装 Visual Studio、CMake、vcpkg、.NET、Python 或其他开发工具
- VC++ 运行库已随压缩包附带
- 首次启动会在 exe 同级自动创建 `data` 文件夹
- 程序没有代码签名，Windows SmartScreen 可能提示“已保护你的电脑”，选择“更多信息 → 仍要运行”

## 从源码构建

### 环境

- Windows 10 / 11 x64
- Visual Studio 2022，安装“使用 C++ 的桌面开发”
- CMake 3.21+
- vcpkg

### 构建

```powershell
git clone https://github.com/1148118271/PenDown.git
cd PenDown

# 安装 wxWidgets（只需一次）
vcpkg install wxwidgets:x64-windows

# 指定 vcpkg 路径
$env:VCPKG_ROOT = "C:\path\to\vcpkg"

cmake --preset vcpkg
cmake --build --preset vcpkg-release
```

产物位置：

```text
build/vcpkg/Release/PenDown.exe
```

同目录会自动复制 wxWidgets 运行时 DLL 和 VC++ 运行库。

如果系统已经安装 wxWidgets，也可以设置 `WXWIN` 后使用：

```powershell
cmake --preset system
cmake --build --preset system-release
```

### 在 Zed / clangd 中开发

项目默认使用 Visual Studio 生成器，不会输出 `compile_commands.json`。clangd 缺少该文件时，会找不到 wxWidgets、MSVC 和 Windows SDK 的头文件，编辑器里出现大量红色错误。执行一次：

```powershell
powershell -ExecutionPolicy Bypass -File tools/setup-clangd.ps1
```

脚本会：

- 用 Ninja + MSVC 配置 `build/clangd`，生成 `compile_commands.json`
- 在项目根目录生成本机专用的 `.clangd`，补充 MSVC / Windows SDK 系统头文件路径

`.clangd` 含本机绝对路径，已加入 `.gitignore`，不要提交。之后重启 Zed，或执行 `clangd: Restart language server` 即可。

## 自检

```powershell
# 文件存储与 TXT/JSON 样式自检
PenDown.exe --selftest selftest.txt

# wxStyledTextCtrl 编辑、样式区间、UTF-8、查找替换自检
PenDown.exe --editor-selftest editor-selftest.txt

# 大文件加载/保存压测：输入文件、报告文件
PenDown.exe --editor-bench 50mb.txt bench.txt
```

报告文件为 UTF-8，以 `OK` 或 `FAIL` 开头。`--selftest` 覆盖根目录便签、嵌套分组、移动、重命名、第一行标题、重复标题复用已有文件名、TXT/JSON 样式往返、链接独立存储、旧格式 URL 迁移与重复样式去重、样式文件删除和旧文件忽略；`--editor-selftest` 覆盖 wxStyledTextCtrl 文本往返、中文/Emoji 字节偏移、选区样式、样式槽与预定义槽保留、链接区间编辑与热点、行高回收、换行、查找和替换。

## 性能验证

开发机上用 `--editor-bench` 对合成文本做过一轮验证（隐藏窗口，Windows x64，SSD）：

| 文件 | 大小 | 样式区间 | 读取 | 加载到编辑器 | 取回正文 | 中间插入 | 保存 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 MB | 1,048,590 B | 1,049 | 6 ms | 15 ms | 0 ms | 1 ms | 5 ms |
| 10 MB | 10,485,778 B | 10,486 | 64 ms | 133 ms | 3 ms | 1 ms | 21 ms |
| 50 MB | 52,428,829 B | 52,429 | 325 ms | 697 ms | 20 ms | 9 ms | 128 ms |

50 MB 文件在开头输入/删除、回车换行的单次操作约 1 ms（`begin_ms` / `newline_ms`）。这组数据只说明当前实现没有在每次输入时重排全文；真实界面的滚动、内存占用、不同磁盘和不同样式分布仍需继续压测，不能仅凭这张表认定所有 50 MB 场景都已达标。

50 MB 压测文件约 859,490 行，行号边距为 52 px。行号使用 Scintilla 内置数字边距，只绘制可见行；宽度只在行数位数变化时重设，因此不会随输入频率产生额外开销。

## 技术栈

| 层 | 选型 |
| --- | --- |
| GUI | wxWidgets 3.2（wxVListBox 自绘树、wxStyledTextCtrl / Scintilla 编辑器） |
| 存储 | 文件系统：目录 = 分组，`.txt` = 正文，`.txt.style.json` = 样式，无数据库 |
| 样式 | UTF-8 字节区间 + JSON；颜色/字体/字号/粗体/斜体/下划线/删除线 |
| Git 同步 | 系统 `git.exe`；数据目录直接作为工作树；强制推送 / 强制拉取 |
| 语言 | C++17 |
| 构建 | CMake 3.21+ / Visual Studio 2022 x64 |
| 字体 | 系统默认界面字体；等宽样式使用系统等宽字体 |
| 图标 | Lucide（ISC，SVG 随程序分发并按状态着色） |
| 平台 | Windows 10 / 11 x64（当前优先；跨平台适配见后续计划） |

## 项目结构

```text
PenDown/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ LICENSE
├─ src/
│  ├─ app.cpp                  # wxApp、命令行参数、启动流程
│  ├─ single_instance.h        # 单实例窗口查找与唤醒消息
│  ├─ core/                    # 与界面无关的文档/存储层
│  │  ├─ note_store.*          # 目录扫描、分组目录、便签路径、标题 / 文件名
│  │  ├─ note_storage.*        # TXT + JSON 样式读写、原子保存、自动保存
│  │  ├─ style_model.*         # 样式区间模型
│  │  ├─ link_model.*          # 独立链接区间，不占样式槽
│  │  ├─ git_sync.*            # Git 仓库初始化、推送、拉取、本地备份
│  │  ├─ text.*                # wxString / std::wstring 转换
│  │  └─ selftest.*            # --selftest 隐藏测试入口
│  └─ ui/                      # 表现层
│     ├─ main_frame.*          # 窄长主窗口：工具栏、分组树、托盘、快捷键
│     ├─ note_tree_ctrl.*      # 自绘多级分组树
│     ├─ note_window.*         # 独立便签窗口
│     ├─ note_editor.*         # wxStyledTextCtrl 封装、样式槽、文本事件
│     ├─ find_replace_dialog.* # 查找 / 替换
│     ├─ color_popup.*         # 浮动颜色盘（文字色/背景色 + RGB）
│     ├─ style_popup.*         # 浮动样式库
│     ├─ name_dialog.*         # 新建 / 重命名分组对话框
│     ├─ settings_dialog.*     # 设置对话框：数据目录、Git 同步、快捷键
│     ├─ git_config.*          # Git 配置读写、DPAPI 令牌加密
│     ├─ app_font.*            # 系统字体与字号辅助
│     ├─ icon_library.*        # SVG 图标加载 / 着色缓存
│     ├─ icon_button.*         # 自绘图标按钮
│     ├─ rounded_panel.*       # 浅色容器
│     ├─ window_effects.*      # 无边框窗口、DWM 阴影和圆角
│     ├─ theme.h               # 主题颜色
│     └─ ui_ids.h              # 控件 / 命令 ID
├─ res/                        # 图标、清单、版本信息
│  └─ icons/                   # Lucide SVG
├─ tools/
│  └─ setup-clangd.ps1         # 生成 compile_commands.json 和本机 .clangd
└─ docs/                       # README 截图（旧版，待更新）
```

## 常见问题

### 旧版 `.md` / `.wxr` 便签去哪了？

新版本只列出 `.txt` 正文和对应的 `.txt.style.json` 样式文件。旧 `.md` / `.wxr` 文件仍保留在数据目录中，不会被自动删除，也不会显示在便签列表里；请手动把内容转入新建的 `.txt` 便签。当前版本不提供 Markdown 到富文本的自动转换。

### 关闭主窗口后程序去哪了？

程序会隐藏到系统托盘，已经打开的便签窗口仍会保持打开。托盘图标右键菜单里有“打开 PenDown”和“关闭”。只有“关闭”才会真正退出并关闭所有窗口。

### 删除的便签可以恢复吗？

可以。删除便签或分组时会移到 Windows 回收站，打开回收站即可还原；删除分组时，其中的子分组和便签会一起移动。只有清空回收站后才会真正删除。

如果数据目录位于网络驱动器等没有回收站的位置，删除会失败并提示，请改用资源管理器手动删除，程序不会静默永久删除。

Git 拉取不属于这个流程：它按远程分支覆盖本地，会删除不在远程里的本地文件，但覆盖前会自动备份到 `%LOCALAPPDATA%\PenDown\backups`。

### 可以同时开多个 PenDown 吗？

不可以。重复启动会唤醒已有实例，不会启动第二个进程。

### 怎么修改分组或便签的上级分组？

在主界面右键点击分组或便签，选择“移动到分组...”，再选择目标分组即可。不能把分组移动到它自己或它的子分组下。

### 怎么修改数据存储位置？

设置（齿轮图标）→“数据存储”→点击数据目录右侧的文件夹图标选择新目录，然后保存。可以在提示中选择把现有便签复制到新目录，旧目录会保留作为备份。

### Git 同步提示找不到 git.exe

安装 [Git for Windows](https://git-scm.com/download/win)，或者在设置 →“Git 同步”→“Git 程序”中填写 `git.exe` 的实际路径，例如 `D:\Git\bin\git.exe`。

### 推送或拉取时冲突怎么处理？

不做冲突合并。推送使用强制推送，以本地为准；拉取会先备份本地数据，再用远程分支强制覆盖本地，以远程为准。

### 样式文件可以删除吗？

可以。删除 `*.txt.style.json` 只会丢失颜色、字体和链接等格式，正文仍然保留；下次保存会按默认样式重新生成或保持无样式。不要只改样式文件里的偏移量，正文变化后样式文件可以通过 hash 校验自动失效。

### 更新程序会覆盖便签吗？

只替换程序文件不会。请不要覆盖或删除 `data` 文件夹；升级前建议备份。

### 为什么没有安装包？

PenDown 是便携程序，解压即用。如果放到 `Program Files`，普通用户可能没有写入权限，建议放在用户目录或使用 `--data` 指定单独的数据目录。

## 第三方组件

- [wxWidgets](https://www.wxwidgets.org/)：wxWindows Library Licence（LGPL 2.1 + exception）
- [Scintilla](https://www.scintilla.org/) / wxStyledTextCtrl：HPND / wxWindows Licence
- [Lucide](https://lucide.dev/)：ISC
- 字体：使用 Windows 系统字体，不随程序分发

## 许可证

本项目采用 [MIT License](LICENSE)。第三方组件遵循各自许可证：

- wxWidgets：wxWindows Library Licence（LGPL 2.1 + exception）
- Scintilla / wxStyledTextCtrl：HPND / wxWindows Licence
- Lucide：ISC

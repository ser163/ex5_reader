# EX5 Reader

> 一款符合 RFC EX5-001 协议的电子书阅读器 —— 你的划线与心得,永远跟着书走。

[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078d4)](https://github.com/ser163/ex5_reader)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)](https://isocpp.org/)
[![MSVC](https://img.shields.io/badge/MSVC-2022-CC2929)](https://visualstudio.microsoft.com/)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

## 项目简介

EX5 Reader 是一款**单文件可执行、零运行时依赖**的电子书阅读器,完整实现了 [RFC EX5-001](https://github.com/ser163/ex5_reader/blob/main/docs/%E4%BA%A7%E5%93%81%E6%96%87%E6%A1%A3.md) 协议(`.ex5` 文件格式 v1.0,实现兼容到 v1.1 共享阅读)。

`.ex5` 是一种**基于 ZIP 容器的电子书格式**:把书籍内容(JSON 元数据 + 章节资源)与读者数据(SQLite 数据库)封装在同一个文件里。阅读进度、划线、摘抄、笔记、心得、书评、评分 —— **全部随书携带**,换设备、换软件、丢给朋友,数据都跟着走。

提供两种形态:

- **`bin\ex5reader.exe`** — 命令行版(CLI),适合脚本化与协议调试
- **`bin\ex5reader_gui.exe`** — 图形界面版(GUI,Win32 原生),鼠标选文划线、黄色高亮渲染

## 特性

| 类别 | 功能 |
|---|---|
| **阅读** | 流式续载(超大书不爆内存)、断点续读、章节跳转、夜间模式、字体/字号/颜色/背景调节 |
| **批注** | 划线(可附批注)、摘抄、笔记、心得;按章节/类型筛选;TXT 导出;**右键边栏条目 → 查看完整详情** |
| **共读** | **v1.1 共享阅读**:未加密的 `.ex5` 文件中,划线/摘抄/笔记/心得/书评/评分对所有持文件者**只读共享**(带作者名),仅作者本人可编辑/删除 |
| **多用户** | 内置「本地读者」,GUI 可自由新建/切换/设密码;密码用 16 字节随机盐 + SHA-256 存,常时比较 |
| **格式** | `.ex5` = ZIP + JSON + SQLite;支持 `.txt` / `.html` 章节资源;加密文件(`encrypt_scope != 0`)会明确拒绝 |
| **可扩展** | DLL 插件机制(扫描 `plugins\*.dll`),标准 SDK 头文件,API 版本校验,热加载;自带 demo「阅读统计」插件 |

## 快速开始

### 环境要求

- **Windows 10/11 x64**
- **MSVC 2022**(Community / Build Tools,带 C++ 桌面开发工作负载)

### 编译

仓库根目录执行:

```cmd
build.bat
```

脚本会自动:
1. 拉起 `vcvars64.bat`
2. 编译 `ex5reader.exe`(CLI)
3. 编译 `ex5reader_gui.exe`(GUI,带 rc 资源)
4. 编译示例插件 `bin\plugins\demo_stats.dll`

产物在 `bin\` 目录,零运行时依赖,直接双击即可。

### 运行

**GUI(推荐)**:

```cmd
bin\ex5reader_gui.exe samples\sample_book.ex5
```

也支持双击 `.ex5` 文件(需先跑 `installer\ex5_setup.iss` 注册文件关联)。

**CLI**:

```cmd
bin\ex5reader.exe samples\sample_book.ex5
```

进入交互式 REPL,输入 `help` 查看命令。

### 自己造一本书

```cmd
python tools\make_sample.py
```

生成 `samples\sample_book.ex5`(3 章《海与灯》),包含完整 `book_data/`、`resources/`、空 `read_data.db`(7 张表)与 `meta.xml`。

## CLI 命令一览

```
info                          书籍信息
chapters                      章节目录
read <章> [偏移] [字数]         阅读章节(自动记录进度)
mark <章> <起> <止> [批注]      划线(可附批注)
excerpt <章> <起> <止>          摘抄原文
note <章> <内容>                写笔记
think <章> <内容>               写心得
notes / thoughts / reviews      查看全部记录
delnote <id> / delthink <id>   删除记录
rate <1-5>                     评分
review <内容>                   写书评
progress                       阅读进度
save                           立即保存(写回 .ex5)
quit                           自动保存并退出
```

## 项目结构

```
ex5_reader/
├── src/                     源码
│   ├── main.cpp             CLI 入口
│   ├── ex5book.h/.cpp       EX5 容器读写层(ZIP/JSON/SQLite)
│   ├── gui_main.cpp         主窗口 / 工具栏 / 消息循环
│   ├── gui_reader.cpp       阅读区(流式加载 / 高亮 / 划线)
│   ├── gui_panel.cpp        右侧笔记面板
│   ├── gui_dialogs.cpp      笔记本/心得集编辑器
│   ├── gui_style.cpp        文字样式 / 夜间模式
│   ├── gui_plugin.cpp       插件宿主
│   ├── ex5_plugin.h         插件 SDK 头文件
│   └── app.rc / app.manifest
├── plugins/demo/            示例插件「阅读统计」源码
├── third_party/             静态编译的第三方库
│   ├── miniz/               ZIP 读写 (richgel999/miniz)
│   ├── sqlite3.c/.h         SQLite amalgamation
│   └── json.hpp             nlohmann/json
├── samples/                 示例书
├── docs/                    产品文档、协议说明
├── installer/               INNO Setup 安装脚本 + .iss
├── tools/make_sample.py     示例书生成器
└── build.bat                一键编译
```

## 技术栈

| 类别 | 选型 | 理由 |
|---|---|---|
| 语言 | C++17 | 性能 + 系统 API 友好 |
| GUI | Win32 原生 | 零依赖、单文件 ~1.8MB、启动 < 100ms |
| ZIP | miniz | 单头文件、纯 C、静态编译 |
| JSON | nlohmann/json | 易用、协议字段多 |
| 数据库 | SQLite amalgamation | 单 .c 文件,静态编译进 exe |
| 密码学 | Windows BCrypt | 系统原生,生成随机盐 |
| 字符 | UTF-8 + 手写码点工具 | 中文路径 / 中文内容全程 UTF-8 |

## 协议符合性

- 实现了 [RFC EX5-001 v1.0](https://github.com/ser163/ex5_reader/blob/main/docs/%E4%BA%A7%E5%93%81%E6%96%87%E6%A1%A3.md) 全部必选功能
- 实现了 v1.1 草案(§5.4 共享阅读):未加密文件中,`notes` / `inspiration` / `reviews` / `ratings` 对所有持文件者只读共享(带作者名)
- 加密文件(`encrypt_scope != 0`)会读取并明确报错,本实现不提供解密

详见 `docs/产品文档.md`、`docs/插件规范.md`。

## 路线图

- [ ] 远程同步(RFC §5 RESTful API)
- [ ] 加密文件解密支持(等 RFC §4 稳定)
- [ ] 更多插件样例(翻译 / 朗读 / 主题)
- [ ] 跨平台:macOS / Linux(Win32 特定代码抽象出 Platform 层后)

## 贡献

PR 欢迎!请保持:

- C++17 兼容、MSVC 2022 编译通过
- 遵循现有命名风格(`snake_case` 函数、`PascalCase` 类型、`kCamelCase` 常量)
- 新增功能同步更新 `docs/产品文档.md` 与 `docs/插件规范.md`
- 第三方依赖尽量保持单文件、静态编译

## 致谢

- [miniz](https://github.com/richgel999/miniz) by Rich Geldreich
- [nlohmann/json](https://github.com/nlohmann/json) by Niels Lohmann
- [SQLite](https://www.sqlite.org) by D. Richard Hipp
- INNO Setup by Jordan Russell

## License

MIT — 详见 [LICENSE](LICENSE)。

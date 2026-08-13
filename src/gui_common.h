// gui_common.h — GUI 各模块共享的状态、结构与跨模块接口
//
// 模块划分(按功能解耦):
//   gui_main.cpp    主窗口、工具栏、菜单、布局、消息分发、打开/保存
//   gui_reader.cpp  阅读区:流式加载(上下双向)、高亮、悬停气泡、划线/摘抄
//   gui_panel.cpp   右侧笔记面板(分类过滤、点击跳转)
//   gui_dialogs.cpp 对话框:输入框、笔记本/心得集编辑器、用户管理、TXT 导出
//   gui_style.cpp   文字样式:字体/字号/颜色/背景色/对齐/夜间模式 + INI 配置
//   gui_plugin.cpp  插件宿主:扫描加载 plugins/*.dll,转发「插件」菜单命令
// 插件作者只需要看 src/ex5_plugin.h(SDK)与 docs/插件规范.md。
#pragma once

#include <windows.h>
#include <windowsx.h>
#include <richedit.h>
#include <commdlg.h>
#include <commctrl.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ex5book.h"
#include "utf8.h"

// ---------------- UTF-8 <-> UTF-16 ----------------
inline std::wstring u8w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
inline std::string wu8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// ---------------- 控件 / 命令 ID ----------------
enum {
    IDC_LIST = 1001, IDC_EDIT, IDC_STATUS, IDC_PANEL,
    IDB_OPEN = 1101, IDB_MARK, IDB_EXCERPT, IDB_NOTE, IDB_THINK,
    IDB_NOTES, IDB_THOUGHTS, IDB_RATE, IDB_SAVE, IDB_USER,
    IDT_ALL = 1201, IDT_MARK, IDT_EXCERPT, IDT_NOTE, IDT_THINK,
    IDB_COLLAPSE = 1206,                                          // 折叠章节列表
    IDM_DEL = 1301, IDM_VIEW,
    IDM_FONT = 1401, IDM_BIGGER, IDM_SMALLER, IDM_COLOR, IDM_BGCOLOR,   // 「文字」菜单
    IDM_ALEFT, IDM_ACENTER, IDM_ARIGHT, IDM_NIGHT,
    IDM_MODE_MIN = 1411, IDM_MODE_COMFY,                              // 「模式」菜单
    IDM_PLUGIN_BASE = 1500                                        // 1500+i 为第 i 个插件
};

// ---------------- 共享状态(定义在 gui_main.cpp) ----------------
struct Seg {                 // 已加载章节在阅读区文本中的段落映射
    int  chapter;            // 章节 index
    long textStart;          // 章正文在 g_text 中的码点起点(标题之后)
    long textLen;            // 章正文码点长度
};
struct PanelItem { int kind; long long id; bool hasRange; long long a, b; bool own; };  // kind:1划 2摘 3笔 4心得;own=false 为他人记录(只读)

extern ex5::Book   g_book;
extern bool        g_open;
extern int         g_chapter;              // 当前章节 index
extern std::string g_text;                 // 阅读区全部已加载文本(UTF-8,含分隔线与标题)
extern std::vector<long> g_cpToU16;        // 码点偏移 -> RichEdit UTF-16 位置
extern std::vector<Seg>  g_segs;
extern int         g_nextToLoad;           // chapters() 中下一个待续载的位置
extern bool        g_refilling;            // 续载防重入
extern std::wstring g_bookPath;            // 当前书路径(供插件查询)

extern HWND g_hwnd, g_hList, g_hEdit, g_hStatus, g_hPanel, g_hTip, g_hCollapse;
extern HFONT g_font, g_fontEdit;
extern bool g_listVisible;
extern HMENU g_hTextMenu;
extern HMENU g_hModeMenu;
extern int  g_viewMode;               // 0=舒心模式(侧栏+图片按钮) 1=极简模式(仅阅读区)

extern std::vector<PanelItem> g_panelItems;
extern int g_panelFilter;
extern long long g_tipNoteId;
extern bool g_tipActive;
extern std::vector<ex5::Note> g_notesCache;   // 笔记缓存(mousemove 期间不查 SQL)
extern const wchar_t* kKindName[];            // {"", "划线", "摘抄", "笔记", "心得"}

// ---------------- 跨模块接口 ----------------
// gui_reader.cpp —— 阅读区
void buildMap();
long u16ToCp(long u16pos);
bool dispToChapter(long p, int& chapter, long& off);
const Seg* findSeg(int chapter);
void loadChaptersFrom(int chapPos, bool scrollTop);
void checkScrollEdges();
void ensureFilled();
void paintHighlights();
void refreshNotesCache();
void updateHoverTip(POINT clientPt);
bool selectionToChapterRange(int& chIdx, long& a, long& b);
void markSelection(bool withComment);
// 跳转到指定章节(必要时自动加载该章)并选中引用范围
void jumpToRef(int chapterIdx, bool hasRange, long long ra, long long rb);

// gui_panel.cpp —— 右侧笔记面板
void refreshPanel();
void panelJumpToSelection();                 // 面板条目点击 -> 跳转原文

// gui_dialogs.cpp —— 对话框
bool promptText(const wchar_t* title, const wchar_t* label, bool multiline, std::string& out,
                bool password = false);
void showRecordList(const wchar_t* title, bool isNotes, long long singleId = -1);  // 笔记本(isNotes=true)/ 心得集;singleId>=0 表示只显示该条(右键「查看」单条模式)
void showUserDialog();

// gui_style.cpp —— 文字样式与夜间模式
void loadTextSettings();
void saveTextSettings();
void applyTextStyle();
void zoomFont(int delta);
void chooseFontDlg();
void chooseColorDlg(bool background);
void setAlign(WORD a);
void toggleNightMode();
bool isNightMode();
WORD styleAlign();
int  iniGetViewMode();                   // INI [view] mode(默认 0 舒心)
void iniSetViewMode(int mode);

// gui_plugin.cpp —— 插件宿主
void pluginsBuildMenu(HMENU hBar);           // 扫描 plugins/*.dll 并追加「插件」菜单
bool pluginCommand(int id);                  // 命中插件菜单命令则执行并返回 true
void pluginsShutdown();

// gui_main.cpp —— 主窗口
void updateTitle();
void openBook(const std::wstring& pathW);
void saveBook(bool quiet);
void setViewMode(int mode);              // 0=舒心 1=极简(切换侧栏与按钮图标)
void applyToolbarIcons();                // 舒心模式加载 icons\*.png,极简模式还原文字按钮
void shareNoticeOnce();                  // v1.1 共享隐私提示(每会话首次创建批注时弹一次)

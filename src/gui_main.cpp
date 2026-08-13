// gui_main.cpp — EX5 阅读器 GUI 版(Win32 原生,64 位,UNICODE)
//
// 本文件:全局状态定义、主窗口(工具栏/菜单/布局/消息分发)、打开/保存书籍。
// 其余功能见 gui_common.h 顶部注释中的模块划分。
//
// 特性:
//  - 阅读区连续滚动:章与章之间横线分隔,每章开头显示章节标题;
//    滚动到底部自动续载下一章,滚动到顶部自动向前补载上一章
//  - 鼠标拖选划线/摘抄/笔记/心得;划线以黄色底纹渲染
//  - 鼠标悬停在划线上弹出气泡,按类型显示批注/摘抄/心得
//  - 右侧笔记面板:竖排分类标签过滤,点击跳转原文,右键删除
//  - 笔记本/心得集:左右结构编辑器,右键跳转引用位置,可导出 TXT
//  - 「文字」菜单:字体/字号/文字颜色/背景色/对齐/夜间模式,INI 持久化
//  - 「插件」菜单:自动加载 plugins\*.dll(见 docs/插件规范.md)
//  - 大文件打开时自动探测并整体载入内存解析(见 ex5book openZip_)
#define UNICODE
#define _UNICODE

#include "gui_common.h"
#include <gdiplus.h>

// ---------------- 全局状态定义(extern 声明见 gui_common.h) ----------------
ex5::Book   g_book;
bool        g_open = false;
int         g_chapter = 0;
std::string g_text;
std::vector<long> g_cpToU16;
std::vector<Seg>  g_segs;
int         g_nextToLoad = 0;
bool        g_refilling = false;
std::wstring g_bookPath;

HWND  g_hwnd = nullptr, g_hList = nullptr, g_hEdit = nullptr, g_hStatus = nullptr;
HWND  g_hPanel = nullptr, g_hTip = nullptr, g_hCollapse = nullptr;
HFONT g_font = nullptr, g_fontEdit = nullptr;
bool  g_listVisible = true;            // 章节列表可见性(小箭头折叠)
HMENU g_hTextMenu = nullptr;
HMENU g_hModeMenu = nullptr;
int   g_viewMode = 0;                  // 0=舒心模式 1=极简模式

static ULONG_PTR g_gpToken = 0;                       // GDI+ 会话
static std::vector<HICON> g_btnIcons;                 // 已加载的工具栏图标(便于释放)

std::vector<PanelItem> g_panelItems;
int g_panelFilter = 0;
long long g_tipNoteId = -1;
bool      g_tipActive = false;
std::vector<ex5::Note> g_notesCache;

// ---------------- 窗口标题 ----------------
void updateTitle() {
    std::wstring t = L"EX5 阅读器";
    if (g_open) {
        t += L" —— 《" + u8w(g_book.title()) + L"》";
        t += L"  [" + u8w(g_book.currentUserName()) + L"]";
    }
    SetWindowTextW(g_hwnd, t.c_str());
}

// ---------------- v1.1 共享阅读隐私提示 ----------------
// 协议 §7:未加密文件中所有划线/摘抄/笔记/心得/评分对任何持文件者可见,
// 应在用户创建此类内容时明确告知(每个会话首次创建时弹一次)。
void shareNoticeOnce() {
    static bool shown = false;
    if (shown) return;
    shown = true;
    MessageBoxW(g_hwnd,
        L"按 EX5 协议 v1.1(§5.4 共享批注):未加密的 .ex5 文件中,\r\n"
        L"你的划线、摘抄、笔记、心得和评分对所有持有本文件的人可见(带你的用户名),\r\n"
        L"只有你自己能编辑或删除它们。\r\n\r\n"
        L"如需隐私,请使用加密(encrypt_scope != 0)的文件。",
        L"共享阅读提示", MB_ICONINFORMATION);
}

// ---------------- 打开书籍 ----------------
void openBook(const std::wstring& pathW) {
    ex5::Book nb;
    std::string err;
    if (!nb.open(wu8(pathW), err)) {
        MessageBoxW(g_hwnd, u8w(err).c_str(), L"打开失败", MB_ICONERROR);
        return;
    }
    g_book = std::move(nb);
    g_bookPath = pathW;
    g_open = true;
    g_chapter = 0;
    updateTitle();
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    for (auto& c : g_book.chapters()) {
        std::wstring item = L"第 " + std::to_wstring(c.index) + L" 章  " + u8w(c.title);
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
    int start = 0, pos = 0; long long off = 0;
    if (g_book.lastPosition(pos, off)) start = pos;
    if (start <= 0) start = g_book.chapters().empty() ? 0 : g_book.chapters()[0].index;
    int selIdx = 0;
    for (size_t i = 0; i < g_book.chapters().size(); ++i)
        if (g_book.chapters()[(size_t)i].index == start) { selIdx = (int)i; break; }
    SendMessageW(g_hList, LB_SETCURSEL, selIdx, 0);
    loadChaptersFrom(selIdx, true);
    refreshNotesCache();
    // 状态栏提示打开模式(内存/文件探测结果)
    std::wstring st = L"已打开(" + u8w(g_book.openMode()) + L"),滚动到底部自动续载下一章";
    SetWindowTextW(g_hStatus, st.c_str());
}

static void openFileDialog() {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"EX5 电子书 (*.ex5)\0*.ex5\0所有文件\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) openBook(file);
}

// ---------------- 保存 ----------------
void saveBook(bool quiet) {
    if (!g_open) return;
    std::string err;
    if (g_book.save(err)) {
        if (!quiet) SetWindowTextW(g_hStatus, L"已保存到 .ex5 文件");
    } else {
        MessageBoxW(g_hwnd, u8w(err).c_str(), L"保存失败", MB_ICONERROR);
    }
}

// ---------------- 主窗口 ----------------
static const wchar_t* kBtnLabels[] = {
    L"打开", L"划线", L"摘抄", L"笔记", L"心得", L"笔记本", L"心得集", L"评分", L"保存", L"用户"
};
static const int kBtnIds[] = {
    IDB_OPEN, IDB_MARK, IDB_EXCERPT, IDB_NOTE, IDB_THINK,
    IDB_NOTES, IDB_THOUGHTS, IDB_RATE, IDB_SAVE, IDB_USER
};
constexpr int kBtnCount = 10;
// ---------------- 按钮悬停提示 ----------------
static HWND g_hBtnTip = nullptr;
static void addBtnTip(HWND target, const wchar_t* text) {
    if (!g_hBtnTip || !target) return;
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;   // 对整个按钮区域生效,接管其鼠标消息
    ti.hwnd = g_hwnd;
    ti.uId = (UINT_PTR)target;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(g_hBtnTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// 为工具栏 10 个按钮、右侧竖标签、折叠箭头统一挂悬停提示(图标按钮必需)
static void setupButtonTips() {
    g_hBtnTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_hBtnTip, TTM_SETMAXTIPWIDTH, 0, 360);
    SendMessageW(g_hBtnTip, TTM_SETDELAYTIME, TTDT_INITIAL, 250);
    static const wchar_t* kBtnTips[kBtnCount] = {
        L"打开:打开一本 .ex5 电子书",
        L"划线:对选中的文字划线,可附加批注(留空为纯划线)",
        L"摘抄:摘抄选中的文字(原文自动入库)",
        L"笔记:写一条笔记,关联当前章节",
        L"心得:写心得;有选区时先自动划线再写,无选区关联当前章节",
        L"笔记本:查看/编辑全部划线、摘抄、笔记,可跳转原文、导出 TXT",
        L"心得集:查看/编辑全部心得,可跳转引用章节、导出 TXT",
        L"评分:给本书打 1-5 星",
        L"保存:立即把全部记录写回 .ex5 文件(关闭窗口时也会自动保存)",
        L"用户:新建用户(可设密码)/ 切换用户,各人记录互相隔离",
    };
    for (int i = 0; i < kBtnCount; ++i)
        addBtnTip(GetDlgItem(g_hwnd, kBtnIds[i]), kBtnTips[i]);
    static const wchar_t* kTabTips[5] = {
        L"显示当前章节的全部记录", L"只看划线", L"只看摘抄", L"只看笔记", L"只看心得",
    };
    for (int i = 0; i < 5; ++i)
        addBtnTip(GetDlgItem(g_hwnd, IDT_ALL + i), kTabTips[i]);
    addBtnTip(g_hCollapse, L"收起 / 展开左侧章节列表");
}

static void layoutControls(int w, int h);   // 定义在本文件后段

// ---------------- 工具栏图片图标(舒心模式) ----------------
// 图标来源(按优先级):1) exe 同目录 icons\<key>.png(外部覆盖,免重编译换图)
//                     2) exe 内嵌 PNG 资源(RCDATA 201+i,默认随程序携带)
//                     3) 都没有则按钮回退为纯文字
static const wchar_t* kBtnIconKeys[kBtnCount] = {
    L"open", L"mark", L"excerpt", L"note", L"think",
    L"notes", L"thoughts", L"rate", L"save", L"user"
};
#define IDR_PNG_BASE 201   // app.rc 中第 i 个按钮图标 = IDR_PNG_BASE + i

// 把 GDI+ 位图缩放到 24x24 并保留透明通道,输出 HICON;失败返回 nullptr
// 用 HICON 而不用 HBITMAP:带 alpha 的图标能和主题按钮底色自然融合,没有色块
static HICON iconToHandle(Gdiplus::Bitmap& src) {
    const int S = 24;
    Gdiplus::Bitmap dst(S, S, PixelFormat32bppARGB);   // 不填底色,保持透明
    {
        Gdiplus::Graphics g(&dst);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(&src, 0, 0, S, S);
    }
    HICON hic = nullptr;
    if (dst.GetHICON(&hic) != Gdiplus::Ok) return nullptr;
    return hic;
}

// 加载第 i 个按钮的图标:先外部文件,后内嵌资源
static HICON loadButtonIcon(int i) {
    // 1) 外部覆盖:exe 同目录 icons\<key>.png
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir = buf;
    size_t pos = dir.find_last_of(L'\\');
    if (pos != std::wstring::npos) dir = dir.substr(0, pos);
    std::wstring path = dir + L"\\icons\\" + kBtnIconKeys[i] + L".png";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Gdiplus::Bitmap src(path.c_str());
        if (src.GetLastStatus() == Gdiplus::Ok) return iconToHandle(src);
    }
    // 2) 内嵌 PNG 资源(RCDATA)
    HRSRC res = FindResourceW(GetModuleHandleW(nullptr),
                              MAKEINTRESOURCEW(IDR_PNG_BASE + i), RT_RCDATA);
    if (!res) return nullptr;
    DWORD size = SizeofResource(GetModuleHandleW(nullptr), res);
    HGLOBAL hg = LoadResource(GetModuleHandleW(nullptr), res);
    if (!hg || !size) return nullptr;
    void* data = LockResource(hg);
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!copy) return nullptr;
    void* cd = GlobalLock(copy);
    memcpy(cd, data, size);
    GlobalUnlock(copy);
    IStream* stm = nullptr;
    if (FAILED(CreateStreamOnHGlobal(copy, TRUE, &stm))) { GlobalFree(copy); return nullptr; }
    Gdiplus::Bitmap src(stm);
    stm->Release();
    if (src.GetLastStatus() != Gdiplus::Ok) return nullptr;
    return iconToHandle(src);
}

// 按当前模式刷新工具栏按钮:舒心=有图标用图标(缺图标回退文字),极简=纯文字
void applyToolbarIcons() {
    for (HICON hi : g_btnIcons) DestroyIcon(hi);
    g_btnIcons.clear();
    for (int i = 0; i < kBtnCount; ++i) {
        HWND b = GetDlgItem(g_hwnd, kBtnIds[i]);
        if (!b) continue;
        // 先摘除旧图像并去掉 BS_ICON/BS_BITMAP(恢复文字显示)
        HICON oldI = (HICON)SendMessageW(b, BM_SETIMAGE, IMAGE_ICON, 0);
        if (oldI) DestroyIcon(oldI);
        HBITMAP oldB = (HBITMAP)SendMessageW(b, BM_SETIMAGE, IMAGE_BITMAP, 0);
        if (oldB) DeleteObject(oldB);
        LONG_PTR sty = GetWindowLongPtrW(b, GWL_STYLE);
        SetWindowLongPtrW(b, GWL_STYLE, (sty & ~(BS_BITMAP | BS_ICON)) | BS_PUSHBUTTON | BS_TEXT);
        SetWindowTextW(b, kBtnLabels[i]);
        if (g_viewMode == 0) {   // 舒心模式:尝试挂图标(外部文件 -> 内嵌资源)
            HICON hic = loadButtonIcon(i);
            if (hic) {
                g_btnIcons.push_back(hic);
                SetWindowLongPtrW(b, GWL_STYLE, GetWindowLongPtrW(b, GWL_STYLE) | BS_ICON);
                SendMessageW(b, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hic);
            }
        }
        SetWindowPos(b, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

// ---------------- 关于 ----------------
// 自定义"关于"对话框:图标 + 元信息 + 可点击的 GitHub 超链接(SysLink 控件)
// 选用 SysLink 而不是 MessageBoxW,因为后者不支持超链接;SysLink 是原生 Comctl32 控件,
// 文本用 <a href="...">...</a> 标记,点击触发 NM_CLICK 通知,父窗口 ShellExecuteW 打开浏览器
static LRESULT CALLBACK AboutProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static HFONT s_hFontBold = nullptr;
    switch (m) {
    case WM_CREATE: {
        // SysLink 是 Comctl32 v6 控件,显式注册(幂等)
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LINK_CLASS };
        InitCommonControlsEx(&icc);

        // 主标题加粗 20pt
        s_hFontBold = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

        // 应用图标(优先用内嵌资源 #100,即 app.rc 里的主图标)
        HICON hi = (HICON)LoadImageW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(100), IMAGE_ICON, 32, 32, 0);
        if (!hi) hi = LoadIconW(nullptr, IDI_INFORMATION);
        HWND hIcon = CreateWindowW(L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE | SS_ICON,
            20, 18, 32, 32, h, nullptr, nullptr, nullptr);
        SendMessageW(hIcon, STM_SETIMAGE, IMAGE_ICON, (LPARAM)hi);

        // 主标题
        HWND hTitle = CreateWindowW(L"STATIC",
            L"EX5 Reader  (ex5reader)  v1.0.0",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            65, 22, 380, 24, h, nullptr, nullptr, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)s_hFontBold, TRUE);

        // 元信息行
        const wchar_t* lines[] = {
            L"一款符合 RFC EX5-001 协议的电子书阅读器",
            L"你的划线与心得,永远跟着书走。",
            L"",
            L"作者:    Harry Liu",
            L"邮箱:    L3478830@163.com",
            L"日期:    2026-08-13",
        };
        int y = 55;
        for (int i = 0; i < 6; i++) {
            CreateWindowW(L"STATIC", lines[i],
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                65, y + i * 20, 380, 18, h, nullptr, nullptr, nullptr);
        }

        // GitHub 超链接(SysLink 控件,点击后 ShellExecuteW 打开浏览器)
        CreateWindowExW(0, WC_LINK,
            L"GitHub:  <a href=\"https://github.com/ser163/ex5_reader\">https://github.com/ser163/ex5_reader</a>",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            65, y + 6 * 20, 380, 18, h, nullptr, nullptr, nullptr);

        // 协议 + 致谢
        CreateWindowW(L"STATIC",
            L"本程序遵循 MIT 开源协议。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            65, 218, 380, 18, h, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC",
            L"使用的第三方库:miniz、nlohmann/json、SQLite(INNO Setup)。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            65, 238, 380, 36, h, nullptr, nullptr, nullptr);

        // 确定按钮
        CreateWindowW(L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            365, 290, 80, 28, h, (HMENU)IDOK, nullptr, nullptr);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->code == NM_CLICK || nm->code == NM_RETURN) {
            NMLINK* nml = (NMLINK*)l;
            ShellExecuteW(h, L"open", nml->item.szUrl, nullptr, nullptr, SW_SHOW);
            return 1;   // 告诉 SysLink 已处理,不要走默认行为
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_NCDESTROY:
        if (s_hFontBold) { DeleteObject(s_hFontBold); s_hFontBold = nullptr; }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void showAbout() {
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = AboutProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"Ex5AboutDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        s_reg = true;
    }
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME,
        L"Ex5AboutDlg", L"关于 EX5 Reader",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 470, 350,
        g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!h) return;
    EnableWindow(g_hwnd, FALSE);
    MSG msg;
    while (IsWindow(h) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
}

// ---------------- 视图模式 ----------------
// 0=舒心(左右侧栏 + 图片按钮)  1=极简(只保留阅读区,按钮不加载图片)
void setViewMode(int mode) {
    g_viewMode = (mode == 1) ? 1 : 0;
    iniSetViewMode(g_viewMode);
    applyToolbarIcons();
    RECT rc; GetClientRect(g_hwnd, &rc);
    layoutControls(rc.right, rc.bottom);
    SetWindowTextW(g_hStatus, g_viewMode == 1
        ? L"极简模式:仅阅读区(「模式」菜单可切回舒心模式)"
        : L"舒心模式:章节列表 + 笔记面板 + 图片按钮");
}

static void layoutControls(int w, int h) {
    const int toolbarH = 42, statusH = 26, listW = 200, panelW = 260, stripW = 18;
    int x = 8;
    for (int i = 0; i < kBtnCount; ++i) {
        HWND b = GetDlgItem(g_hwnd, kBtnIds[i]);
        if (b) MoveWindow(b, x, 6, 66, 30, TRUE);
        x += 72;
    }
    int bodyH = h - toolbarH - statusH - 8;
    bool side = (g_viewMode == 0);                       // 极简模式隐藏左右侧栏
    int effListW = (side && g_listVisible) ? listW : 0;
    int effPanelW = side ? panelW : 0;
    ShowWindow(g_hList, (side && g_listVisible) ? SW_SHOW : SW_HIDE);
    MoveWindow(g_hList, 8, toolbarH, effListW, bodyH, TRUE);
    // 折叠小箭头:夹在章节列表与阅读区之间,竖直居中
    if (g_hCollapse) {
        ShowWindow(g_hCollapse, side ? SW_SHOW : SW_HIDE);
        MoveWindow(g_hCollapse, 8 + effListW, toolbarH + bodyH / 2 - 32, stripW, 64, TRUE);
        SetWindowTextW(g_hCollapse, (side && g_listVisible) ? L"◀" : L"▶");
    }
    int editX = 8 + effListW + (side ? stripW + 6 : 0);
    MoveWindow(g_hEdit, editX, toolbarH, w - editX - effPanelW - 16, bodyH, TRUE);
    ShowWindow(g_hPanel, side ? SW_SHOW : SW_HIDE);
    MoveWindow(g_hPanel, w - effPanelW - 8, toolbarH, side ? panelW - 46 : 0, bodyH, TRUE);
    for (int i = 0; i < 5; ++i) {
        HWND t = GetDlgItem(g_hwnd, IDT_ALL + i);
        if (t) {
            ShowWindow(t, side ? SW_SHOW : SW_HIDE);
            MoveWindow(t, w - 8 - 42, toolbarH + 4 + i * 54, 42, 50, TRUE);
        }
    }
    MoveWindow(g_hStatus, 0, h - statusH, w, statusH, TRUE);
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hwnd = h;
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);
        g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_fontEdit = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        for (int i = 0; i < kBtnCount; ++i) {
            HWND b = CreateWindowW(L"BUTTON", kBtnLabels[i],
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, h, (HMENU)(INT_PTR)kBtnIds[i], nullptr, nullptr);
            SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
        }
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, h, (HMENU)IDC_LIST, nullptr, nullptr);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_hCollapse = CreateWindowW(L"BUTTON", L"◀",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, h, (HMENU)IDB_COLLAPSE, nullptr, nullptr);
        g_hPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, h, (HMENU)IDC_PANEL, nullptr, nullptr);
        SendMessageW(g_hPanel, WM_SETFONT, (WPARAM)g_font, TRUE);
        {
            const wchar_t* tabs[] = {L"全", L"划", L"摘", L"笔", L"心"};
            for (int i = 0; i < 5; ++i) {
                HWND t = CreateWindowW(L"BUTTON", tabs[i],
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0, h, (HMENU)(INT_PTR)(IDT_ALL + i), nullptr, nullptr);
                SendMessageW(t, WM_SETFONT, (WPARAM)g_font, TRUE);
            }
        }
        setupButtonTips();   // 工具栏/竖标签/折叠箭头的悬停提示
        g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, h, (HMENU)IDC_EDIT, nullptr, nullptr);
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_fontEdit, TRUE);
        SendMessageW(g_hEdit, EM_SETEVENTMASK, 0,
                     ENM_SELCHANGE | ENM_SCROLL | ENM_MOUSEEVENTS);
        // 悬停气泡控件
        g_hTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            h, nullptr, GetModuleHandleW(nullptr), nullptr);
        {
            TOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            ti.hwnd = g_hEdit;
            ti.uId = 1;
            ti.lpszText = (LPWSTR)L"";
            SendMessageW(g_hTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(g_hTip, TTM_SETMAXTIPWIDTH, 0, 320);
            SendMessageW(g_hTip, TTM_SETDELAYTIME, TTDT_INITIAL, 200);
        }
        g_hStatus = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC",
            L"  打开一本 .ex5 电子书开始阅读(左上角「打开」按钮)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, h, (HMENU)IDC_STATUS, nullptr, nullptr);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_font, TRUE);
        // 菜单栏:「模式」+「文字」+「插件」
        {
            HMENU hBar = CreateMenu();
            g_hModeMenu = CreatePopupMenu();
            AppendMenuW(g_hModeMenu, MF_STRING, IDM_MODE_MIN,   L"极简模式(&J)\t仅阅读区");
            AppendMenuW(g_hModeMenu, MF_STRING, IDM_MODE_COMFY, L"舒心模式(&S)\t侧栏 + 图片按钮");
            AppendMenuW(hBar, MF_POPUP, (UINT_PTR)g_hModeMenu, L"模式(&M)");
            g_hTextMenu = CreatePopupMenu();
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_FONT,    L"字体(&F)...");
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_BIGGER,  L"增大字号(&B)\tCtrl +");
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_SMALLER, L"减小字号(&S)\tCtrl -");
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_COLOR,   L"文字颜色(&C)...");
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_BGCOLOR, L"背景色(&G)...");
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_NIGHT,   L"夜间模式(&N)");
            AppendMenuW(g_hTextMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_ALEFT,   L"左对齐(&L)");
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_ACENTER, L"居中(&E)");
            AppendMenuW(g_hTextMenu, MF_STRING, IDM_ARIGHT,  L"右对齐(&R)");
            AppendMenuW(hBar, MF_POPUP, (UINT_PTR)g_hTextMenu, L"文字(&T)");
            pluginsBuildMenu(hBar);   // 有插件时追加「插件」菜单
            // 「关于」菜单(最右):目前只有「关于 EX5 Reader」一项,无帮助文档
            HMENU hAboutMenu = CreatePopupMenu();
            AppendMenuW(hAboutMenu, MF_STRING, IDM_ABOUT, L"关于 EX5 Reader...");
            AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hAboutMenu, L"关于(&A)");
            SetMenu(g_hwnd, hBar);
            g_viewMode = iniGetViewMode();   // 启动时恢复上次的视图模式
            applyToolbarIcons();             // 舒心模式挂图标,极简模式纯文字
            RECT rc0; GetClientRect(g_hwnd, &rc0);   // 菜单栏会压缩客户区,立即重排
            layoutControls(rc0.right, rc0.bottom);
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)l;
        mmi->ptMinTrackSize.x = 1040;
        mmi->ptMinTrackSize.y = 520;
        return 0;
    }
    case WM_SIZE:
        layoutControls(LOWORD(l), HIWORD(l));
        ensureFilled();   // 窗口变大后内容不足一屏时自动补载下一章
        return 0;
    case WM_INITMENUPOPUP:
        if ((HMENU)w == g_hTextMenu) {   // 对齐项单选勾选 + 夜间模式勾选
            WORD a = styleAlign();
            UINT cur = (a == PFA_CENTER) ? IDM_ACENTER
                     : (a == PFA_RIGHT)  ? IDM_ARIGHT : IDM_ALEFT;
            CheckMenuRadioItem(g_hTextMenu, IDM_ALEFT, IDM_ARIGHT, cur, MF_BYCOMMAND);
            CheckMenuItem(g_hTextMenu, IDM_NIGHT,
                          MF_BYCOMMAND | (isNightMode() ? MF_CHECKED : MF_UNCHECKED));
        } else if ((HMENU)w == g_hModeMenu) {   // 当前模式单选勾选
            CheckMenuRadioItem(g_hModeMenu, IDM_MODE_MIN, IDM_MODE_COMFY,
                               g_viewMode == 1 ? IDM_MODE_MIN : IDM_MODE_COMFY, MF_BYCOMMAND);
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->idFrom != IDC_EDIT) return 0;
        if (nm->code == EN_SELCHANGE) {
            DWORD s = 0, e = 0;
            SendMessageW(g_hEdit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
            if (e > s && g_open) {
                int chA = 0, chB = 0; long offA = 0, offB = 0;
                long cpa = u16ToCp((long)s), cpb = u16ToCp((long)e);
                if (dispToChapter(cpa, chA, offA) && dispToChapter(cpb, chB, offB) && chA == chB) {
                    std::wstring t = L"第 " + std::to_wstring(chA) + L" 章 · 已选中 " +
                        std::to_wstring(offB - offA) + L" 字符(章内偏移 " +
                        std::to_wstring(offA) + L"-" + std::to_wstring(offB) +
                        L"),点「划线」或「摘抄」";
                    SetWindowTextW(g_hStatus, t.c_str());
                } else {
                    SetWindowTextW(g_hStatus, L"选区跨章节或包含标题,划线请在单章正文内选择");
                }
            } else if (g_open) {
                // 光标移动 -> 当前章节跟随,面板随之切换
                int ch = 0; long off = 0;
                if (dispToChapter(u16ToCp((long)s), ch, off) && ch != g_chapter) {
                    g_chapter = ch;
                    refreshPanel();
                }
            }
        } else if (nm->code == EN_VSCROLL) {
            checkScrollEdges();
        } else if (nm->code == EN_MSGFILTER) {
            MSGFILTER* mf = (MSGFILTER*)l;
            if (mf->msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(mf->wParam) & MK_CONTROL)) {
                // Ctrl + 滚轮:缩放字号,并吞掉这条消息避免同时滚动
                zoomFont(GET_WHEEL_DELTA_WPARAM(mf->wParam) > 0 ? +2 : -2);
                mf->msg = WM_NULL;
            } else if (mf->msg == WM_MOUSEMOVE) {
                POINT pt{ GET_X_LPARAM(mf->lParam), GET_Y_LPARAM(mf->lParam) };
                updateHoverTip(pt);
            } else if (mf->msg == WM_LBUTTONDOWN || mf->msg == WM_MOUSEWHEEL) {
                if (g_tipActive) {
                    TOOLINFOW ti{};
                    ti.cbSize = sizeof(ti);
                    ti.hwnd = g_hEdit;
                    ti.uId = 1;
                    SendMessageW(g_hTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
                    g_tipActive = false;
                    g_tipNoteId = -1;
                }
                if (mf->msg == WM_MOUSEWHEEL)
                    PostMessageW(g_hwnd, WM_APP + 11, 0, 0);   // 滚动完成后检查两端续载
            }
        }
        return 0;
    }
    case WM_APP + 11:   // 滚轮滚动结束后的两端续载检查
        checkScrollEdges();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (pluginCommand(id)) return 0;                 // 插件菜单优先
        if (id == IDC_LIST && HIWORD(w) == LBN_SELCHANGE) {
            LRESULT sel = SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && g_open && sel < (LRESULT)g_book.chapters().size())
                loadChaptersFrom((int)sel, true);
            return 0;
        }
        if (id == IDC_PANEL && HIWORD(w) == LBN_SELCHANGE) {
            panelJumpToSelection();
            return 0;
        }
        if (id >= IDT_ALL && id <= IDT_THINK) {
            g_panelFilter = id - IDT_ALL;
            refreshPanel();
            std::wstring t = L"笔记面板:";
            t += g_panelFilter ? kKindName[g_panelFilter] : L"全部";
            t += L" 分类";
            SetWindowTextW(g_hStatus, t.c_str());
            return 0;
        }
        switch (id) {
        case IDB_COLLAPSE: {
            g_listVisible = !g_listVisible;
            RECT rc; GetClientRect(g_hwnd, &rc);
            layoutControls(rc.right, rc.bottom);
            break;
        }
        case IDB_OPEN:    openFileDialog(); break;
        case IDB_MARK:    markSelection(true); break;
        case IDB_EXCERPT: markSelection(false); break;
        case IDB_NOTE: {
            if (!g_open || g_chapter <= 0) break;
            std::string content;
            if (promptText(L"写笔记", L"笔记内容(关联当前章节):", true, content) && !content.empty()) {
                long long nid = g_book.addNote(content, g_chapter, false, 0, 0, "");
                shareNoticeOnce();
                refreshPanel();
                SetWindowTextW(g_hStatus, (L"笔记已记录 #" + std::to_wstring(nid)).c_str());
            }
            break;
        }
        case IDB_THINK: {
            if (!g_open || g_chapter <= 0) break;
            // 有选区:先自动落一条划线作为心得依据;无选区:心得仅关联当前章节
            int chIdx = 0; long a = 0, b = 0;
            bool hasSel = selectionToChapterRange(chIdx, a, b);
            if (hasSel) {
                std::string chapText = g_book.chapterText(chIdx);
                std::string original = utf8::charSubstr(chapText, (size_t)a, (size_t)b);
                g_book.addNote("", chIdx, true, a, b, original);
                g_chapter = chIdx;
                refreshNotesCache();
                paintHighlights();
            }
            std::string content;
            const wchar_t* label = hasSel
                ? L"已对选中文字划线,写下你的心得:"
                : L"未选中文字,心得将关联当前章节(选中文本可同时划线):";
            if (promptText(L"写心得", label, true, content) && !content.empty()) {
                long long nid = g_book.addInspiration(content, g_chapter);
                shareNoticeOnce();
                refreshPanel();
                std::wstring t = hasSel ? L"已划线并记录心得 #" : L"心得已记录(关联本章)#";
                SetWindowTextW(g_hStatus, (t + std::to_wstring(nid)).c_str());
            } else if (hasSel) {
                refreshPanel();
            }
            break;
        }
        case IDB_NOTES:    if (g_open) showRecordList(L"笔记本(划线 / 摘抄 / 笔记)", true); break;
        case IDB_THOUGHTS: if (g_open) showRecordList(L"心得集", false); break;
        case IDB_RATE: {
            if (!g_open) break;
            // v1.1 共享阅读:先列出所有用户的评分,再录入自己的
            std::wstring label;
            auto all = g_book.listRatings();
            if (all.empty()) {
                label = L"还没有人评分。请输入 1-5 星:";
            } else {
                label = L"本书评分:";
                for (auto& r : all) {
                    if (label.size() > 5) label += L"  ";
                    label += u8w(r.author.empty() ? "未知" : r.author);
                    if (r.own) label += L"(我)";
                    label += L" " + std::to_wstring(r.stars) + L"星";
                }
                label += L"\r\n请输入你的评分(1-5 星,覆盖旧评分):";
            }
            std::string s;
            if (promptText(L"评分", label.c_str(), false, s)) {
                int v = atoi(s.c_str());
                if (g_book.addRating(v)) {
                    shareNoticeOnce();
                    SetWindowTextW(g_hStatus, (L"评分成功:" + std::to_wstring(v) + L" 星").c_str());
                } else {
                    MessageBoxW(g_hwnd, L"评分需为 1-5 的整数", L"提示", MB_ICONWARNING);
                }
            }
            break;
        }
        case IDB_SAVE: saveBook(false); break;
        case IDB_USER: if (g_open) showUserDialog(); break;
        // ---- 「帮助」菜单 ----
        case IDM_ABOUT: showAbout(); break;
        // ---- 「文字」菜单 ----
        case IDM_FONT:    chooseFontDlg(); break;
        case IDM_BIGGER:  zoomFont(+2); break;
        case IDM_SMALLER: zoomFont(-2); break;
        case IDM_COLOR:   chooseColorDlg(false); break;
        case IDM_BGCOLOR: chooseColorDlg(true); break;
        case IDM_NIGHT:   toggleNightMode(); break;
        case IDM_MODE_MIN:   setViewMode(1); break;
        case IDM_MODE_COMFY: setViewMode(0); break;
        case IDM_ALEFT:   setAlign(PFA_LEFT); break;
        case IDM_ACENTER: setAlign(PFA_CENTER); break;
        case IDM_ARIGHT:  setAlign(PFA_RIGHT); break;
        }
        return 0;
    }
    // 笔记面板右键 -> 删除菜单 -> 确认后删除
    case WM_CONTEXTMENU: {
        if ((HWND)w == g_hPanel && g_open) {
            POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            POINT pc = pt;
            ScreenToClient(g_hPanel, &pc);
            LRESULT r = SendMessageW(g_hPanel, LB_ITEMFROMPOINT, 0, MAKELPARAM(pc.x, pc.y));
            if (HIWORD(r) == 0) {
                int idx = (int)LOWORD(r);
                SendMessageW(g_hPanel, LB_SETCURSEL, idx, 0);
                // v1.1:他人记录只读,右键不给删除入口
                if (idx < (int)g_panelItems.size() && !g_panelItems[(size_t)idx].own) {
                    SetWindowTextW(g_hStatus, L"他人共享的记录只读,仅作者本人可删除");
                    return 0;
                }
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, IDM_VIEW, L"查看");
                AppendMenuW(menu, MF_STRING, IDM_DEL,  L"删除这条记录");
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         pt.x, pt.y, 0, h, nullptr);
                DestroyMenu(menu);
                if (cmd == IDM_VIEW && idx < (int)g_panelItems.size()) {
                    // 边栏条目是简短摘要,点"查看"打开完整详情窗口(只显示该条)
                    auto& it = g_panelItems[(size_t)idx];
                    bool isNotes = (it.kind >= 1 && it.kind <= 3);   // 1划 2摘 3笔 → 笔记本
                    const wchar_t* ttl = isNotes
                        ? L"笔记本(划线 / 摘抄 / 笔记)" : L"心得集";
                    showRecordList(ttl, isNotes, it.id);
                } else if (cmd == IDM_DEL && idx < (int)g_panelItems.size()) {
                    auto& it = g_panelItems[(size_t)idx];
                    std::wstring q = L"确定删除这条「";
                    q += kKindName[it.kind];
                    q += L"」记录吗?此操作会写入 .ex5 文件。";
                    if (MessageBoxW(h, q.c_str(), L"删除确认",
                                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        bool ok = (it.kind == 4) ? g_book.deleteInspiration(it.id)
                                                 : g_book.deleteNote(it.id);
                        refreshNotesCache();
                        refreshPanel();
                        paintHighlights();
                        SetWindowTextW(g_hStatus, ok ? L"记录已删除(保存后生效)"
                                                     : L"删除失败");
                    }
                }
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        saveBook(true);
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        pluginsShutdown();
        for (HICON hi : g_btnIcons) DestroyIcon(hi);
        g_btnIcons.clear();
        if (g_gpToken) { Gdiplus::GdiplusShutdown(g_gpToken); g_gpToken = 0; }
        if (g_font) DeleteObject(g_font);
        if (g_fontEdit) DeleteObject(g_fontEdit);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    (void)cmdLine;
    LoadLibraryW(L"Msftedit.dll");   // RichEdit 4.1+
    Gdiplus::GdiplusStartupInput gpInput{};   // GDI+:工具栏 PNG 图标加载用
    Gdiplus::GdiplusStartup(&g_gpToken, &gpInput, nullptr);
    loadTextSettings();              // 读取 ex5reader.ini(字体/字号/颜色/背景/对齐/夜间模式)

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"Ex5ReaderGui";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(100));
    wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(100));
    RegisterClassExW(&wc);

    HWND h = CreateWindowExW(0, L"Ex5ReaderGui", L"EX5 阅读器",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 660,
        nullptr, nullptr, inst, nullptr);
    ShowWindow(h, show);
    UpdateWindow(h);

    // 命令行传入 .ex5 路径则直接打开(用 GetCommandLineW,空参数时不会被误认为自身路径)
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2 && argv[1][0]) openBook(argv[1]);
    if (argv) LocalFree(argv);

    // Ctrl + / Ctrl - 字号快捷键(主键盘与数字小键盘)
    ACCEL acc[] = {
        { FCONTROL | FVIRTKEY, VK_OEM_PLUS,  IDM_BIGGER },
        { FCONTROL | FVIRTKEY, VK_ADD,       IDM_BIGGER },
        { FCONTROL | FVIRTKEY, VK_OEM_MINUS, IDM_SMALLER },
        { FCONTROL | FVIRTKEY, VK_SUBTRACT,  IDM_SMALLER },
    };
    HACCEL hAcc = CreateAcceleratorTableW(acc, (int)(sizeof(acc) / sizeof(acc[0])));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!TranslateAcceleratorW(g_hwnd, hAcc, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

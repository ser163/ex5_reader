// gui_dialogs.cpp — 对话框:模态输入框、笔记本/心得集(左右结构编辑器 + 右键跳转 + TXT 导出)、用户管理
#include "gui_common.h"

#include <ctime>

// ---------------- 模态输入框 ----------------
struct PromptState {
    const wchar_t* label;
    bool multiline;
    bool password = false;
    std::wstring result;
    bool ok = false;
};

static LRESULT CALLBACK PromptProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    PromptState* st = (PromptState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)l;
        st = (PromptState*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        int editH = st->multiline ? 120 : 26;
        DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOVSCROLL |
            (st->multiline ? ES_MULTILINE | ES_WANTRETURN : ES_AUTOHSCROLL);
        if (st->password) style |= ES_PASSWORD;
        HWND hLabel = CreateWindowW(L"STATIC", st->label, WS_CHILD | WS_VISIBLE,
            12, 10, 396, 20, h, nullptr, nullptr, nullptr);
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style,
            12, 34, 396, editH, h, (HMENU)100, nullptr, nullptr);
        HWND hOk = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            216, 34 + editH + 10, 90, 30, h, (HMENU)IDOK, nullptr, nullptr);
        HWND hCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            318, 34 + editH + 10, 90, 30, h, (HMENU)IDCANCEL, nullptr, nullptr);
        for (HWND c : {hLabel, hEdit, hOk, hCancel}) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        SetFocus(hEdit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) {
            if (LOWORD(w) == IDOK) {
                wchar_t buf[8192]{};
                GetDlgItemTextW(h, 100, buf, 8191);
                st->result = buf;
                st->ok = true;
            }
            DestroyWindow(h);
        }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool promptText(const wchar_t* title, const wchar_t* label, bool multiline, std::string& out,
                bool password) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PromptProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"Ex5Prompt";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    PromptState st{ label, multiline };
    st.password = password;
    RECT pr; GetWindowRect(g_hwnd, &pr);
    int w = 424, hgt = 34 + (multiline ? 120 : 26) + 10 + 30 + 52;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - hgt) / 2;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"Ex5Prompt", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, w, hgt,
        g_hwnd, nullptr, GetModuleHandleW(nullptr), &st);
    if (!dlg) return false;
    EnableWindow(g_hwnd, FALSE);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
    if (st.ok) out = wu8(st.result);
    return st.ok;
}

// ---------------- 笔记本 / 心得集(左右结构编辑器) ----------------
// 控件 id
enum {
    REC_LIST = 100, REC_TYPE = 111, REC_CHAPTER, REC_EDIT,
    REC_SAVE, REC_JUMP, REC_EXPORT, REC_HINT, REC_TIME,
    REC_LBL_ORG = 120, REC_EDIT_ORG, REC_LBL_CMT, REC_EDIT_CMT,
    RECM_JUMP = 201, RECM_DEL, RECM_EXPORT
};

struct RecState {
    bool isNotes = true;
    long long singleId = -1;    // >=0: 只显示该 id(右键「查看」单条模式);-1: 全部
    std::vector<ex5::Note> notes;
    std::vector<ex5::Inspiration> insps;
    int  cur = -1;              // 当前选中行(-1 无选择)
    bool editable = false;      // 当前条目是否有可编辑内容(笔记/心得正文,划线/摘抄的批注)
    bool commentMode = false;   // 划线/摘抄:右侧为「原文(只读)+批注(可编辑)」双框
    // 右键「跳转到引用位置」:关闭对话框后由 showRecordList 执行
    bool jumpRequested = false;
    int  jumpChapter = 0;
    bool jumpHasRange = false;
    long long jumpA = 0, jumpB = 0;
};

static int noteKind(const ex5::Note& n) {
    return (n.hasRange && n.content.empty()) ? 2 : n.hasRange ? 1 : 3;
}

// 时间戳 → "YYYY-MM-DD HH:MM" 宽字符串(本地时区);<=0 视为"无时间"
static std::wstring fmtTimeW(long long ts) {
    if (ts <= 0) return L"-";
    std::time_t t = (std::time_t)ts;
    std::tm tm{};
    localtime_s(&tm, &t);
    wchar_t buf[32];
    std::wcsftime(buf, 32, L"%Y-%m-%d %H:%M", &tm);
    return buf;
}
static void layoutRecEditor(HWND h, RecState* st, int w, int hgt);   // 定义在 RecProc 前

// 从数据库重读记录并刷新左侧列表,保持当前选中行
static void fillRecList(HWND h, RecState* st, int keepSel) {
    HWND hLb = GetDlgItem(h, REC_LIST);
    SendMessageW(hLb, LB_RESETCONTENT, 0, 0);
    if (st->isNotes) {
        st->notes = g_book.listNotes();
        if (st->singleId >= 0) {
            // 「查看」单条模式:只保留匹配 id 的那一条
            st->notes.erase(
                std::remove_if(st->notes.begin(), st->notes.end(),
                    [&](const ex5::Note& n) { return n.id != st->singleId; }),
                st->notes.end());
        }
        for (auto& n : st->notes) {
            std::wstring line = L"【";
            line += kKindName[noteKind(n)];
            if (!n.own && !n.author.empty()) { line += L"·"; line += u8w(n.author); }  // 他人记录标注作者
            line += L"】第" + std::to_wstring(n.chapterId) + L"章  ";
            if (!n.original.empty()) line += u8w(n.original);
            if (!n.content.empty())  line += L"  —— " + u8w(n.content);
            if (line.size() > 120) line = line.substr(0, 120) + L"…";
            SendMessageW(hLb, LB_ADDSTRING, 0, (LPARAM)line.c_str());
        }
    } else {
        st->insps = g_book.listInspirations();
        if (st->singleId >= 0) {
            st->insps.erase(
                std::remove_if(st->insps.begin(), st->insps.end(),
                    [&](const ex5::Inspiration& i) { return i.id != st->singleId; }),
                st->insps.end());
        }
        for (auto& i : st->insps) {
            std::wstring line = L"【心得";
            if (!i.own && !i.author.empty()) { line += L"·"; line += u8w(i.author); }
            line += L"】第" + std::to_wstring(i.chapterId) + L"章  " + u8w(i.content);
            if (line.size() > 120) line = line.substr(0, 120) + L"…";
            SendMessageW(hLb, LB_ADDSTRING, 0, (LPARAM)line.c_str());
        }
    }
    int count = st->isNotes ? (int)st->notes.size() : (int)st->insps.size();
    if (count == 0) { st->cur = -1; return; }
    if (keepSel < 0 || keepSel >= count) keepSel = 0;
    SendMessageW(hLb, LB_SETCURSEL, keepSel, 0);
    st->cur = keepSel;
}

// 把选中记录装入右侧编辑区。
// 笔记/心得:单个内容框可编辑;划线/摘抄:原文只读、批注可编辑(保存按钮对两者均可用)
static void showRecordInEditor(HWND h, RecState* st) {
    HWND hType = GetDlgItem(h, REC_TYPE), hCh = GetDlgItem(h, REC_CHAPTER);
    HWND hEd = GetDlgItem(h, REC_EDIT), hSave = GetDlgItem(h, REC_SAVE);
    HWND hLo = GetDlgItem(h, REC_LBL_ORG), hEo = GetDlgItem(h, REC_EDIT_ORG);
    HWND hLc = GetDlgItem(h, REC_LBL_CMT), hEc = GetDlgItem(h, REC_EDIT_CMT);
    std::wstring type, chap, content, original, time;
    st->editable = false;
    st->commentMode = false;
    if (st->cur >= 0) {
        if (st->isNotes) {
            auto& n = st->notes[(size_t)st->cur];
            int kind = noteKind(n);
            type = kKindName[kind];
            if (!n.author.empty()) { type += L" · "; type += u8w(n.author); }  // v1.1 显示作者
            if (!n.own) type += L"(只读)";
            chap = L"第 " + std::to_wstring(n.chapterId) + L" 章";
            time = fmtTimeW(n.createTime);
            if (kind == 3) {                       // 笔记:正文可编辑(仅自己的)
                content = u8w(n.content);
                st->editable = n.own;
            } else {                               // 划线/摘抄:原文只读,批注可编辑(仅自己的)
                st->commentMode = true;
                original = u8w(n.original);
                content  = u8w(n.content);
                st->editable = n.own;
            }
        } else {                                   // 心得:可编辑(仅自己的)
            auto& i = st->insps[(size_t)st->cur];
            type = L"心得";
            if (!i.author.empty()) { type += L" · "; type += u8w(i.author); }
            if (!i.own) type += L"(只读)";
            chap = L"第 " + std::to_wstring(i.chapterId) + L" 章";
            time = fmtTimeW(i.createTime);
            content = u8w(i.content);
            st->editable = i.own;
        }
    }
    SetWindowTextW(hType, type.c_str());
    SetWindowTextW(hCh, chap.c_str());
    SetWindowTextW(GetDlgItem(h, REC_TIME), time.c_str());
    // 切换单框(笔记/心得)与双框(划线/摘抄:原文+批注)
    ShowWindow(hEd,  st->commentMode ? SW_HIDE : SW_SHOW);
    ShowWindow(hLo,  st->commentMode ? SW_SHOW : SW_HIDE);
    ShowWindow(hEo,  st->commentMode ? SW_SHOW : SW_HIDE);
    ShowWindow(hLc,  st->commentMode ? SW_SHOW : SW_HIDE);
    ShowWindow(hEc,  st->commentMode ? SW_SHOW : SW_HIDE);
    if (st->commentMode) {
        SetWindowTextW(hEo, original.c_str());
        SendMessageW(hEo, EM_SETREADONLY, TRUE, 0);        // 原文永远只读
        SetWindowTextW(hEc, content.c_str());
        SendMessageW(hEc, EM_SETREADONLY, st->editable ? FALSE : TRUE, 0);
    } else {
        SetWindowTextW(hEd, content.c_str());
        SendMessageW(hEd, EM_SETREADONLY, st->editable ? FALSE : TRUE, 0);
    }
    EnableWindow(hSave, st->editable);
    // 单框/双框切换后按当前窗口尺寸重排
    RECT rc; GetClientRect(h, &rc);
    layoutRecEditor(h, st, rc.right, rc.bottom);
}

static void setRecHint(HWND h, const wchar_t* t) {
    SetWindowTextW(GetDlgItem(h, REC_HINT), t);
}

static void saveRecord(HWND h, RecState* st) {
    if (st->cur < 0 || !st->editable) return;
    // 划线/摘抄保存批注框,笔记/心得保存正文框
    HWND hEd = GetDlgItem(h, st->commentMode ? REC_EDIT_CMT : REC_EDIT);
    int len = GetWindowTextLengthW(hEd);
    std::wstring buf((size_t)len + 1, 0);
    GetWindowTextW(hEd, buf.data(), len + 1);
    buf.resize((size_t)len);
    std::string c = wu8(buf);
    bool ok;
    if (st->isNotes) {
        ok = g_book.updateNote(st->notes[(size_t)st->cur].id, c);
        if (ok) st->notes[(size_t)st->cur].content = c;
    } else {
        ok = g_book.updateInspiration(st->insps[(size_t)st->cur].id, c);
        if (ok) st->insps[(size_t)st->cur].content = c;
    }
    if (ok) {
        refreshNotesCache();
        refreshPanel();
        int keep = st->cur;
        fillRecList(h, st, keep);              // 列表摘要同步更新
        showRecordInEditor(h, st);
        setRecHint(h, L"已保存(点击主窗口「保存」后写入 .ex5 文件)");
    } else {
        setRecHint(h, L"保存失败");
    }
}

// 当前记录的引用位置 -> 写入跳转请求
static void requestJump(RecState* st) {
    if (st->cur < 0) return;
    if (st->isNotes) {
        auto& n = st->notes[(size_t)st->cur];
        st->jumpChapter = n.chapterId;
        st->jumpHasRange = n.hasRange;
        st->jumpA = n.rangeStart;
        st->jumpB = n.rangeEnd;
    } else {
        st->jumpChapter = st->insps[(size_t)st->cur].chapterId;
        st->jumpHasRange = false;
        st->jumpA = st->jumpB = 0;
    }
    st->jumpRequested = true;
}

static void deleteRecord(HWND h, RecState* st) {
    if (st->cur < 0) return;
    // v1.1 编辑权限:仅作者本人可删除(SQL 层 user_id 也强制,这里先拦 UI)
    bool own = st->isNotes ? st->notes[(size_t)st->cur].own : st->insps[(size_t)st->cur].own;
    if (!own) {
        setRecHint(h, L"他人的记录不能删除(协议 v1.1 共享阅读:仅作者本人可编辑/删除)");
        return;
    }
    std::wstring q = st->isNotes
        ? (L"确定删除这条「" + std::wstring(kKindName[noteKind(st->notes[(size_t)st->cur])]) + L"」吗?")
        : L"确定删除这条「心得」吗?";
    if (MessageBoxW(h, q.c_str(), L"删除确认", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    bool ok = st->isNotes ? g_book.deleteNote(st->notes[(size_t)st->cur].id)
                          : g_book.deleteInspiration(st->insps[(size_t)st->cur].id);
    refreshNotesCache();
    paintHighlights();
    refreshPanel();
    fillRecList(h, st, st->cur);
    showRecordInEditor(h, st);
    setRecHint(h, ok ? L"记录已删除" : L"删除失败");
}

// ---------------- TXT 导出 ----------------
static std::string fmtTime(long long t) {
    time_t tt = (time_t)t;
    tm tmv{};
    localtime_s(&tmv, &tt);
    char b[32]{};
    snprintf(b, 31, "%04d-%02d-%02d %02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return b;
}

static void exportRecords(HWND hDlg, RecState* st) {
    wchar_t file[MAX_PATH]{};
    lstrcpynW(file, st->isNotes ? L"笔记本.txt" : L"心得集.txt", MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hDlg;
    ofn.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0所有文件\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    std::string out = "\xEF\xBB\xBF";   // UTF-8 BOM,记事本可正确识别
    out += st->isNotes ? "EX5 阅读器 · 笔记本导出\r\n" : "EX5 阅读器 · 心得集导出\r\n";
    out += "书名:《" + g_book.title() + "》\r\n\r\n";
    if (st->isNotes) {
        for (auto& n : g_book.listNotes()) {
            out += "【" + wu8(kKindName[noteKind(n)]) + "】第 " + std::to_string(n.chapterId)
                 + " 章  作者:" + (n.author.empty() ? "未知" : n.author)
                 + (n.own ? "(我)" : "") + "  (" + fmtTime(n.createTime) + ")\r\n";
            if (!n.original.empty()) out += "  原文:" + n.original + "\r\n";
            if (!n.content.empty())
                out += std::string(n.hasRange ? "  批注:" : "  内容:") + n.content + "\r\n";
            out += "----------------------------------------\r\n";
        }
    } else {
        for (auto& i : g_book.listInspirations()) {
            out += "【心得】第 " + std::to_string(i.chapterId) + " 章  作者:"
                 + (i.author.empty() ? "未知" : i.author) + (i.own ? "(我)" : "")
                 + "  (" + fmtTime(i.createTime) + ")\r\n";
            out += "  " + i.content + "\r\n";
            out += "----------------------------------------\r\n";
        }
    }
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        MessageBoxW(hDlg, L"无法写入该文件", L"导出失败", MB_ICONERROR);
        return;
    }
    DWORD written = 0;
    WriteFile(f, out.data(), (DWORD)out.size(), &written, nullptr);
    CloseHandle(f);
    setRecHint(hDlg, L"导出成功");
    MessageBoxW(hDlg, (L"已导出到:\r\n" + std::wstring(file)).c_str(),
                L"导出成功", MB_ICONINFORMATION);
}

// ---------------- 记录编辑器窗口过程 ----------------
// 编辑器布局(支持调整窗口大小/最大化):左列表满高,右栏自适应,按钮锚底
static void layoutRecEditor(HWND h, RecState* st, int w, int hgt) {
    int rx = 344, rw = w - rx - 12;
    if (rw < 200) rw = 200;
    MoveWindow(GetDlgItem(h, REC_LIST), 12, 12, 320, hgt - 24, TRUE);
    MoveWindow(GetDlgItem(h, IDCANCEL),   w - 126, hgt - 48, 110, 30, TRUE);
    MoveWindow(GetDlgItem(h, REC_HINT),   rx,        hgt - 70,  rw, 20, TRUE);
    MoveWindow(GetDlgItem(h, REC_SAVE),   rx,        hgt - 112, 110, 30, TRUE);
    MoveWindow(GetDlgItem(h, REC_JUMP),   rx + 118,  hgt - 112, 110, 30, TRUE);
    MoveWindow(GetDlgItem(h, REC_EXPORT), rx + 236,  hgt - 112, 110, 30, TRUE);
    int areaTop = 64, areaBot = hgt - 122;
    int areaH = areaBot - areaTop; if (areaH < 60) areaH = 60;
    if (st && st->commentMode) {   // 划线/摘抄:原文 + 批注双框均分
        int orgH = (areaH - 68) / 2; if (orgH < 40) orgH = 40;
        MoveWindow(GetDlgItem(h, REC_LBL_ORG),  rx, areaTop, rw, 18, TRUE);
        MoveWindow(GetDlgItem(h, REC_EDIT_ORG), rx, areaTop + 20, rw, orgH, TRUE);
        int cy = areaTop + 20 + orgH + 8;
        MoveWindow(GetDlgItem(h, REC_LBL_CMT),  rx, cy, rw, 18, TRUE);
        MoveWindow(GetDlgItem(h, REC_EDIT_CMT), rx, cy + 20, rw, areaBot - cy - 20, TRUE);
    } else {                       // 笔记/心得:单框铺满
        MoveWindow(GetDlgItem(h, REC_EDIT), rx, areaTop, rw, areaH, TRUE);
    }
}

static LRESULT CALLBACK RecProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    RecState* st = (RecState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)l;
        st = (RecState*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        HWND hLb = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            12, 12, 320, 458, h, (HMENU)REC_LIST, nullptr, nullptr);
        HWND hLt = CreateWindowW(L"STATIC", L"类型:", WS_CHILD | WS_VISIBLE,
            344, 16, 44, 20, h, nullptr, nullptr, nullptr);
        HWND hType = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            390, 16, 140, 20, h, (HMENU)REC_TYPE, nullptr, nullptr);
        HWND hLc = CreateWindowW(L"STATIC", L"章节:", WS_CHILD | WS_VISIBLE,
            536, 16, 44, 20, h, nullptr, nullptr, nullptr);
        HWND hCh = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            582, 16, 120, 20, h, (HMENU)REC_CHAPTER, nullptr, nullptr);
        // 第二行:创建时间(右侧详情展示)
        HWND hLt2 = CreateWindowW(L"STATIC", L"创建时间:", WS_CHILD | WS_VISIBLE,
            344, 40, 76, 20, h, nullptr, nullptr, nullptr);
        HWND hTime = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            424, 40, 240, 20, h, (HMENU)REC_TIME, nullptr, nullptr);
        HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            344, 64, 364, 316, h, (HMENU)REC_EDIT, nullptr, nullptr);
        // 划线/摘抄双框:原文(只读)+ 批注(可编辑),默认隐藏,选中划线/摘抄时显示
        HWND hLo = CreateWindowW(L"STATIC", L"原文(只读):", WS_CHILD,
            344, 64, 364, 18, h, (HMENU)REC_LBL_ORG, nullptr, nullptr);
        HWND hEo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            344, 84, 364, 118, h, (HMENU)REC_EDIT_ORG, nullptr, nullptr);
        HWND hLblC = CreateWindowW(L"STATIC", L"批注(可编辑):", WS_CHILD,
            344, 210, 364, 18, h, (HMENU)REC_LBL_CMT, nullptr, nullptr);
        HWND hEc = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            344, 230, 364, 150, h, (HMENU)REC_EDIT_CMT, nullptr, nullptr);
        HWND hSave = CreateWindowW(L"BUTTON", L"保存修改", WS_CHILD | WS_VISIBLE,
            344, 388, 110, 30, h, (HMENU)REC_SAVE, nullptr, nullptr);
        HWND hJump = CreateWindowW(L"BUTTON", L"跳转原文", WS_CHILD | WS_VISIBLE,
            462, 388, 110, 30, h, (HMENU)REC_JUMP, nullptr, nullptr);
        HWND hExp = CreateWindowW(L"BUTTON", L"导出 TXT", WS_CHILD | WS_VISIBLE,
            580, 388, 110, 30, h, (HMENU)REC_EXPORT, nullptr, nullptr);
        HWND hHint = CreateWindowW(L"STATIC", L"提示:右键列表项可跳转/删除/导出",
            WS_CHILD | WS_VISIBLE, 344, 432, 364, 20, h, (HMENU)REC_HINT, nullptr, nullptr);
        HWND hClose = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE,
            598, 452, 110, 30, h, (HMENU)IDCANCEL, nullptr, nullptr);
        for (HWND c : {hLb, hLt, hType, hLc, hCh, hEd, hLo, hEo, hLblC, hEc, hSave, hJump, hExp, hHint, hClose})
            SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        fillRecList(h, st, 0);
        showRecordInEditor(h, st);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == REC_LIST && HIWORD(w) == LBN_SELCHANGE) {
            LRESULT sel = SendMessageW(GetDlgItem(h, REC_LIST), LB_GETCURSEL, 0, 0);
            st->cur = (sel == LB_ERR) ? -1 : (int)sel;
            showRecordInEditor(h, st);
            bool own = true; std::string author;
            if (st->cur >= 0) {
                if (st->isNotes) { own = st->notes[(size_t)st->cur].own; author = st->notes[(size_t)st->cur].author; }
                else             { own = st->insps[(size_t)st->cur].own; author = st->insps[(size_t)st->cur].author; }
            }
            if (!own) {
                setRecHint(h, (L"「" + u8w(author) +
                    L"」的记录 —— 共享阅读(协议 v1.1):可读、可跳转,仅作者本人可编辑").c_str());
            } else {
                setRecHint(h, st->commentMode ? L"原文只读;批注可编辑,改完点「保存修改」"
                             : st->editable  ? L"可编辑,改完点「保存修改」"
                                             : L"请选择一条记录");
            }
        } else if (id == REC_SAVE) {
            saveRecord(h, st);
        } else if (id == REC_JUMP) {
            requestJump(st);
            if (st->jumpRequested) DestroyWindow(h);
        } else if (id == REC_EXPORT) {
            exportRecords(h, st);
        } else if (id == IDCANCEL) {
            DestroyWindow(h);
        }
        return 0;
    }
    // 列表右键 -> 跳转引用位置 / 删除 / 导出
    case WM_CONTEXTMENU: {
        if ((HWND)w == GetDlgItem(h, REC_LIST)) {
            POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            POINT pc = pt;
            ScreenToClient((HWND)w, &pc);
            LRESULT r = SendMessageW((HWND)w, LB_ITEMFROMPOINT, 0, MAKELPARAM(pc.x, pc.y));
            if (HIWORD(r) == 0) {
                SendMessageW((HWND)w, LB_SETCURSEL, (int)LOWORD(r), 0);
                st->cur = (int)LOWORD(r);
                showRecordInEditor(h, st);
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, RECM_JUMP,   L"跳转到引用位置");
                AppendMenuW(menu, MF_STRING, RECM_DEL,    L"删除这条记录");
                AppendMenuW(menu, MF_STRING, RECM_EXPORT, L"导出 TXT");
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         pt.x, pt.y, 0, h, nullptr);
                DestroyMenu(menu);
                if (cmd == RECM_JUMP) {
                    requestJump(st);
                    if (st->jumpRequested) DestroyWindow(h);
                } else if (cmd == RECM_DEL) {
                    deleteRecord(h, st);
                } else if (cmd == RECM_EXPORT) {
                    exportRecords(h, st);
                }
            }
            return 0;
        }
        break;
    }
    case WM_SIZE:
        if (st) layoutRecEditor(h, st, LOWORD(l), HIWORD(l));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)l;
        mmi->ptMinTrackSize.x = 600;
        mmi->ptMinTrackSize.y = 460;
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void showRecordList(const wchar_t* title, bool isNotes, long long singleId) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = RecProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"Ex5RecEdit";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    RecState st{};
    st.isNotes = isNotes;
    st.singleId = singleId;
    RECT pr; GetWindowRect(g_hwnd, &pr);
    int w = 724, hgt = 534;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - hgt) / 2;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"Ex5RecEdit", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_VISIBLE,
        x, y, w, hgt, g_hwnd, nullptr, GetModuleHandleW(nullptr), &st);
    if (!dlg) return;
    EnableWindow(g_hwnd, FALSE);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
    refreshNotesCache();
    paintHighlights();
    refreshPanel();
    // 对话框关闭后执行跳转(阅读区在主窗口,需先退出模态)
    if (st.jumpRequested) {
        jumpToRef(st.jumpChapter, st.jumpHasRange, st.jumpA, st.jumpB);
        SetWindowTextW(g_hStatus, L"已跳转到引用位置");
    }
}

// ---------------- 用户管理对话框(新建 / 切换) ----------------
struct UserDlgState {
    std::vector<ex5::Book::UserInfo> users;
};

static void fillUserList(HWND h, UserDlgState* st) {
    HWND hLb = GetDlgItem(h, 100);
    SendMessageW(hLb, LB_RESETCONTENT, 0, 0);
    st->users = g_book.listUsers();
    long long cur = g_book.currentUserId();
    int curSel = 0;
    for (size_t i = 0; i < st->users.size(); ++i) {
        auto& u = st->users[i];
        std::wstring line = (u.id == cur) ? L"√ " : L"   ";
        line += u8w(u.name);
        line += u.hasCipher ? L"  (有密码)" : L"  (无密码)";
        if (u.id == cur) { line += L"  [当前用户]"; curSel = (int)i; }
        SendMessageW(hLb, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    if (!st->users.empty()) SendMessageW(hLb, LB_SETCURSEL, curSel, 0);
}

// 切换成功后刷新与当前用户相关的所有视图
static void afterUserSwitched(HWND hDlg, UserDlgState* st) {
    refreshNotesCache();
    paintHighlights();
    refreshPanel();
    updateTitle();
    fillUserList(hDlg, st);
    SetWindowTextW(g_hStatus,
        (L"当前用户:" + u8w(g_book.currentUserName())).c_str());
}

static LRESULT CALLBACK UserProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    UserDlgState* st = (UserDlgState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)l;
        st = (UserDlgState*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        HWND hLb = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            12, 12, 556, 280, h, (HMENU)100, nullptr, nullptr);
        HWND hNew = CreateWindowW(L"BUTTON", L"新建用户", WS_CHILD | WS_VISIBLE,
            12, 302, 110, 30, h, (HMENU)101, nullptr, nullptr);
        HWND hSw = CreateWindowW(L"BUTTON", L"切换用户", WS_CHILD | WS_VISIBLE,
            132, 302, 110, 30, h, (HMENU)102, nullptr, nullptr);
        HWND hClose = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE,
            462, 302, 100, 30, h, (HMENU)IDCANCEL, nullptr, nullptr);
        for (HWND c : {hLb, hNew, hSw, hClose}) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        fillUserList(h, st);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 101) {                 // 新建用户
            std::string name, pw;
            EnableWindow(h, FALSE);
            bool okName = promptText(L"新建用户", L"用户名:", false, name);
            bool okPw = false;
            if (okName && !name.empty())
                okPw = promptText(L"设置密码", L"密码(留空则不设密码):", false, pw, true);
            EnableWindow(h, TRUE);
            SetForegroundWindow(h);
            if (okName && !name.empty() && okPw) {
                std::string err;
                long long nid = g_book.createUser(name, pw, err);
                if (nid < 0) {
                    MessageBoxW(h, u8w(err).c_str(), L"新建失败", MB_ICONERROR);
                } else {
                    std::string err2;
                    if (g_book.switchUser(nid, pw, err2)) {
                        afterUserSwitched(h, st);
                    } else {
                        fillUserList(h, st);
                        MessageBoxW(h, u8w(err2).c_str(), L"提示", MB_ICONWARNING);
                    }
                }
            }
        } else if (LOWORD(w) == 102) {          // 切换用户
            HWND hLb = GetDlgItem(h, 100);
            LRESULT sel = SendMessageW(hLb, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && sel < (LRESULT)st->users.size()) {
                auto& u = st->users[(size_t)sel];
                std::string pw;
                bool go = true;
                if (u.hasCipher) {
                    EnableWindow(h, FALSE);
                    go = promptText(L"切换用户",
                        (L"请输入「" + u8w(u.name) + L"」的密码:").c_str(), false, pw, true);
                    EnableWindow(h, TRUE);
                    SetForegroundWindow(h);
                }
                if (go) {
                    std::string err;
                    if (g_book.switchUser(u.id, pw, err)) {
                        afterUserSwitched(h, st);
                    } else {
                        MessageBoxW(h, u8w(err).c_str(), L"切换失败", MB_ICONERROR);
                    }
                }
            }
        } else if (LOWORD(w) == IDCANCEL) {
            DestroyWindow(h);
        }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void showUserDialog() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = UserProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"Ex5Users";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    UserDlgState st{};
    RECT pr; GetWindowRect(g_hwnd, &pr);
    int w = 584, hgt = 384;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - hgt) / 2;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"Ex5Users", L"用户管理",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, w, hgt,
        g_hwnd, nullptr, GetModuleHandleW(nullptr), &st);
    if (!dlg) return;
    EnableWindow(g_hwnd, FALSE);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
    refreshNotesCache();
    paintHighlights();
    refreshPanel();
    updateTitle();
}

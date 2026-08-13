// gui_reader.cpp — 阅读区:流式加载(上下双向)、高亮、悬停气泡、划线/摘抄、跳转
#include "gui_common.h"

// ---------------- 偏移映射 ----------------
void buildMap() {
    auto cps = utf8::decode(g_text);
    g_cpToU16.assign(cps.size() + 1, 0);
    long pos = 0;
    for (size_t i = 0; i < cps.size(); ++i) {
        g_cpToU16[i] = pos;
        pos += (cps[i] > 0xFFFF) ? 2 : 1;
    }
    g_cpToU16[cps.size()] = pos;
}
long u16ToCp(long u16pos) {
    auto it = std::upper_bound(g_cpToU16.begin(), g_cpToU16.end(), u16pos);
    long r = (long)(it - g_cpToU16.begin()) - 1;
    return r < 0 ? 0 : r;
}

// 显示区码点位置 -> (章节 index, 章内码点偏移);落在标题/分隔线上返回 false
bool dispToChapter(long p, int& chapter, long& off) {
    for (auto& s : g_segs) {
        if (p >= s.textStart && p < s.textStart + s.textLen) {
            chapter = s.chapter;
            off = p - s.textStart;
            return true;
        }
    }
    return false;
}
const Seg* findSeg(int chapter) {
    for (auto& s : g_segs) if (s.chapter == chapter) return &s;
    return nullptr;
}

// 笔记缓存:内容变更时调用
void refreshNotesCache() {
    g_notesCache.clear();
    if (g_open) g_notesCache = g_book.listNotes();
}

// ---------------- 连续阅读区(流式加载) ----------------
// 追加一个章节段落:横线分隔 + 章节标题 + 正文
static void appendSeg(int chapPos) {
    const auto& chs = g_book.chapters();
    if (chapPos < 0 || chapPos >= (int)chs.size()) return;
    const ex5::Chapter& c = chs[(size_t)chapPos];

    std::string header = "\n──────────── "
                       + c.title + " ────────────\n\n";
    std::string body = g_book.chapterText(c.index);

    Seg s;
    s.chapter   = c.index;
    s.textStart = (long)utf8::charCount(g_text + header);
    s.textLen   = (long)utf8::charCount(body);
    g_text += header;
    g_text += body;
    g_text += "\n";
    g_segs.push_back(s);
    g_nextToLoad = chapPos + 1;
}

// 在阅读区开头插入一个更早的章节段落(向上流式加载),返回插入块码点长度
static long prependSeg(int chapPos) {
    const auto& chs = g_book.chapters();
    if (chapPos < 0 || chapPos >= (int)chs.size()) return 0;
    const ex5::Chapter& c = chs[(size_t)chapPos];
    std::string header = "\n──────────── "
                       + c.title + " ────────────\n\n";
    std::string body = g_book.chapterText(c.index);
    std::string block = header + body + "\n";
    long addedCp = (long)utf8::charCount(block);

    Seg s;
    s.chapter   = c.index;
    s.textStart = (long)utf8::charCount(header);
    s.textLen   = (long)utf8::charCount(body);
    for (auto& old : g_segs) old.textStart += addedCp;   // 原有段落整体后移
    g_text = block + g_text;
    g_segs.insert(g_segs.begin(), s);
    return addedCp;
}

// 从 chapters() 的 chapPos 位置开始重建阅读区(连带加载下一章)
void loadChaptersFrom(int chapPos, bool scrollTop) {
    if (!g_open) return;
    const auto& chs = g_book.chapters();
    if (chapPos < 0 || chapPos >= (int)chs.size()) return;

    g_text.clear();
    g_segs.clear();
    appendSeg(chapPos);
    appendSeg(chapPos + 1);   // 第一章加载完成后,下一章一并加载

    buildMap();
    SetWindowTextW(g_hEdit, u8w(g_text).c_str());
    applyTextStyle();      // 重设文本后恢复字体/字号/颜色/对齐
    paintHighlights();

    g_chapter = chs[(size_t)chapPos].index;
    g_book.recordProgress(g_chapter, 0, (long long)utf8::charCount(g_book.chapterText(g_chapter)));
    refreshPanel();
    if (scrollTop) SendMessageW(g_hEdit, EM_SETSEL, 0, 0),
                   SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);
    ensureFilled();   // 可视区未填满时继续补载
}

// 滚动到底部时续载下一章,保持当前视图位置
static void appendNextChapter() {
    if (!g_open || g_refilling) return;
    if (g_nextToLoad >= (int)g_book.chapters().size()) return;
    g_refilling = true;

    int firstLine = (int)SendMessageW(g_hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    DWORD s = 0, e = 0;
    SendMessageW(g_hEdit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);

    appendSeg(g_nextToLoad);
    buildMap();
    SetWindowTextW(g_hEdit, u8w(g_text).c_str());
    applyTextStyle();      // 重设文本后恢复字体/字号/颜色/对齐
    paintHighlights();

    SendMessageW(g_hEdit, EM_SETSEL, s, e);
    int nowFirst = (int)SendMessageW(g_hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(g_hEdit, EM_LINESCROLL, 0, firstLine - nowFirst);

    std::wstring t = L"已续载:第 " + std::to_wstring(g_book.chapters()[(size_t)g_nextToLoad - 1].index) + L" 章";
    SetWindowTextW(g_hStatus, t.c_str());
    g_refilling = false;
}

// 滚动到顶部时向前补载上一章,保持当前视图位置不跳
static void prependPrevChapter() {
    if (!g_open || g_refilling || g_segs.empty()) return;
    const auto& chs = g_book.chapters();
    int firstPos = -1;
    for (size_t i = 0; i < chs.size(); ++i)
        if (chs[i].index == g_segs.front().chapter) { firstPos = (int)i; break; }
    if (firstPos <= 0) return;                  // 前面没有更早的章节
    g_refilling = true;

    int oldFirst = (int)SendMessageW(g_hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    int oldLines = (int)SendMessageW(g_hEdit, EM_GETLINECOUNT, 0, 0);
    DWORD s = 0, e = 0;
    SendMessageW(g_hEdit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);

    long addedCp = prependSeg(firstPos - 1);
    buildMap();
    SetWindowTextW(g_hEdit, u8w(g_text).c_str());
    applyTextStyle();      // 重设文本后恢复字体/字号/颜色/对齐
    paintHighlights();

    int addedLines = (int)SendMessageW(g_hEdit, EM_GETLINECOUNT, 0, 0) - oldLines;
    long addedU16 = g_cpToU16[(size_t)addedCp]; // 插入块的 UTF-16 长度
    SendMessageW(g_hEdit, EM_SETSEL, s + (DWORD)addedU16, e + (DWORD)addedU16);
    int nowFirst = (int)SendMessageW(g_hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(g_hEdit, EM_LINESCROLL, 0, oldFirst + addedLines - nowFirst);

    std::wstring t = L"已向前续载:第 " + std::to_wstring(chs[(size_t)firstPos - 1].index) + L" 章";
    SetWindowTextW(g_hStatus, t.c_str());
    g_refilling = false;
}

// 滚动到两端时的续载检查(滚到底向下续载,滚到顶向上续载)
void checkScrollEdges() {
    if (!g_open || g_refilling) return;
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(g_hEdit, SB_VERT, &si);
    if (si.nMax > 0 && si.nPos + (int)si.nPage >= si.nMax)
        appendNextChapter();
    else if (si.nPos <= si.nMin)
        prependPrevChapter();
}

// 窗口变大(如最大化)后内容填不满可视区时,连续补载后续章节
void ensureFilled() {
    if (!g_open || g_refilling) return;
    for (int guard = 0; guard < 500; ++guard) {
        if (g_nextToLoad >= (int)g_book.chapters().size()) break;
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE;
        GetScrollInfo(g_hEdit, SB_VERT, &si);
        if ((long long)si.nMax + 1 > (long long)si.nPage) break;  // 已有超出可视区的内容
        appendNextChapter();
    }
}

// ---------------- 正文渲染与高亮 ----------------
void paintHighlights() {
    if (!g_open || g_segs.empty()) return;
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BACKCOLOR;
    cf.dwEffects = CFE_AUTOBACKCOLOR;
    SendMessageW(g_hEdit, EM_SETSEL, 0, -1);
    SendMessageW(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    cf.dwEffects = 0;
    cf.crBackColor = RGB(255, 246, 160);
    for (auto& n : g_book.listNotes()) {
        if (!n.hasRange) continue;
        const Seg* seg = findSeg(n.chapterId);
        if (!seg) continue;
        long a = seg->textStart + (long)n.rangeStart;
        long b = seg->textStart + (long)n.rangeEnd;
        if (a < 0 || b >= (long)g_cpToU16.size()) continue;
        SendMessageW(g_hEdit, EM_SETSEL, g_cpToU16[(size_t)a], g_cpToU16[(size_t)b]);
        SendMessageW(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    SendMessageW(g_hEdit, EM_SETSEL, 0, 0);
}

// ---------------- 悬停气泡 ----------------
// 鼠标悬停在划线/摘抄上时,按类型显示批注或原文
void updateHoverTip(POINT clientPt) {
    if (!g_open) return;
    POINTL ptl{ clientPt.x, clientPt.y };
    long ci = (long)SendMessageW(g_hEdit, EM_CHARFROMPOS, 0, (LPARAM)&ptl);
    long cp = u16ToCp(ci);
    int ch = 0; long off = 0;
    const ex5::Note* hit = nullptr;
    if (dispToChapter(cp, ch, off)) {
        for (auto& n : g_notesCache) {
            if (n.chapterId == ch && n.hasRange &&
                off >= n.rangeStart && off < n.rangeEnd) { hit = &n; break; }
        }
    }
    if (hit) {
        int kind = hit->content.empty() ? 2 : 1;
        std::wstring text = L"【";
        text += kKindName[kind];
        if (!hit->own && !hit->author.empty()) { text += L"·"; text += u8w(hit->author); }  // 他人划线标作者
        text += L"】";
        if (!hit->content.empty()) text += u8w(hit->content);
        else {
            std::string o = hit->original;
            if (o.size() > 80) o = o.substr(0, 80) + "...";
            text += u8w(o);
        }
        POINT scr = clientPt;
        ClientToScreen(g_hEdit, &scr);
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        ti.hwnd = g_hEdit;
        ti.uId = 1;
        ti.lpszText = (LPWSTR)text.c_str();
        if (g_tipNoteId != hit->id || !g_tipActive) {
            SendMessageW(g_hTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
            SendMessageW(g_hTip, TTM_TRACKPOSITION, 0, MAKELPARAM(scr.x + 12, scr.y + 20));
            SendMessageW(g_hTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
            g_tipNoteId = hit->id;
            g_tipActive = true;
        } else {
            SendMessageW(g_hTip, TTM_TRACKPOSITION, 0, MAKELPARAM(scr.x + 12, scr.y + 20));
        }
    } else if (g_tipActive) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = g_hEdit;
        ti.uId = 1;
        SendMessageW(g_hTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        g_tipActive = false;
        g_tipNoteId = -1;
    }
}

// ---------------- 划线 / 摘抄 ----------------
// 选区(显示区 UTF-16 位置)-> (章节, 章内码点区间);跨章/落在标题返回 false
bool selectionToChapterRange(int& chIdx, long& a, long& b) {
    DWORD s = 0, e = 0;
    SendMessageW(g_hEdit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (e <= s) return false;
    long cpa = u16ToCp((long)s), cpb = u16ToCp((long)e);
    int chA = 0, chB = 0; long offA = 0, offB = 0;
    if (!dispToChapter(cpa, chA, offA) || !dispToChapter(cpb, chB, offB)) return false;
    if (chA != chB) return false;   // 跨章节
    chIdx = chA; a = offA; b = offB;
    return true;
}

void markSelection(bool withComment) {
    if (!g_open) return;
    int chIdx = 0; long a = 0, b = 0;
    if (!selectionToChapterRange(chIdx, a, b)) {
        MessageBoxW(g_hwnd, L"请先在某一章的正文内选中一段文字(跨章节或标题行不可用)。",
                    L"提示", MB_ICONINFORMATION);
        return;
    }
    std::string chapText = g_book.chapterText(chIdx);
    std::string original = utf8::charSubstr(chapText, (size_t)a, (size_t)b);
    std::string comment;
    if (withComment) {
        std::string hint = "划线原文:" + original;
        if (hint.size() > 60) hint = hint.substr(0, 60) + "...";
        if (!promptText(L"划线批注(可直接确定留空)", u8w(hint).c_str(), false, comment))
            return;
    }
    long long id = g_book.addNote(comment, chIdx, true, a, b, original);
    if (id < 0) {
        MessageBoxW(g_hwnd, L"写入失败", L"错误", MB_ICONERROR);
        return;
    }
    shareNoticeOnce();   // v1.1:首次创建批注时告知共享可见性
    g_chapter = chIdx;
    refreshNotesCache();
    paintHighlights();
    refreshPanel();
    std::wstring msg = withComment ? L"已划线并保存批注" : L"已摘抄";
    SetWindowTextW(g_hStatus, (msg + L":#" + std::to_wstring(id)).c_str());
}

// ---------------- 跳转引用 ----------------
// 跳转到指定章节(该章未加载时自动重建阅读区)并选中引用范围
void jumpToRef(int chapterIdx, bool hasRange, long long ra, long long rb) {
    if (!g_open) return;
    if (!findSeg(chapterIdx)) {
        const auto& chs = g_book.chapters();
        for (int i = 0; i < (int)chs.size(); ++i)
            if (chs[i].index == chapterIdx) { loadChaptersFrom(i, false); break; }
    }
    const Seg* seg = findSeg(chapterIdx);
    if (!seg) return;
    long a16 = g_cpToU16[(size_t)seg->textStart], b16 = a16;
    if (hasRange) {
        long ca = seg->textStart + (long)ra, cb = seg->textStart + (long)rb;
        if (ca >= 0 && cb < (long)g_cpToU16.size()) {
            a16 = g_cpToU16[(size_t)ca];
            b16 = g_cpToU16[(size_t)cb];
        }
    }
    SendMessageW(g_hEdit, EM_SETSEL, a16, b16);
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);
    SetFocus(g_hEdit);
    if (g_chapter != chapterIdx) { g_chapter = chapterIdx; refreshPanel(); }
    // 左侧章节列表同步选中(LB_SETCURSEL 不会触发 LBN_SELCHANGE)
    const auto& chs = g_book.chapters();
    for (int i = 0; i < (int)chs.size(); ++i)
        if (chs[i].index == chapterIdx) { SendMessageW(g_hList, LB_SETCURSEL, i, 0); break; }
}

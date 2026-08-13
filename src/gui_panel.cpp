// gui_panel.cpp — 右侧笔记面板:当前章节的划线/摘抄/笔记/心得分页显示、点击跳转
#include "gui_common.h"

const wchar_t* kKindName[] = {L"", L"划线", L"摘抄", L"笔记", L"心得"};

void refreshPanel() {
    if (!g_hPanel) return;
    SendMessageW(g_hPanel, LB_RESETCONTENT, 0, 0);
    g_panelItems.clear();
    if (!g_open || g_chapter <= 0) return;
    for (auto& n : g_book.listNotes()) {
        if (n.chapterId != g_chapter) continue;
        int kind = (n.hasRange && n.content.empty()) ? 2 : n.hasRange ? 1 : 3;
        if (g_panelFilter && kind != g_panelFilter) continue;
        std::wstring line = L"【";
        line += kKindName[kind];
        if (!n.own && !n.author.empty()) { line += L"·"; line += u8w(n.author); }  // 他人记录标作者
        line += L"】";
        if (!n.original.empty()) line += u8w(n.original);
        if (!n.content.empty()) {
            if (!n.original.empty()) line += L" —— ";
            line += u8w(n.content);
        }
        if (line.size() > 60) line = line.substr(0, 60) + L"…";
        g_panelItems.push_back({kind, n.id, n.hasRange, n.rangeStart, n.rangeEnd, n.own});
        SendMessageW(g_hPanel, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    for (auto& i : g_book.listInspirations()) {
        if (i.chapterId != g_chapter) continue;
        if (g_panelFilter && g_panelFilter != 4) continue;
        std::wstring line = L"【心得";
        if (!i.own && !i.author.empty()) { line += L"·"; line += u8w(i.author); }
        line += L"】" + u8w(i.content);
        if (line.size() > 60) line = line.substr(0, 60) + L"…";
        g_panelItems.push_back({4, i.id, false, 0, 0, i.own});
        SendMessageW(g_hPanel, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

// 面板条目点击 -> 跳转原文(面板只显示当前章节,该章必然已在阅读区)
void panelJumpToSelection() {
    LRESULT sel = SendMessageW(g_hPanel, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel >= (LRESULT)g_panelItems.size()) return;
    auto& it = g_panelItems[(size_t)sel];
    jumpToRef(g_chapter, it.hasRange, it.a, it.b);
    std::wstring t = L"已定位到";
    t += (it.kind == 4) ? L"心得" : (it.hasRange ? L"原文(偏移 " +
         std::to_wstring(it.a) + L"-" + std::to_wstring(it.b) + L")" : L"笔记");
    SetWindowTextW(g_hStatus, t.c_str());
}

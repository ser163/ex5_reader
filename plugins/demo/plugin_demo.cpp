// plugin_demo.cpp — demo 插件「阅读统计」
// 演示插件规范(docs/插件规范.md)的最小可用实现:
// 导出 Ex5PluginQuery / Ex5PluginInit / Ex5PluginRun / Ex5PluginShutdown,
// 点击「插件 -> 阅读统计」时弹出当前书籍的章节与笔记统计。
#include "../../src/ex5_plugin.h"
#include <stdio.h>

static const Ex5HostApi* g_host = nullptr;

static const wchar_t* kName = L"阅读统计(demo)";
static const wchar_t* kVersion = L"1.0";
static const wchar_t* kAuthor = L"EX5 Reader Project";
static const wchar_t* kDesc = L"统计当前书籍的章节数、划线/摘抄/笔记与心得数量";

extern "C" {

__declspec(dllexport) BOOL WINAPI Ex5PluginQuery(Ex5PluginInfo* info) {
    if (!info) return FALSE;
    info->apiVersion  = EX5_PLUGIN_API_VERSION;
    info->name        = kName;
    info->version     = kVersion;
    info->author      = kAuthor;
    info->description = kDesc;
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI Ex5PluginInit(const Ex5HostApi* host) {
    if (!host || host->apiVersion != EX5_PLUGIN_API_VERSION) return FALSE;
    g_host = host;
    return TRUE;
}

__declspec(dllexport) void WINAPI Ex5PluginRun(void) {
    if (!g_host) return;
    if (!g_host->bookPath()) {
        g_host->messageBox(L"请先打开一本 .ex5 电子书。", kName, MB_ICONINFORMATION);
        return;
    }
    wchar_t buf[640];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE,
        L"《%s》\r\n\r\n"
        L"章节总数:%d\r\n"
        L"当前章节:第 %d 章\r\n"
        L"划线 / 摘抄 / 笔记:%d 条\r\n"
        L"心得:%d 条\r\n\r\n"
        L"—— 由 demo 插件「阅读统计」生成",
        g_host->bookTitle(),
        g_host->chapterCount(),
        g_host->currentChapter(),
        g_host->noteCount(),
        g_host->inspirationCount());
    g_host->messageBox(buf, kName, MB_ICONINFORMATION);
    g_host->setStatus(L"demo 插件「阅读统计」已运行");
}

__declspec(dllexport) void WINAPI Ex5PluginShutdown(void) {
    g_host = nullptr;
}

} // extern "C"

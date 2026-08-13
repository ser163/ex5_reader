// gui_plugin.cpp — 插件宿主:扫描 exe 同目录 plugins\*.dll,加载并挂到「插件」菜单
#include "gui_common.h"
#include "ex5_plugin.h"

struct Plugin {
    HMODULE dll = nullptr;
    std::wstring name;
    void (WINAPI* run)() = nullptr;
    void (WINAPI* shutdown)() = nullptr;
};
static std::vector<Plugin> g_plugins;

// ---------------- 宿主能力实现 ----------------
static HWND WINAPI hostMainWindow() { return g_hwnd; }

static const wchar_t* hostBookPath() {
    return g_open ? g_bookPath.c_str() : nullptr;
}
static const wchar_t* hostBookTitle() {
    static std::wstring t;
    if (!g_open) return nullptr;
    t = u8w(g_book.title());
    return t.c_str();
}
static int WINAPI hostChapterCount()    { return g_open ? (int)g_book.chapters().size() : 0; }
static int WINAPI hostCurrentChapter()  { return g_open ? g_chapter : 0; }
static int WINAPI hostNoteCount()       { return g_open ? (int)g_book.listNotes().size() : 0; }
static int WINAPI hostInspirationCount(){ return g_open ? (int)g_book.listInspirations().size() : 0; }
static void WINAPI hostSetStatus(const wchar_t* text) {
    if (text) SetWindowTextW(g_hStatus, text);
}
static int WINAPI hostMessageBox(const wchar_t* text, const wchar_t* title, unsigned int flags) {
    return MessageBoxW(g_hwnd, text ? text : L"", title ? title : L"插件", flags);
}

static Ex5HostApi makeHostApi() {
    Ex5HostApi api{};
    api.apiVersion       = EX5_PLUGIN_API_VERSION;
    api.mainWindow       = hostMainWindow;
    api.bookPath         = hostBookPath;
    api.bookTitle        = hostBookTitle;
    api.chapterCount     = hostChapterCount;
    api.currentChapter   = hostCurrentChapter;
    api.noteCount        = hostNoteCount;
    api.inspirationCount = hostInspirationCount;
    api.setStatus        = hostSetStatus;
    api.messageBox       = hostMessageBox;
    return api;
}

// 宿主能力表必须长期有效(插件会保存指针),不能是栈上临时对象
static Ex5HostApi g_hostApi = makeHostApi();

// ---------------- 扫描与加载 ----------------
void pluginsBuildMenu(HMENU hBar) {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir = buf;
    size_t pos = dir.find_last_of(L'\\');
    if (pos != std::wstring::npos) dir = dir.substr(0, pos);
    std::wstring pattern = dir + L"\\plugins\\*.dll";

    const Ex5HostApi* api = &g_hostApi;
    HMENU hMenu = nullptr;

    WIN32_FIND_DATAW fd{};
    HANDLE hf = FindFirstFileW(pattern.c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring full = dir + L"\\plugins\\" + fd.cFileName;
            HMODULE dll = LoadLibraryW(full.c_str());
            if (!dll) continue;
            auto query    = (BOOL (WINAPI*)(Ex5PluginInfo*))GetProcAddress(dll, "Ex5PluginQuery");
            auto init     = (BOOL (WINAPI*)(const Ex5HostApi*))GetProcAddress(dll, "Ex5PluginInit");
            auto run      = (void (WINAPI*)())GetProcAddress(dll, "Ex5PluginRun");
            auto shutdown = (void (WINAPI*)())GetProcAddress(dll, "Ex5PluginShutdown");
            if (!query || !init || !run) { FreeLibrary(dll); continue; }

            Ex5PluginInfo info{};
            if (!query(&info) || info.apiVersion != EX5_PLUGIN_API_VERSION || !info.name) {
                FreeLibrary(dll); continue;
            }
            if (!init(api)) { FreeLibrary(dll); continue; }

            Plugin p;
            p.dll = dll;
            p.name = info.name;
            p.run = run;
            p.shutdown = shutdown;
            g_plugins.push_back(p);
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }

    if (!g_plugins.empty()) {
        hMenu = CreatePopupMenu();
        for (size_t i = 0; i < g_plugins.size(); ++i)
            AppendMenuW(hMenu, MF_STRING, IDM_PLUGIN_BASE + (UINT)i, g_plugins[i].name.c_str());
        AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hMenu, L"插件(&P)");
    }
}

// 命中插件菜单命令则执行并返回 true(在 WM_COMMAND 最前面调用)
bool pluginCommand(int id) {
    int idx = id - IDM_PLUGIN_BASE;
    if (idx < 0 || idx >= (int)g_plugins.size()) return false;
    if (g_plugins[(size_t)idx].run) g_plugins[(size_t)idx].run();
    return true;
}

void pluginsShutdown() {
    for (auto& p : g_plugins) {
        if (p.shutdown) p.shutdown();
        FreeLibrary(p.dll);
    }
    g_plugins.clear();
}

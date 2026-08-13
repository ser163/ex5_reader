// gui_style.cpp — 文字样式:字体/字号/文字颜色/背景色/对齐/夜间模式
// 持久化到本地配置文件 ex5reader.ini(优先 exe 同目录,不可写时退回 %LOCALAPPDATA%\EX5Reader\)
#include "gui_common.h"

static std::wstring g_fontFace = L"Microsoft YaHei";
static int      g_fontSizePt = 14;
static COLORREF g_textColor = RGB(0, 0, 0);
static COLORREF g_bgColor = RGB(255, 255, 255);   // 阅读区背景色
static WORD     g_align = PFA_LEFT;               // PFA_LEFT / PFA_CENTER / PFA_RIGHT
static bool     g_night = false;                  // 夜间模式

// 夜间模式调色板(开夜间时覆盖自定义文字/背景色,自定义值仍保留)
static const COLORREF kNightText = RGB(210, 210, 210);
static const COLORREF kNightBg   = RGB(32, 32, 36);

bool isNightMode() { return g_night; }
WORD styleAlign()  { return g_align; }

// ---------------- 配置文件 ----------------
static std::wstring configFallbackDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring d = n ? buf : L".";
    d += L"\\EX5Reader";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
static std::wstring configPath(bool forWrite) {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir = buf;
    size_t pos = dir.find_last_of(L'\\');
    if (pos != std::wstring::npos) dir = dir.substr(0, pos);
    std::wstring f = dir + L"\\ex5reader.ini";
    if (!forWrite) {
        if (GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES) return f;
        std::wstring fb = configFallbackDir() + L"\\ex5reader.ini";
        if (GetFileAttributesW(fb.c_str()) != INVALID_FILE_ATTRIBUTES) return fb;
        return f;   // 都不存在:读出默认值
    }
    HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return f; }
    return configFallbackDir() + L"\\ex5reader.ini";
}

void loadTextSettings() {
    std::wstring f = configPath(false);    wchar_t face[LF_FACESIZE]{};
    GetPrivateProfileStringW(L"text", L"face", L"", face, LF_FACESIZE, f.c_str());
    if (face[0]) g_fontFace = face;
    int v = GetPrivateProfileIntW(L"text", L"size", 0, f.c_str());
    if (v >= 8 && v <= 48) g_fontSizePt = v;
    g_textColor = (COLORREF)GetPrivateProfileIntW(L"text", L"color", (int)g_textColor, f.c_str());
    g_bgColor   = (COLORREF)GetPrivateProfileIntW(L"text", L"bgcolor", (int)g_bgColor, f.c_str());
    v = GetPrivateProfileIntW(L"text", L"align", 0, f.c_str());
    if (v >= 1 && v <= 3) g_align = (WORD)v;
    g_night = GetPrivateProfileIntW(L"text", L"night", 0, f.c_str()) != 0;
}

void saveTextSettings() {
    std::wstring f = configPath(true);
    auto putNum = [&](const wchar_t* key, int v) {
        wchar_t b[16]{};
        _itow_s(v, b, 10);
        WritePrivateProfileStringW(L"text", key, b, f.c_str());
    };
    WritePrivateProfileStringW(L"text", L"face", g_fontFace.c_str(), f.c_str());
    putNum(L"size", g_fontSizePt);
    putNum(L"color", (int)g_textColor);
    putNum(L"bgcolor", (int)g_bgColor);
    putNum(L"align", (int)g_align);
    putNum(L"night", g_night ? 1 : 0);
}

// ---------------- 视图模式([view] mode:0=舒心 1=极简) ----------------
int iniGetViewMode() {
    std::wstring f = configPath(false);
    int v = GetPrivateProfileIntW(L"view", L"mode", 0, f.c_str());
    return (v == 1) ? 1 : 0;
}
void iniSetViewMode(int mode) {
    std::wstring f = configPath(true);
    WritePrivateProfileStringW(L"view", L"mode", mode == 1 ? L"1" : L"0", f.c_str());
}

// ---------------- 应用样式 ----------------
// 把当前字体/字号/颜色/背景色/对齐应用到全部正文(换文本后需重调)
void applyTextStyle() {
    if (!g_hEdit) return;
    COLORREF txt = g_night ? kNightText : g_textColor;
    COLORREF bg  = g_night ? kNightBg   : g_bgColor;
    SendMessageW(g_hEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)bg);
    DWORD s = 0, e = 0;
    SendMessageW(g_hEdit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET;
    cf.yHeight = g_fontSizePt * 20;              // 单位:twips(1 磅 = 20)
    cf.crTextColor = txt;
    cf.bCharSet = DEFAULT_CHARSET;
    lstrcpynW(cf.szFaceName, g_fontFace.c_str(), LF_FACESIZE);
    SendMessageW(g_hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = g_align;
    SendMessageW(g_hEdit, EM_SETSEL, 0, -1);
    SendMessageW(g_hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SendMessageW(g_hEdit, EM_SETSEL, s, e);      // 还原选区
}

// ---------------- 交互操作 ----------------
void zoomFont(int delta) {
    int ns = g_fontSizePt + delta;
    if (ns < 8) ns = 8;
    if (ns > 48) ns = 48;
    if (ns == g_fontSizePt) return;
    g_fontSizePt = ns;
    applyTextStyle();
    saveTextSettings();
    SetWindowTextW(g_hStatus, (L"字号:" + std::to_wstring(g_fontSizePt) + L" 磅").c_str());
}

void chooseFontDlg() {
    HDC hdc = GetDC(g_hwnd);
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(g_fontSizePt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfCharSet = DEFAULT_CHARSET;
    ReleaseDC(g_hwnd, hdc);
    lstrcpynW(lf.lfFaceName, g_fontFace.c_str(), LF_FACESIZE);
    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = g_hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST;
    cf.nFontType = SCREEN_FONTTYPE;
    if (ChooseFontW(&cf)) {
        g_fontFace = lf.lfFaceName;
        if (cf.iPointSize > 0) g_fontSizePt = cf.iPointSize / 10;
        applyTextStyle();
        saveTextSettings();
        SetWindowTextW(g_hStatus,
            (L"字体:" + g_fontFace + L"  " + std::to_wstring(g_fontSizePt) + L" 磅").c_str());
    }
}

// background=false 改文字颜色,true 改阅读区背景色
void chooseColorDlg(bool background) {
    static COLORREF cust[16]{};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g_hwnd;
    cc.rgbResult = background ? g_bgColor : g_textColor;
    cc.lpCustColors = cust;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        if (background) g_bgColor = cc.rgbResult;
        else            g_textColor = cc.rgbResult;
        applyTextStyle();
        saveTextSettings();
        SetWindowTextW(g_hStatus, background ? L"背景色已更新" : L"文字颜色已更新");
    }
}

void setAlign(WORD a) {
    g_align = a;
    applyTextStyle();
    saveTextSettings();
    const wchar_t* n = (a == PFA_CENTER) ? L"居中" : (a == PFA_RIGHT) ? L"右对齐" : L"左对齐";
    SetWindowTextW(g_hStatus, (std::wstring(L"正文对齐:") + n).c_str());
}

void toggleNightMode() {
    g_night = !g_night;
    applyTextStyle();
    saveTextSettings();
    SetWindowTextW(g_hStatus, g_night ? L"夜间模式:开(深色背景/浅色文字)"
                                      : L"夜间模式:关(恢复自定义颜色)");
}

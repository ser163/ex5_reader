// ex5_plugin.h — EX5 阅读器插件 SDK(公开头文件,插件作者只需要这个文件)
//
// 插件是一个 Windows DLL,放在阅读器 exe 同目录的 plugins\ 子目录下,
// 启动时被自动扫描加载,名称出现在菜单栏「插件」下,点击即调用 Ex5PluginRun()。
// 详细规范见 docs/插件规范.md。
#ifndef EX5_PLUGIN_H
#define EX5_PLUGIN_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EX5_PLUGIN_API_VERSION 1

// 插件基本信息(Ex5PluginQuery 填写;字符串指针指向插件内的静态/常量字符串)
typedef struct Ex5PluginInfo {
    int             apiVersion;   // 必须填 EX5_PLUGIN_API_VERSION
    const wchar_t*  name;         // 显示在「插件」菜单中的名称(必填)
    const wchar_t*  version;      // 例如 L"1.0"
    const wchar_t*  author;       // 作者
    const wchar_t*  description;  // 一句话描述
} Ex5PluginInfo;

// 宿主(阅读器)提供给插件的能力。指针在进程生命周期内有效。
typedef struct Ex5HostApi {
    int             apiVersion;                          // 宿主当前 API 版本
    HWND            (WINAPI* mainWindow)(void);          // 主窗口句柄(可作为对话框父窗口)
    const wchar_t*  (WINAPI* bookPath)(void);            // 当前 .ex5 路径;未打开返回 NULL
    const wchar_t*  (WINAPI* bookTitle)(void);           // 书名;未打开返回 NULL
    int             (WINAPI* chapterCount)(void);        // 章节总数
    int             (WINAPI* currentChapter)(void);      // 当前章节 index;未打开返回 0
    int             (WINAPI* noteCount)(void);           // 当前用户的划线+摘抄+笔记条数
    int             (WINAPI* inspirationCount)(void);    // 当前用户的心得条数
    void            (WINAPI* setStatus)(const wchar_t* text);          // 在主窗口状态栏显示一句话
    int             (WINAPI* messageBox)(const wchar_t* text,
                                         const wchar_t* title,
                                         unsigned int flags);          // 以主窗口为父窗口弹提示框
} Ex5HostApi;

// ---------------- 插件必须导出的函数(C 链接) ----------------
//
// BOOL WINAPI Ex5PluginQuery(Ex5PluginInfo* info);
//     填写插件信息。返回 FALSE 则放弃加载。
//
// BOOL WINAPI Ex5PluginInit(const Ex5HostApi* host);
//     宿主能力表传入,插件可保存该指针长期使用。返回 FALSE 则放弃加载。
//
// void WINAPI Ex5PluginRun(void);
//     用户在「插件」菜单点击本插件时调用。
//
// ---------------- 可选导出 ----------------
// void WINAPI Ex5PluginShutdown(void);
//     阅读器退出前调用,用于释放资源。

#ifdef __cplusplus
}
#endif

#endif // EX5_PLUGIN_H

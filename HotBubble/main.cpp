#include <Windows.h>
#include <Shellapi.h>
#include <Commctrl.h>
#include "config.h"
#include "SettingWindow.h"
#include "AboutWindow.h"
#include "BubbleWindow.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(linker, "/MANIFESTUAC:\"level='asInvoker' uiAccess='false'\"")

// 自定义消息和控件ID
#define ID_TRAY_ICON       1001
#define WM_TRAYMESSAGE     (WM_USER + 1)

#define MENU_ABOUT         2001
#define MENU_SETTINGS      2002
#define MENU_EXIT          2003

// 全局变量
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

const wchar_t g_szClassName[] = L"TrayHideWnd";
HWND g_hWnd = NULL;
NOTIFYICONDATA g_nid = { 0 };
HINSTANCE g_hInst = NULL;

// DPI 常量（编译期不依赖高版本 SDK，运行时动态加载对应函数）
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif
#ifndef PROCESS_PER_MONITOR_DPI_AWARE
#define PROCESS_PER_MONITOR_DPI_AWARE 1
#endif

// DPI 感知初始化（兼容 Windows 7，运行时动态加载）
static void InitDpiAwareness()
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");

    // 1) Windows 10 1607+: SetProcessDpiAwarenessContext
    if (hUser32)
    {
        typedef BOOL(WINAPI* FnSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
        auto pFn = (FnSetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pFn)
        {
            pFn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }

    // 2) Windows 8.1+: shcore.dll -> SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore)
    {
        typedef HRESULT(WINAPI* FnSetProcessDpiAwareness)(int);
        auto pFn = (FnSetProcessDpiAwareness)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (pFn)
        {
            pFn(PROCESS_PER_MONITOR_DPI_AWARE);
            FreeLibrary(hShcore);
            return;
        }
        FreeLibrary(hShcore);
    }

    // 3) Windows Vista+: SetProcessDPIAware（系统级 DPI 感知，回退方案）
    if (hUser32)
    {
        typedef BOOL(WINAPI* FnSetProcessDPIAware)();
        auto pFn = (FnSetProcessDPIAware)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (pFn)
        {
            pFn();
        }
    }
}


// 程序入口点
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow)
{
    g_hInst = hInst;

    InitDpiAwareness();

    InitBubbleWindowHook();
    LoadConfig();

    // 注册窗口类
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = g_szClassName;
    RegisterClass(&wc);

    // 创建隐藏消息窗口
    g_hWnd = CreateWindowEx(0, g_szClassName, L"",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        NULL, NULL, hInst, NULL);

    // 初始化托盘图标
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYMESSAGE;
    g_nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(102));
    wcscpy_s(g_nid.szTip, L"托盘后台程序");
    Shell_NotifyIcon(NIM_ADD, &g_nid);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UninitBubbleWindowHook();
    return 0;
}

// 显示托盘右键菜单
void ShowTrayMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, MENU_ABOUT, L"关于");
    AppendMenu(hMenu, MF_STRING, MENU_SETTINGS, L"设置");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, MENU_EXIT, L"退出程序");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

// 窗口消息处理函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_TRAYMESSAGE:
        if (lp == WM_RBUTTONUP)
            ShowTrayMenu(hWnd);
        return 0;

    case WM_COMMAND:
        switch (wp)
        {
        case MENU_ABOUT:
            OpenAboutWindow(hWnd);
            break;
        case MENU_SETTINGS:
            OpenSettingWindow(hWnd);
            break;
        case MENU_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

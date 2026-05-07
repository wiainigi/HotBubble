// 包含Windows API核心头文件，提供窗口、消息、系统服务等基础功能
#include <Windows.h>
// 包含Shell API，提供托盘图标、快捷方式等外壳功能
#include <Shellapi.h>
// 包含通用控件，提供进度条、列表等控件（本例中用于设置窗口）
#include <Commctrl.h>
// 自定义头文件，声明设置窗口的打开函数OpenSettingWindow()
#include "SettingWindow.h" 
#include "AboutWindow.h"

// 链接shell32.lib，提供Shell API函数实现（如Shell_NotifyIcon）
#pragma comment(lib, "shell32.lib")
// 链接comctl32.lib，提供通用控件实现（如InitCommonControls）
#pragma comment(lib, "comctl32.lib")
// 指定清单依赖，启用Windows通用控件6.0版本（支持现代视觉效果和DPI缩放）
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// 开启高DPI感知，解决文字模糊
// 针对x86（32位）和x64（64位）平台分别指定UAC执行级别为asInvoker（使用当前用户权限）
#if defined _M_IX86
#pragma comment(linker, "/MANIFESTUAC:\"level='asInvoker' uiAccess='false'\"")
#elif defined _M_X64
#pragma comment(linker, "/MANIFESTUAC:\"level='asInvoker' uiAccess='false'\"")
#endif

// ---------------------- 自定义消息和控件ID ----------------------
// 托盘图标的唯一标识符，用于区分多个图标（本例只有一个）
#define ID_TRAY_ICON       1001
// 自定义窗口消息，用于托盘图标发送鼠标事件到窗口
// WM_USER是Windows保留的第一个可用自定义消息值（>= 0x400）
#define WM_TRAYMESSAGE     (WM_USER + 1)

// 右键菜单项ID
#define MENU_ABOUT         2001   // 关于菜单项
#define MENU_SETTINGS      2002   // 设置菜单项
#define MENU_EXIT          2003   // 退出程序菜单项

// ---------------------- 全局变量 ----------------------
// 窗口过程函数声明，处理所有发送到窗口的消息
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// 窗口类名称，用于注册和创建窗口
const wchar_t g_szClassName[] = L"TrayHideWnd";
// 主窗口句柄（隐藏的消息窗口）
HWND g_hWnd = NULL;
// NOTIFYICONDATA结构体，用于配置托盘图标（图标、提示文字、回调消息等）
NOTIFYICONDATA g_nid = { 0 };
// 应用程序实例句柄，由WinMain传入，用于加载资源（图标等）
HINSTANCE g_hInst = NULL;

// ---------------------- 程序入口点 ----------------------
// wWinMain是Windows GUI应用程序的标准入口点
// 参数：hInst - 当前实例句柄；hPrevInst - 前一个实例（已废弃，总是NULL）
//       lpCmdLine - 命令行字符串；nShow - 窗口显示方式（本例未使用）
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow)
{
    // 保存实例句柄到全局变量，供后续加载资源使用
    g_hInst = hInst;

    // 启用高DPI感知（关键：解决弹窗文字模糊）
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 - 每个显示器独立DPI感知（Win10 1703+）
    // 让程序在不同DPI显示器上自动缩放，文字和控件不会模糊
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // ---------------------- 注册窗口类 ----------------------
    WNDCLASS wc = { 0 };          // 初始化窗口类结构体为零
    wc.lpfnWndProc = WndProc;     // 设置窗口过程函数，处理所有消息
    wc.hInstance = hInst;         // 设置实例句柄
    wc.lpszClassName = g_szClassName; // 窗口类名称
    RegisterClass(&wc);           // 向系统注册窗口类，失败返回0

    // ---------------------- 创建隐藏消息窗口 ----------------------
    // CreateWindowEx - 创建扩展窗口（支持额外样式）
    // 参数：扩展样式0；类名；窗口标题（空）；窗口样式；
    //       x,y,宽,高(CW_USEDEFAULT自动)；父窗口(NULL)；菜单(NULL)；
    //       实例句柄；附加参数(NULL)
    // 该窗口不显示，仅用于接收托盘图标的回调消息
    g_hWnd = CreateWindowEx(
        0, g_szClassName, L"",
        WS_OVERLAPPEDWINDOW,      // 重叠窗口样式（实际不显示）
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        NULL, NULL, hInst, NULL
    );

    // ---------------------- 托盘图标初始化 ----------------------
    ZeroMemory(&g_nid, sizeof(g_nid));  // 清空结构体内存
    g_nid.cbSize = sizeof(NOTIFYICONDATA); // 结构体大小（版本标识）
    g_nid.hWnd = g_hWnd;                // 接收消息的窗口句柄
    g_nid.uID = ID_TRAY_ICON;           // 图标唯一ID
    // uFlags指定哪些成员有效：NIF_ICON(图标)、NIF_MESSAGE(消息)、NIF_TIP(提示文字)
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYMESSAGE; // 鼠标事件时发送此自定义消息
    // LoadIcon - 从可执行文件资源中加载图标，资源ID为102
    // MAKEINTRESOURCE将整数ID转换为资源字符串形式
    g_nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(102));
    // 复制鼠标悬浮时的提示文字（最大64字符，含结尾'\0'）
    wcscpy_s(g_nid.szTip, L"托盘后台程序");

    // Shell_NotifyIcon - 向系统添加/修改/删除托盘图标
    // NIM_ADD - 添加新图标；第二个参数为NOTIFYICONDATA指针
    Shell_NotifyIcon(NIM_ADD, &g_nid);

    // ---------------------- 消息循环 ----------------------
    MSG msg;  // 消息结构体，存储消息信息
    // GetMessage - 从消息队列获取消息，返回0表示收到WM_QUIT
    // 参数：消息结构体；窗口句柄(NULL接收所有窗口消息)；最小值；最大值
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);  // 将虚拟按键消息转换为字符消息
        DispatchMessage(&msg);   // 将消息分发给对应的窗口过程
    }

    // 程序退出前删除托盘图标
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
    return 0;  // 返回给系统
}

// ---------------------- 显示托盘右键菜单 ----------------------
void ShowTrayMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);  // 获取当前鼠标屏幕坐标

    // CreatePopupMenu - 创建弹出式菜单（返回菜单句柄）
    HMENU hMenu = CreatePopupMenu();
    // AppendMenu - 向菜单添加项目
    // 参数：菜单句柄；标志(MF_STRING字符串项)；命令ID；显示文字
    AppendMenu(hMenu, MF_STRING, MENU_ABOUT, L"关于");
    AppendMenu(hMenu, MF_STRING, MENU_SETTINGS, L"设置");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);  // 分隔线
    AppendMenu(hMenu, MF_STRING, MENU_EXIT, L"退出程序");

    // SetForegroundWindow - 将窗口置前，确保菜单正常显示焦点
    SetForegroundWindow(hWnd);
    // TrackPopupMenu - 显示弹出式菜单
    // 参数：菜单句柄；对齐方式(TPM_BOTTOMALIGN底部对齐,TPM_LEFTALIGN左对齐)；
    //       x,y屏幕坐标；0；所属窗口句柄；NULL
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, 0, hWnd, NULL);

    DestroyMenu(hMenu);  // 销毁菜单，释放内存
}

// ---------------------- 窗口消息处理函数 ----------------------
// HWND hWnd - 接收消息的窗口句柄
// UINT msg - 消息类型（如WM_COMMAND、WM_DESTROY）
// WPARAM wp, LPARAM lp - 消息附加参数，含义取决于消息类型
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        // 托盘图标的回调消息（由Shell_NotifyIcon发送）
    case WM_TRAYMESSAGE:
    {
        // lp参数包含鼠标事件类型（如WM_RBUTTONUP右键抬起）
        if (lp == WM_RBUTTONUP)
        {
            ShowTrayMenu(hWnd);  // 显示右键菜单
        }
        return 0;
    }

    // 菜单命令响应（当用户点击菜单项时发送）
    case WM_COMMAND:
    {
        // wp的低16位是菜单项的命令ID（高16位是通知码，菜单时为0）
        switch (wp)
        {
        case MENU_ABOUT:
            OpenAboutWindow(hWnd);
            break;
        case MENU_SETTINGS:
            // OpenSettingWindow - 自定义函数，在SettingWindow.h中声明
            // 打开设置窗口并进行相应的快捷键设置
            OpenSettingWindow(hWnd);
            break;
        case MENU_EXIT:
            // DestroyWindow - 销毁窗口，会触发WM_DESTROY消息
            DestroyWindow(hWnd);
            break;
        }
        return 0;
    }

    // 窗口销毁消息（DestroyWindow调用后发送）
    case WM_DESTROY:
        // 从系统托盘中删除图标（NIM_DELETE删除）
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        // PostQuitMessage - 向消息队列发送WM_QUIT，终止GetMessage循环
        // 参数：退出代码0，返回给系统
        PostQuitMessage(0);
        return 0;
    }

    // 不处理的消息交给默认窗口过程处理（如窗口大小、关闭等）
    return DefWindowProc(hWnd, msg, wp, lp);
}
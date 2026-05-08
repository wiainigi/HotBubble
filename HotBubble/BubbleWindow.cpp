#include "BubbleWindow.h"
#include "config.h"          // 使用全局配置变量 (g_nBgAlpha, g_crBg, g_crTitle, g_nTitleSize等)
#include <windows.h>
#include <psapi.h>           // GetModuleBaseName
#include <cstring>           // memset
#include <cwchar>            // towlower

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")

// ==================== 全局静态变量 ====================
static HWND        g_hBubbleWnd = NULL;      // 单例窗口句柄
static HHOOK       g_hKeyboardHook = NULL;   // 低级键盘钩子句柄
static int         g_nModifierCount = 0;     // 当前按下的修饰键数量（Ctrl/Alt/Shift/Win）
static int         g_modKeyStates[256] = { 0 }; // 修饰键按下状态（0=未按，1=已按）
static WNDCLASS    g_wc = { 0 };             // 窗口类（已注册标志）
static const wchar_t CLASS_NAME[] = L"BubbleWindowClass";

// 当前激活窗口的进程名（在显示窗口时更新，已转为小写）
static wchar_t     g_szActiveProcessName[MAX_PATH] = L"";

// 前向声明
static LRESULT CALLBACK BubbleWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
static void ShowBubbleWindow();
static void DestroyBubbleWindow();

// ==================== 辅助函数：获取前景窗口进程名 ====================
static void GetActiveWindowProcessName(wchar_t* buffer, size_t size)
{
    if (!buffer || size == 0) return;
    buffer[0] = L'\0';

    HWND hFore = GetForegroundWindow();
    if (!hFore)
    {
        wcsncpy_s(buffer, size, L"Unknown", _TRUNCATE);
        return;
    }

    DWORD procId = 0;
    GetWindowThreadProcessId(hFore, &procId);
    if (procId == 0)
    {
        wcsncpy_s(buffer, size, L"Unknown", _TRUNCATE);
        return;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, procId);
    if (!hProcess)
    {
        wcsncpy_s(buffer, size, L"Unknown", _TRUNCATE);
        return;
    }

    // 获取进程可执行文件的基本名称（如 code.exe）
    if (GetModuleBaseNameW(hProcess, NULL, buffer, (DWORD)size) == 0)
    {
        wcsncpy_s(buffer, size, L"Unknown", _TRUNCATE);
    }

    CloseHandle(hProcess);
}

// ==================== 窗口管理（单例） ====================
static BOOL RegisterBubbleClass()
{
    if (g_wc.lpszClassName) return TRUE; // 已注册

    g_wc.style = CS_HREDRAW | CS_VREDRAW;
    g_wc.lpfnWndProc = BubbleWndProc;
    g_wc.hInstance = GetModuleHandle(NULL);
    g_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    g_wc.hbrBackground = NULL;   // 自定义背景（通过 WM_PAINT 绘制）
    g_wc.lpszClassName = CLASS_NAME;

    return RegisterClass(&g_wc) != 0;
}

static void ShowBubbleWindow()
{
    if (!RegisterBubbleClass()) return;

    // 单例：如果已存在则先销毁再创建（保证干净状态）
    if (g_hBubbleWnd)
        DestroyBubbleWindow();

    // 获取当前激活窗口的进程名（用于显示）
    GetActiveWindowProcessName(g_szActiveProcessName, MAX_PATH);

    // 将进程名转换为全小写
    for (int i = 0; g_szActiveProcessName[i] != L'\0'; ++i) {
        g_szActiveProcessName[i] = towlower(g_szActiveProcessName[i]);
    }

    // 获取工作区（不包含任务栏的区域）
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    int screenWidth = workArea.right - workArea.left;
    int screenBottom = workArea.bottom;
    const int windowHeight = 400;     // 固定高度
    int windowX = workArea.left;
    int windowY = screenBottom - windowHeight;  // 屏幕底部向上偏移

    // 创建窗口：无边框、置顶、分层窗口、鼠标穿透
    g_hBubbleWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        CLASS_NAME,
        L"BubbleWindow",
        WS_POPUP,
        windowX, windowY,
        screenWidth, windowHeight,
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!g_hBubbleWnd) return;

    // 设置全局透明度 (用户语义: 0=不透明, 255=全透明)
    BYTE opacity = (BYTE)(255 - g_nBgAlpha);
    SetLayeredWindowAttributes(g_hBubbleWnd, 0, opacity, LWA_ALPHA);

    // 显示窗口并更新背景
    ShowWindow(g_hBubbleWnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hBubbleWnd);
}

static void DestroyBubbleWindow()
{
    if (g_hBubbleWnd)
    {
        DestroyWindow(g_hBubbleWnd);
        g_hBubbleWnd = NULL;
    }
}

// ==================== 窗口绘制函数 ====================
static void DrawBackground(HDC hdc, const RECT& rect)
{
    HBRUSH brush = CreateSolidBrush(g_crBg);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

static void DrawProcessName(HDC hdc, const RECT& clientRect)
{
    if (g_szActiveProcessName[0] == L'\0')
        return;

    // 根据全局配置创建字体（大小 g_nTitleSize）
    HFONT hFont = CreateFontW(
        g_nTitleSize,               // 字体高度（逻辑单位）
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei"          // 使用常见无衬线字体，可按需改为 L"Segoe UI"
    );

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    SetTextColor(hdc, g_crTitle);
    SetBkMode(hdc, TRANSPARENT);

    // 绘制区域：左上角，上下左右留 15px 边距
    RECT textRect = clientRect;
    textRect.left += 15;
    textRect.top += 15;
    textRect.right -= 15;
    textRect.bottom -= 15;

    DrawTextW(hdc,
        g_szActiveProcessName,
        -1,
        &textRect,
        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);    // 释放字体资源
}

// ==================== 窗口过程 ====================
static LRESULT CALLBACK BubbleWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);

        DrawBackground(hdc, clientRect);
        DrawProcessName(hdc, clientRect);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;   // 避免闪烁，由 WM_PAINT 负责绘制

    case WM_DESTROY:
        g_hBubbleWnd = NULL;
        return 0;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

// ==================== 低级键盘钩子 ====================
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lp;
        DWORD vk = pKb->vkCode;

        BOOL isModifier = (vk == VK_CONTROL) ||
            (vk == VK_LCONTROL) ||
            (vk == VK_RCONTROL) ||
            (vk == VK_MENU) ||
            (vk == VK_LMENU) ||
            (vk == VK_RMENU) ||
            (vk == VK_SHIFT) ||
            (vk == VK_LSHIFT) ||
            (vk == VK_RSHIFT) ||
            (vk == VK_LWIN) ||
            (vk == VK_RWIN);

        if (isModifier)
        {
            BOOL isKeyDown = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
            BOOL isKeyUp = (wp == WM_KEYUP || wp == WM_SYSKEYUP);

            if (isKeyDown)
            {
                if (g_modKeyStates[vk] == 0)
                {
                    g_modKeyStates[vk] = 1;
                    if (g_nModifierCount == 0)
                        ShowBubbleWindow();
                    g_nModifierCount++;
                }
            }
            else if (isKeyUp)
            {
                if (g_modKeyStates[vk] == 1)
                {
                    g_modKeyStates[vk] = 0;
                    if (g_nModifierCount > 0)
                    {
                        g_nModifierCount--;
                        if (g_nModifierCount == 0)
                            DestroyBubbleWindow();
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wp, lp);
}

// ==================== 公共接口 ====================
void InitBubbleWindowHook()
{
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    g_nModifierCount = 0;
    memset(g_modKeyStates, 0, sizeof(g_modKeyStates));
    g_szActiveProcessName[0] = L'\0';
}

void UninitBubbleWindowHook()
{
    if (g_hKeyboardHook)
    {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }
    DestroyBubbleWindow();
}
#include "BubbleWindow.h"
#include "config.h"          // 使用全局配置变量
#include <windows.h>

#pragma comment(lib, "user32.lib")

// ==================== 全局静态变量 ====================
static HWND        g_hBubbleWnd = NULL;      // 单例窗口句柄
static HHOOK       g_hKeyboardHook = NULL;   // 低级键盘钩子句柄
static int         g_nModifierCount = 0;     // 当前按下的修饰键数量（Ctrl/Alt/Shift/Win）
static WNDCLASS    g_wc = { 0 };             // 窗口类（已注册标志）
static const wchar_t CLASS_NAME[] = L"BubbleWindowClass";

// 前向声明
static LRESULT CALLBACK BubbleWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
static void ShowBubbleWindow();
static void DestroyBubbleWindow();

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

    // 获取工作区（不包含任务栏的区域）
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    int screenWidth = workArea.right - workArea.left;
    int screenBottom = workArea.bottom;
    const int windowHeight = 400;     // 固定高度
    int windowX = workArea.left;
    int windowY = screenBottom - windowHeight;  // 屏幕底部向上偏移

    // 创建窗口：无边框、置顶、分层窗口（支持透明度）
    g_hBubbleWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        CLASS_NAME,
        L"BubbleWindow",
        WS_POPUP,               // 无边框弹出窗口
        windowX, windowY,
        screenWidth, windowHeight,
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!g_hBubbleWnd) return;

    // 设置全局透明度 (用户语义: 0=不透明, 255=全透明)
    // SetLayeredWindowAttributes 的 alpha 值: 0=全透明, 255=不透明
    BYTE opacity = (BYTE)(255 - g_nBgAlpha);
    SetLayeredWindowAttributes(g_hBubbleWnd, 0, opacity, LWA_ALPHA);

    // 显示窗口并更新背景
    ShowWindow(g_hBubbleWnd, SW_SHOWNOACTIVATE);  // 不激活窗口，避免抢焦点
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

// ==================== 窗口过程 ====================
static void DrawBackground(HWND hWnd, HDC hdc, const RECT& rect)
{
    // 使用配置的背景颜色（g_crBg）
    HBRUSH brush = CreateSolidBrush(g_crBg);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

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
        DrawBackground(hWnd, hdc, clientRect);
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

// 新增：记录修饰键按下状态（按下=1，未按=0）
static int g_modKeyStates[256] = { 0 };

// ==================== 低级键盘钩子 ====================
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lp;
        BOOL isKeyDown = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        BOOL isKeyUp = (wp == WM_KEYUP || wp == WM_SYSKEYUP);

        // 只关注修饰键：Ctrl, Alt, Shift, Win (左右都考虑)
        BOOL isModifier = (pKb->vkCode == VK_CONTROL) ||
            (pKb->vkCode == VK_LCONTROL) ||
            (pKb->vkCode == VK_RCONTROL) ||
            (pKb->vkCode == VK_MENU) ||     // Alt
            (pKb->vkCode == VK_LMENU) ||
            (pKb->vkCode == VK_RMENU) ||
            (pKb->vkCode == VK_SHIFT) ||
            (pKb->vkCode == VK_LSHIFT) ||
            (pKb->vkCode == VK_RSHIFT) ||
            (pKb->vkCode == VK_LWIN) ||
            (pKb->vkCode == VK_RWIN);

        if (isModifier)
        {
            if (isKeyDown)
            {
                // 按下修饰键：计数器从 0 变为 1 时显示窗口
                if (g_nModifierCount == 0)
                {
                    ShowBubbleWindow();
                }
                g_nModifierCount++;
            }
            else if (isKeyUp)
            {
                // 抬起修饰键：计数器递减，减到 0 时销毁窗口
                if (g_nModifierCount > 0)
                {
                    g_nModifierCount--;
                    if (g_nModifierCount == 0)
                    {
                        DestroyBubbleWindow();
                    }
                }
            }
        }
    }
    // 传递给下一个钩子（必须）
    return CallNextHookEx(g_hKeyboardHook, nCode, wp, lp);
}

// ==================== 公共接口 ====================
void InitBubbleWindowHook()
{
    // 安装低级键盘钩子（不需要 DLL）
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    // 初始化计数器
    g_nModifierCount = 0;
}

void UninitBubbleWindowHook()
{
    if (g_hKeyboardHook)
    {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }
    // 确保任何残留窗口销毁
    DestroyBubbleWindow();
}
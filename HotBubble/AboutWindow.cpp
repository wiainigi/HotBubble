#include "AboutWindow.h"
#include "config.h"
#include <windows.h>
#include <wingdi.h>

#pragma comment(lib, "msimg32.lib")

#define IDC_STATIC_TITLE_TEXT       1701
#define IDC_STATIC_CLOSE_BTN        1702
#define IDC_STATIC_ABOUT_CONTENT    1703
#define IDC_BTN_CLOSE               1704

static HWND g_hAbout = NULL;
static HFONT g_hSegoeUIFont = NULL;
static HFONT g_hBigCloseFont = NULL;

static LRESULT CALLBACK AboutWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        // 创建 Segoe UI 默认字体
        g_hSegoeUIFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!g_hSegoeUIFont)
            g_hSegoeUIFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // 大号关闭按钮字体（×）
        g_hBigCloseFont = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        // 标题 "关于"
        CreateWindowW(L"STATIC", L"关于", WS_CHILD | WS_VISIBLE | SS_CENTER,
            6, 20, 80, 30, hWnd, (HMENU)IDC_STATIC_TITLE_TEXT, NULL, NULL);

        // 右上角关闭按钮 ×
        CreateWindowW(L"STATIC", L"×", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
            360, 10, 40, 35, hWnd, (HMENU)IDC_STATIC_CLOSE_BTN, NULL, NULL);

        int yLine = 80;
        int lineHeight = 30;

        CreateWindowW(L"STATIC", L"软件名称：热键泡泡", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, yLine, 360, lineHeight, hWnd, (HMENU)IDC_STATIC_ABOUT_CONTENT, NULL, NULL);
        yLine += lineHeight;

        CreateWindowW(L"STATIC", L"软件版本：v0.0.1", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, yLine, 360, lineHeight, hWnd, (HMENU)IDC_STATIC_ABOUT_CONTENT, NULL, NULL);
        yLine += lineHeight;

        CreateWindowW(L"STATIC", L"软件主页：没有哦", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, yLine, 360, lineHeight, hWnd, (HMENU)IDC_STATIC_ABOUT_CONTENT, NULL, NULL);
        yLine += lineHeight;

        CreateWindowW(L"STATIC", L"软件介绍：按住 Win、Ctrl、Shift、Alt 后显示当前使用软件的快捷键提示", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, yLine, 360, lineHeight * 2, hWnd, (HMENU)IDC_STATIC_ABOUT_CONTENT, NULL, NULL);
        yLine += lineHeight * 2;

        CreateWindowW(L"STATIC", L"软件作者：wiainigi@qq.com", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, yLine, 360, lineHeight, hWnd, (HMENU)IDC_STATIC_ABOUT_CONTENT, NULL, NULL);

        // 底部关闭按钮
        CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            160, 290, 80, 30, hWnd, (HMENU)IDC_BTN_CLOSE, NULL, NULL);

        // 为所有子控件设置默认字体
        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessage(hChild, WM_SETFONT, (WPARAM)g_hSegoeUIFont, TRUE);
            return TRUE;
            }, 0);

        // 单独为 × 按钮设置大字体
        HWND hCloseBtn = GetDlgItem(hWnd, IDC_STATIC_CLOSE_BTN);
        if (hCloseBtn)
            SendMessage(hCloseBtn, WM_SETFONT, (WPARAM)g_hBigCloseFont, TRUE);

        return 0;
    }

    case WM_ERASEBKGND:
    {
        // 与 SettingWindow 完全一致的背景绘制（支持透明度）
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC hdcMem = CreateCompatibleDC(hdc);
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (hBitmap)
        {
            HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);
            RECT rcMem = { 0, 0, w, h };
            HBRUSH hBrush = CreateSolidBrush(g_crBg);
            FillRect(hdcMem, &rcMem, hBrush);
            DeleteObject(hBrush);
            BYTE alpha = (BYTE)(g_nBgAlpha);
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, alpha, 0 };
            AlphaBlend(hdc, 0, 0, w, h, hdcMem, 0, 0, w, h, blend);
            SelectObject(hdcMem, hOld);
            DeleteObject(hBitmap);
        }
        DeleteDC(hdcMem);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wp;
        HWND hCtrl = (HWND)lp;
        int ctrlId = GetDlgCtrlID(hCtrl);

        SetBkMode(hdc, TRANSPARENT);
        if (ctrlId == IDC_STATIC_TITLE_TEXT || ctrlId == IDC_STATIC_CLOSE_BTN)
            SetTextColor(hdc, g_crTitle);
        else if (ctrlId == IDC_STATIC_ABOUT_CONTENT)
            SetTextColor(hdc, g_crLabel);
        else
            SetTextColor(hdc, RGB(0, 0, 0));  // 按钮文字黑色
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if ((id == IDC_STATIC_CLOSE_BTN && code == STN_CLICKED) || id == IDC_BTN_CLOSE)
        {
            DestroyWindow(hWnd);
            g_hAbout = NULL;
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        if (g_hSegoeUIFont) DeleteObject(g_hSegoeUIFont);
        if (g_hBigCloseFont) DeleteObject(g_hBigCloseFont);
        g_hAbout = NULL;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

void OpenAboutWindow(HWND hParent)
{
    // 加载配置文件，确保配色与 SettingWindow 一致
    LoadConfig();

    if (g_hAbout)
    {
        SetForegroundWindow(g_hAbout);
        return;
    }

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = AboutWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"AboutWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    int wndWidth = 420, wndHeight = 360;
    int cx = (GetSystemMetrics(SM_CXSCREEN) - wndWidth) / 2;
    int cy = (GetSystemMetrics(SM_CYSCREEN) - wndHeight) / 2;
    g_hAbout = CreateWindowExW(0, L"AboutWindowClass", L"", WS_POPUP | WS_VISIBLE,
        cx, cy, wndWidth, wndHeight, hParent, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(g_hAbout, SW_SHOW);
    UpdateWindow(g_hAbout);
}
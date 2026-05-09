// SettingWindow.cpp
#include "SettingWindow.h"
#include "Config.h"
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <wingdi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")

// 此段内容是全局申明的变量，重新生成代码时保留此段代码注释
//  extern int      g_nTitleSize;   标题文字大小
//  extern int      g_nLabelSize;   标签文字大小
//  extern int      g_nBgAlpha;     窗口背景颜色透明度 0-255
//  extern bool     g_bShowWindows; 是否显示系统快捷键
//  extern bool     g_bShowProcess; 是否显示程序快捷键
//  extern COLORREF g_crTitle;      标题文字颜色
//  extern COLORREF g_crLabel;      标签文字颜色
//  extern COLORREF g_crBg;         窗口背景颜色
//  extern COLORREF g_crMark;       标记文字颜色

// 此段内容是配置文件config.ini所有内容，重新生成代码时保留此段代码注释
//  [UIConfig]
//  titleFontSize = 10
//  cardFontSize = 8
//  bgAlpha = 200
//  bgColor = 0, 0, 0
//  activeTextColor = 255, 60, 60
//  labelTextColor = 255, 255, 255
//  titleTextColor = 255, 255, 255
//  showWindows = 1
//  showProcess = 1

// 引用外部全局变量
extern int      g_nTitleSize;
extern int      g_nLabelSize;
extern int      g_nBgAlpha;
extern bool     g_bShowWindows;
extern bool     g_bShowProcess;
extern COLORREF g_crTitle;
extern COLORREF g_crLabel;
extern COLORREF g_crBg;
extern COLORREF g_crMark;

#define IDC_EDIT_TITLE_SIZE         1101
#define IDC_EDIT_LABEL_SIZE         1102
#define IDC_BTN_TITLE_COLOR         1201
#define IDC_BTN_LABEL_COLOR         1202
#define IDC_BTN_BG_COLOR            1203
#define IDC_BTN_MARK_COLOR          1204
#define IDC_STATIC_TITLE_COLOR      1301
#define IDC_STATIC_LABEL_COLOR      1302
#define IDC_STATIC_BG_COLOR         1303
#define IDC_STATIC_MARK_COLOR       1304
#define IDC_EDIT_BG_ALPHA           1401
#define IDC_BTN_CONFIRM             1501
#define IDC_BTN_CANCEL              1502
#define IDC_STATIC_TITLE_TEXT       1601
#define IDC_STATIC_CLOSE_BTN        1602
#define IDC_STATIC_LABEL_TEXT       1603
#define IDC_CHECK_SHOW_WINDOWS      1604
#define IDC_CHECK_SHOW_PROCESS      1605

typedef struct {
    int      titleSize;
    int      labelSize;
    COLORREF crTitle;
    COLORREF crLabel;
    COLORREF crBg;
    COLORREF crMark;
    int      bgAlpha;
} SettingConfig;

static HWND   g_hSetting = NULL;
static SettingConfig g_tempConfig;
static HFONT  g_hSegoeUIFont = NULL;

static int GetDlgItemIntSafe(HWND hDlg, int nID)
{
    BOOL success;
    int val = GetDlgItemInt(hDlg, nID, &success, TRUE);
    return success ? val : 0;
}

static void SetDlgItemInt(HWND hDlg, int nID, int val)
{
    wchar_t buf[16];
    swprintf_s(buf, 16, L"%d", val);
    SetDlgItemTextW(hDlg, nID, buf);
}

static int GetDlgItemAlpha(HWND hDlg, int nID)
{
    BOOL success;
    int val = GetDlgItemInt(hDlg, nID, &success, TRUE);
    if (!success) val = 255;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return val;
}

static void UpdateColorPreview(HWND hWnd, int nID)
{
    HWND hStatic = GetDlgItem(hWnd, nID);
    if (hStatic) InvalidateRect(hStatic, NULL, TRUE);
}

static void PickColor(HWND hWnd, COLORREF* pColor, int nStaticID)
{
    CHOOSECOLOR cc = { 0 };
    static COLORREF customColors[16] = { 0 };
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hWnd;
    cc.rgbResult = *pColor;
    cc.lpCustColors = customColors;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (ChooseColor(&cc))
    {
        *pColor = cc.rgbResult;
        UpdateColorPreview(hWnd, nStaticID);
    }
}

LRESULT CALLBACK SettingWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hSegoeUIFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!g_hSegoeUIFont)
            g_hSegoeUIFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreateWindowW(L"STATIC", L"设置", WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 20, 80, 30, hWnd, (HMENU)IDC_STATIC_TITLE_TEXT, NULL, NULL);
        CreateWindowW(L"STATIC", L"×", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
            740, 10, 40, 35, hWnd, (HMENU)IDC_STATIC_CLOSE_BTN, NULL, NULL);

        CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            20, 60, 760, 200, hWnd, NULL, NULL, NULL);
        int yBase = 100;
        CreateWindowW(L"STATIC", L"标题文字大小：", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            40, yBase, 140, 20, hWnd, (HMENU)IDC_STATIC_LABEL_TEXT, NULL, NULL);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            190, yBase - 2, 80, 22, hWnd, (HMENU)IDC_EDIT_TITLE_SIZE, NULL, NULL);

        // 添加“显示系统快捷键”复选框
        CreateWindowW(L"BUTTON", L"显示系统快捷键", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            400, yBase - 2, 180, 22, hWnd, (HMENU)IDC_CHECK_SHOW_WINDOWS, NULL, NULL);
        yBase += 35;

        CreateWindowW(L"STATIC", L"标签文字大小：", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            40, yBase, 140, 20, hWnd, (HMENU)IDC_STATIC_LABEL_TEXT, NULL, NULL);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            190, yBase - 2, 80, 22, hWnd, (HMENU)IDC_EDIT_LABEL_SIZE, NULL, NULL);

        // 添加“显示程序快捷键”复选框
        CreateWindowW(L"BUTTON", L"显示程序快捷键", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            400, yBase - 2, 180, 22, hWnd, (HMENU)IDC_CHECK_SHOW_PROCESS, NULL, NULL);
        yBase += 35;

        CreateWindowW(L"STATIC", L"背景透明度：", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            40, yBase, 140, 20, hWnd, (HMENU)IDC_STATIC_LABEL_TEXT, NULL, NULL);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            190, yBase - 2, 80, 22, hWnd, (HMENU)IDC_EDIT_BG_ALPHA, NULL, NULL);

        CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            20, 270, 760, 230, hWnd, NULL, NULL, NULL);
        yBase = 310;
        CreateWindowW(L"STATIC", L"标题颜色：", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            40, yBase, 140, 20, hWnd, (HMENU)IDC_STATIC_LABEL_TEXT, NULL, NULL);
        CreateWindowW(L"BUTTON", L"选色", WS_CHILD | WS_VISIBLE | BS_CENTER,
            190, yBase - 3, 80, 25, hWnd, (HMENU)IDC_BTN_TITLE_COLOR, NULL, NULL);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
            280, yBase - 2, 30, 22, hWnd, (HMENU)IDC_STATIC_TITLE_COLOR, NULL, NULL);
        yBase += 35;
        CreateWindowW(L"STATIC", L"标签颜色：", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            40, yBase, 140, 20, hWnd, (HMENU)IDC_STATIC_LABEL_TEXT, NULL, NULL);
        CreateWindowW(L"BUTTON", L"选色", WS_CHILD | WS_VISIBLE | BS_CENTER,
            190, yBase - 3, 80, 25, hWnd, (HMENU)IDC_BTN_LABEL_COLOR, NULL, NULL);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
            280, yBase - 2, 30, 22, hWnd, (HMENU)IDC_STATIC_LABEL_COLOR, NULL, NULL);
        yBase += 35;
        CreateWindowW(L"STATIC", L"背景颜色：", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            40, yBase, 140, 20, hWnd, (HMENU)IDC_STATIC_LABEL_TEXT, NULL, NULL);
        CreateWindowW(L"BUTTON", L"选色", WS_CHILD | WS_VISIBLE | BS_CENTER,
            190, yBase - 3, 80, 25, hWnd, (HMENU)IDC_BTN_BG_COLOR, NULL, NULL);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
            280, yBase - 2, 30, 22, hWnd, (HMENU)IDC_STATIC_BG_COLOR, NULL, NULL);
        yBase += 35;
        CreateWindowW(L"STATIC", L"标记颜色：", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            40, yBase, 140, 20, hWnd, (HMENU)IDC_STATIC_LABEL_TEXT, NULL, NULL);
        CreateWindowW(L"BUTTON", L"选色", WS_CHILD | WS_VISIBLE | BS_CENTER,
            190, yBase - 3, 80, 25, hWnd, (HMENU)IDC_BTN_MARK_COLOR, NULL, NULL);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
            280, yBase - 2, 30, 22, hWnd, (HMENU)IDC_STATIC_MARK_COLOR, NULL, NULL);

        CreateWindowW(L"BUTTON", L"确认", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            600, 520, 80, 30, hWnd, (HMENU)IDC_BTN_CONFIRM, NULL, NULL);
        CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            690, 520, 80, 30, hWnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);

        // 为所有子控件设置 Segoe UI 字体
        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessage(hChild, WM_SETFONT, (WPARAM)g_hSegoeUIFont, TRUE);
            return TRUE;
            }, 0);

        // 为关闭按钮单独创建更大的字体
        HFONT hBigFont = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HWND hCloseBtn = GetDlgItem(hWnd, IDC_STATIC_CLOSE_BTN);
        SendMessage(hCloseBtn, WM_SETFONT, (WPARAM)hBigFont, TRUE);

        // 保存这个字体句柄，以便在 WM_DESTROY 中释放
        SetProp(hWnd, L"CloseBtnFont", (HANDLE)hBigFont);

        SetDlgItemInt(hWnd, IDC_EDIT_TITLE_SIZE, g_nTitleSize);
        SetDlgItemInt(hWnd, IDC_EDIT_LABEL_SIZE, g_nLabelSize);
        SetDlgItemInt(hWnd, IDC_EDIT_BG_ALPHA, g_nBgAlpha);
        // 设置复选框状态
        CheckDlgButton(hWnd, IDC_CHECK_SHOW_WINDOWS, g_bShowWindows ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHECK_SHOW_PROCESS, g_bShowProcess ? BST_CHECKED : BST_UNCHECKED);

        g_tempConfig.titleSize = g_nTitleSize;
        g_tempConfig.labelSize = g_nLabelSize;
        g_tempConfig.crTitle = g_crTitle;
        g_tempConfig.crLabel = g_crLabel;
        g_tempConfig.crBg = g_crBg;
        g_tempConfig.crMark = g_crMark;
        g_tempConfig.bgAlpha = g_nBgAlpha;

        UpdateColorPreview(hWnd, IDC_STATIC_TITLE_COLOR);
        UpdateColorPreview(hWnd, IDC_STATIC_LABEL_COLOR);
        UpdateColorPreview(hWnd, IDC_STATIC_BG_COLOR);
        UpdateColorPreview(hWnd, IDC_STATIC_MARK_COLOR);
        return 0;
    }

    case WM_ERASEBKGND:
    {
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
            HBRUSH hBrush = CreateSolidBrush(g_tempConfig.crBg);
            FillRect(hdcMem, &rcMem, hBrush);
            DeleteObject(hBrush);
            // 直接使用 bgAlpha 作为源半透明度，与气泡窗口一致
            BYTE alpha = (BYTE)g_tempConfig.bgAlpha;
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
    case WM_CTLCOLOREDIT:
    {
        // 对于编辑框，使用系统默认背景（不修改）
        if (msg == WM_CTLCOLOREDIT)
        {
            return DefWindowProc(hWnd, msg, wp, lp);
        }

        HDC hdc = (HDC)wp;
        HWND hCtrl = (HWND)lp;
        int ctrlId = GetDlgCtrlID(hCtrl);

        if (ctrlId == IDC_STATIC_TITLE_COLOR || ctrlId == IDC_STATIC_LABEL_COLOR ||
            ctrlId == IDC_STATIC_BG_COLOR || ctrlId == IDC_STATIC_MARK_COLOR)
        {
            COLORREF col = RGB(255, 255, 255);
            if (ctrlId == IDC_STATIC_TITLE_COLOR) col = g_tempConfig.crTitle;
            else if (ctrlId == IDC_STATIC_LABEL_COLOR) col = g_tempConfig.crLabel;
            else if (ctrlId == IDC_STATIC_BG_COLOR) col = g_tempConfig.crBg;
            else if (ctrlId == IDC_STATIC_MARK_COLOR) col = g_tempConfig.crMark;
            HBRUSH hBrush = CreateSolidBrush(col);
            SetBkColor(hdc, col);
            RECT rect;
            GetClientRect(hCtrl, &rect);
            FillRect(hdc, &rect, hBrush);
            DeleteObject(hBrush);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }

        SetBkMode(hdc, TRANSPARENT);
        if (ctrlId == IDC_STATIC_TITLE_TEXT || ctrlId == IDC_STATIC_CLOSE_BTN)
            SetTextColor(hdc, g_tempConfig.crTitle);
        else if (ctrlId == IDC_STATIC_LABEL_TEXT)
            SetTextColor(hdc, g_tempConfig.crLabel);
        else
            SetTextColor(hdc, RGB(0, 0, 0));
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id == IDC_STATIC_CLOSE_BTN && code == STN_CLICKED)
        {
            DestroyWindow(hWnd);
            g_hSetting = NULL;
            return 0;
        }
        switch (id)
        {
        case IDC_BTN_TITLE_COLOR: PickColor(hWnd, &g_tempConfig.crTitle, IDC_STATIC_TITLE_COLOR); break;
        case IDC_BTN_LABEL_COLOR: PickColor(hWnd, &g_tempConfig.crLabel, IDC_STATIC_LABEL_COLOR); break;
        case IDC_BTN_BG_COLOR:
            PickColor(hWnd, &g_tempConfig.crBg, IDC_STATIC_BG_COLOR);
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        case IDC_BTN_MARK_COLOR: PickColor(hWnd, &g_tempConfig.crMark, IDC_STATIC_MARK_COLOR); break;
        case IDC_BTN_CONFIRM:
            g_tempConfig.titleSize = GetDlgItemIntSafe(hWnd, IDC_EDIT_TITLE_SIZE);
            g_tempConfig.labelSize = GetDlgItemIntSafe(hWnd, IDC_EDIT_LABEL_SIZE);
            g_tempConfig.bgAlpha = GetDlgItemAlpha(hWnd, IDC_EDIT_BG_ALPHA);
            g_nTitleSize = g_tempConfig.titleSize;
            g_nLabelSize = g_tempConfig.labelSize;
            g_nBgAlpha = g_tempConfig.bgAlpha;
            g_crTitle = g_tempConfig.crTitle;
            g_crLabel = g_tempConfig.crLabel;
            g_crBg = g_tempConfig.crBg;
            g_crMark = g_tempConfig.crMark;
            // 读取复选框状态并更新全局变量
            g_bShowWindows = (IsDlgButtonChecked(hWnd, IDC_CHECK_SHOW_WINDOWS) == BST_CHECKED);
            g_bShowProcess = (IsDlgButtonChecked(hWnd, IDC_CHECK_SHOW_PROCESS) == BST_CHECKED);
            SaveConfig();
            DestroyWindow(hWnd);
            g_hSetting = NULL;
            return 0;
        case IDC_BTN_CANCEL:
            DestroyWindow(hWnd);
            g_hSetting = NULL;
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        HFONT hBigFont = (HFONT)GetProp(hWnd, L"CloseBtnFont");
        if (hBigFont) DeleteObject(hBigFont);
        RemoveProp(hWnd, L"CloseBtnFont");
        if (g_hSegoeUIFont) DeleteObject(g_hSegoeUIFont);
        g_hSetting = NULL;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

void OpenSettingWindow(HWND hParent)
{
    if (g_hSetting)
    {
        SetForegroundWindow(g_hSetting);
        return;
    }
    LoadConfig();
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = SettingWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"SettingWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    int wndWidth = 800, wndHeight = 580;
    int cx = (GetSystemMetrics(SM_CXSCREEN) - wndWidth) / 2;
    int cy = (GetSystemMetrics(SM_CYSCREEN) - wndHeight) / 2;
    g_hSetting = CreateWindowExW(0, L"SettingWindowClass", L"", WS_POPUP | WS_VISIBLE,
        cx, cy, wndWidth, wndHeight, hParent, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(g_hSetting, SW_SHOW);
    UpdateWindow(g_hSetting);
}
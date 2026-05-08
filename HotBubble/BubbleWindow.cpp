#include "BubbleWindow.h"
#include "config.h"
#include <windows.h>
#include <psapi.h>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")

// ==================== 全局静态变量 ====================
static HWND        g_hBubbleWnd = NULL;
static HHOOK       g_hKeyboardHook = NULL;
static int         g_nModifierCount = 0;
static int         g_modKeyStates[256] = { 0 };
static WNDCLASS    g_wc = { 0 };
static const wchar_t CLASS_NAME[] = L"BubbleWindowClass";

// 此段内容是全局申明的变量，重新生成代码时保留此段代码注释
//  extern int      g_nTitleSize;   标题文字大小
//  extern int      g_nLabelSize;   标签文字大小
//  extern int      g_nBgAlpha;     窗口背景颜色透明度 0-255
//  extern COLORREF g_crTitle;      标题文字颜色
//  extern COLORREF g_crLabel;      标签文字颜色
//  extern COLORREF g_crBg;         窗口背景颜色
//  extern COLORREF g_crMark;       标记文字颜色

// 此段内容是keyboards/windows.ini配置文件内容格式（文件夹在程序根目录），重新生成代码时保留此段代码注释
//  [Info]                          配置文件信息节点
//  Name = Windows System           配置文件对应的进程名称
//  Description = Windows 系统       进程说明
//  
//  [Keys]                          热键信息节点
//  Win = 打开 / 关闭开始菜单          热键和热键作用
//  Win + D = 显示桌面
//  Win + E = 打开文件资源管理器
//  ...

// ------------------- 全局配置文件数据 -------------------
static std::wstring       g_titleText;
static std::vector<std::pair<std::wstring, std::wstring>> g_hotkeyList;
static bool               g_configLoaded = false;

static std::wstring       g_processTitleText;
static std::vector<std::pair<std::wstring, std::wstring>> g_processHotkeyList;

// 前向声明
static LRESULT CALLBACK BubbleWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
static void ShowBubbleWindow();
static void DestroyBubbleWindow();
static bool LoadConfiguration();
static bool LoadProcessConfiguration(const std::wstring& processExeName);
static int  CalculateWindowHeight(int screenWidth);
static void DrawHotkeysLayout(HDC hdc, const RECT& rect, int startY,
    const std::vector<std::pair<std::wstring, std::wstring>>& hotkeyList,
    int& consumedHeight);
static int  DrawTitle(HDC hdc, const RECT& clientRect, const std::wstring& titleText,
    int topOffset);
static void OnPaint(HWND hWnd);  // 添加前向声明

// ==================== 辅助函数：获取当前exe所在目录 ====================
static std::wstring GetExeDirectory()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    return std::wstring(path);
}

// ==================== 辅助函数：获取前台窗口进程名（不含扩展名） ====================
static std::wstring GetForegroundProcessName()
{
    HWND hFgWnd = GetForegroundWindow();
    if (!hFgWnd) return L"";

    DWORD pid = 0;
    GetWindowThreadProcessId(hFgWnd, &pid);
    if (pid == 0) return L"";

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return L"";

    wchar_t exePath[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProcess, 0, exePath, &size))
    {
        std::wstring path(exePath);
        size_t pos = path.rfind(L'\\');
        if (pos != std::wstring::npos)
        {
            std::wstring name = path.substr(pos + 1);
            size_t dotPos = name.rfind(L'.');
            if (dotPos != std::wstring::npos)
                name = name.substr(0, dotPos);
            CloseHandle(hProcess);
            return name;
        }
    }

    CloseHandle(hProcess);
    return L"";
}

// ==================== 读取并解析 windows.ini ====================
static bool LoadConfiguration()
{
    if (g_configLoaded) return true;

    std::wstring iniPath = GetExeDirectory() + L"keyboards\\windows.ini";
    std::ifstream file(iniPath);
    if (!file.is_open())
    {
        g_titleText = L"Windows 快捷键指南";
        g_hotkeyList.clear();
        g_configLoaded = true;
        return false;
    }

    g_titleText.clear();
    g_hotkeyList.clear();

    std::string line;
    std::string currentSection;
    const std::string sectionInfo = "[Info]";
    const std::string sectionKeys = "[Keys]";

    auto trim = [](std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
            }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
            }).base(), s.end());
        };

    auto utf8ToWide = [](const std::string& utf8) -> std::wstring {
        if (utf8.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
        if (len <= 0) return L"";
        std::wstring wstr(len - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], len);
        return wstr;
        };

    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = line;
            continue;
        }

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        trim(key);
        trim(value);
        if (key.empty()) continue;

        if (currentSection == sectionInfo)
        {
            if (key == "Name" && g_titleText.empty())
                g_titleText = utf8ToWide(value);
            else if (key == "Description" && g_titleText.empty())
                g_titleText = utf8ToWide(value);
        }
        else if (currentSection == sectionKeys)
        {
            std::wstring wKey = utf8ToWide(key);
            std::wstring wValue = utf8ToWide(value);
            if (!wKey.empty())
                g_hotkeyList.push_back({ wKey, wValue });
        }
    }

    if (g_titleText.empty())
        g_titleText = L"Windows 快捷键指南";

    g_configLoaded = true;
    return true;
}

// ==================== 读取并解析进程专属 .ini ====================
static bool LoadProcessConfiguration(const std::wstring& processExeName)
{
    g_processTitleText.clear();
    g_processHotkeyList.clear();

    if (processExeName.empty())
        return false;

    std::wstring iniPath = GetExeDirectory() + L"keyboards\\" + processExeName + L".ini";
    std::ifstream file(iniPath);
    if (!file.is_open())
        return false;

    std::string line;
    std::string currentSection;
    const std::string sectionInfo = "[Info]";
    const std::string sectionKeys = "[Keys]";

    auto trim = [](std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
            }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
            }).base(), s.end());
        };

    auto utf8ToWide = [](const std::string& utf8) -> std::wstring {
        if (utf8.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
        if (len <= 0) return L"";
        std::wstring wstr(len - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], len);
        return wstr;
        };

    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = line;
            continue;
        }

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        trim(key);
        trim(value);
        if (key.empty()) continue;

        if (currentSection == sectionInfo)
        {
            if (key == "Name" && g_processTitleText.empty())
                g_processTitleText = utf8ToWide(value);
            else if (key == "Description" && g_processTitleText.empty())
                g_processTitleText = utf8ToWide(value);
        }
        else if (currentSection == sectionKeys)
        {
            std::wstring wKey = utf8ToWide(key);
            std::wstring wValue = utf8ToWide(value);
            if (!wKey.empty())
                g_processHotkeyList.push_back({ wKey, wValue });
        }
    }

    if (g_processTitleText.empty())
        g_processTitleText = processExeName;

    return true;
}

// ==================== 窗口管理 ====================
static BOOL RegisterBubbleClass()
{
    if (g_wc.lpszClassName) return TRUE;
    g_wc.style = CS_HREDRAW | CS_VREDRAW;
    g_wc.lpfnWndProc = BubbleWndProc;
    g_wc.hInstance = GetModuleHandle(NULL);
    g_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    g_wc.hbrBackground = NULL;
    g_wc.lpszClassName = CLASS_NAME;
    return RegisterClass(&g_wc) != 0;
}

// 计算窗口总高度（基于当前屏幕宽度）
static int CalculateWindowHeight(int screenWidth)
{
    const int margin = 15;
    const int titleBottomSpacing = 15;
    const int blockSpacing = 25;
    const int bottomPadding = 15;

    HDC hdc = GetDC(NULL);
    HFONT hTitleFont = CreateFontW(-g_nTitleSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hHotkeyFont = CreateFontW(-g_nLabelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);

    // 标题1高度
    RECT titleRect = { 0, 0, screenWidth - 2 * margin, 0 };
    DrawTextW(hdc, g_titleText.c_str(), -1, &titleRect, DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK);
    int titleHeight1 = titleRect.bottom - titleRect.top;

    // 热键区域1高度
    SelectObject(hdc, hHotkeyFont);
    int hotkeyHeight1 = 0;
    if (!g_hotkeyList.empty())
    {
        int x = margin, y = 0, maxWidth = screenWidth - 2 * margin;
        SIZE textSize;
        GetTextExtentPoint32W(hdc, L"测", 1, &textSize);
        int lineHeight = textSize.cy + 8;
        for (const auto& item : g_hotkeyList)
        {
            std::wstring display = item.first + L"  " + item.second;
            GetTextExtentPoint32W(hdc, display.c_str(), (int)display.length(), &textSize);
            int itemW = textSize.cx + 16;
            if (x + itemW > maxWidth && x > margin)
            {
                y += lineHeight + 8;
                x = margin;
            }
            x += itemW + 12;
        }
        hotkeyHeight1 = y + lineHeight;
    }

    // 进程块高度
    int titleHeight2 = 0, hotkeyHeight2 = 0;
    if (!g_processTitleText.empty())
    {
        SelectObject(hdc, hTitleFont);
        RECT titleRect2 = { 0, 0, screenWidth - 2 * margin, 0 };
        DrawTextW(hdc, g_processTitleText.c_str(), -1, &titleRect2, DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK);
        titleHeight2 = titleRect2.bottom - titleRect2.top;

        if (!g_processHotkeyList.empty())
        {
            SelectObject(hdc, hHotkeyFont);
            int x = margin, y = 0, maxWidth = screenWidth - 2 * margin;
            SIZE textSize;
            GetTextExtentPoint32W(hdc, L"测", 1, &textSize);
            int lineHeight = textSize.cy + 8;
            for (const auto& item : g_processHotkeyList)
            {
                std::wstring display = item.first + L"  " + item.second;
                GetTextExtentPoint32W(hdc, display.c_str(), (int)display.length(), &textSize);
                int itemW = textSize.cx + 16;
                if (x + itemW > maxWidth && x > margin)
                {
                    y += lineHeight + 8;
                    x = margin;
                }
                x += itemW + 12;
            }
            hotkeyHeight2 = y + lineHeight;
        }
    }

    // 清理
    SelectObject(hdc, hOldFont);
    DeleteObject(hTitleFont);
    DeleteObject(hHotkeyFont);
    ReleaseDC(NULL, hdc);

    int total = margin + titleHeight1 + titleBottomSpacing + hotkeyHeight1;
    if (!g_processTitleText.empty())
    {
        total += blockSpacing + titleHeight2;
        if (!g_processHotkeyList.empty())
            total += titleBottomSpacing + hotkeyHeight2;
        else
            total += titleBottomSpacing;
    }
    total += bottomPadding;

    return total;
}

// 绘制标题，返回纯文本高度
static int DrawTitle(HDC hdc, const RECT& clientRect, const std::wstring& titleText, int topOffset)
{
    const int margin = 15;
    RECT rect = { clientRect.left + margin, topOffset, clientRect.right - margin, clientRect.bottom };

    HFONT hFont = CreateFontW(-g_nTitleSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    SetTextColor(hdc, g_crTitle);
    SetBkMode(hdc, TRANSPARENT);

    RECT calcRect = rect;
    DrawTextW(hdc, titleText.c_str(), -1, &calcRect, DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK);
    int height = calcRect.bottom - rect.top;

    DrawTextW(hdc, titleText.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_WORDBREAK);

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);

    return height;
}

// 绘制热键列表，consumedHeight 为实际占用高度
static void DrawHotkeysLayout(HDC hdc, const RECT& rect, int startY,
    const std::vector<std::pair<std::wstring, std::wstring>>& hotkeyList,
    int& consumedHeight)
{
    const int margin = 15;
    const int horzSpacing = 12;
    const int vertSpacing = 8;
    const int padH = 8;
    const int padV = 4;

    if (hotkeyList.empty())
    {
        consumedHeight = 0;
        return;
    }

    HFONT hFont = CreateFontW(-g_nLabelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    SetTextColor(hdc, g_crLabel);
    SetBkMode(hdc, TRANSPARENT);

    SIZE textSize;
    GetTextExtentPoint32W(hdc, L"测", 1, &textSize);
    int lineHeight = textSize.cy + padV * 2;

    int x = margin;
    int y = startY;
    int maxWidth = rect.right - margin;

    for (const auto& item : hotkeyList)
    {
        std::wstring display = item.first + L"  " + item.second;
        GetTextExtentPoint32W(hdc, display.c_str(), (int)display.length(), &textSize);
        int itemWidth = textSize.cx + padH * 2;

        if (x + itemWidth > maxWidth && x > margin)
        {
            y += lineHeight + vertSpacing;
            x = margin;
        }

        RECT itemRect = { x, y, x + itemWidth, y + lineHeight };
        HPEN pen = CreatePen(PS_SOLID, 1, g_crLabel);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom, 6, 6);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBr);
        DeleteObject(pen);

        RECT textRect = { x + padH, y + padV, x + itemWidth - padH, y + lineHeight - padV };
        DrawTextW(hdc, display.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        x += itemWidth + horzSpacing;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);

    consumedHeight = (y + lineHeight) - startY;
}

// ==================== 窗口绘制（WM_PAINT） - 唯一的 OnPaint ====================
static void OnPaint(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);

    // 绘制半透明背景
    {
        int w = clientRect.right - clientRect.left;
        int h = clientRect.bottom - clientRect.top;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        RECT memRect = { 0, 0, w, h };
        HBRUSH brush = CreateSolidBrush(g_crBg);
        FillRect(memDC, &memRect, brush);
        DeleteObject(brush);

        BLENDFUNCTION blend = { AC_SRC_OVER, 0, (BYTE)g_nBgAlpha, 0 };
        AlphaBlend(hdc, 0, 0, w, h, memDC, 0, 0, w, h, blend);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
    }

    const int margin = 15;
    const int titleBottomSpacing = 15;
    const int blockSpacing = 25;

    // 第一块：Windows 标题 + 热键
    int titleH1 = DrawTitle(hdc, clientRect, g_titleText, margin);
    int hotkeyConsumed1 = 0;
    DrawHotkeysLayout(hdc, clientRect, margin + titleH1 + titleBottomSpacing,
        g_hotkeyList, hotkeyConsumed1);

    // 第二块：进程专属（如果有标题）
    if (!g_processTitleText.empty())
    {
        int startY2 = margin + titleH1 + titleBottomSpacing + hotkeyConsumed1 + blockSpacing;
        int titleH2 = DrawTitle(hdc, clientRect, g_processTitleText, startY2);
        if (!g_processHotkeyList.empty())
        {
            DrawHotkeysLayout(hdc, clientRect, startY2 + titleH2 + titleBottomSpacing,
                g_processHotkeyList, hotkeyConsumed1);
        }
    }

    EndPaint(hWnd, &ps);
}

// ==================== 窗口过程 ====================
static LRESULT CALLBACK BubbleWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        OnPaint(hWnd);
        return 0;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_DESTROY:
        g_hBubbleWnd = NULL;
        return 0;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

// ==================== 显示/销毁窗口 ====================
static void ShowBubbleWindow()
{
    if (!RegisterBubbleClass()) return;
    LoadConfiguration();

    std::wstring procName = GetForegroundProcessName();
    LoadProcessConfiguration(procName);

    if (!procName.empty() && g_processTitleText.empty() && g_processHotkeyList.empty())
        g_processTitleText = procName + L" 无配置文件";

    if (g_hBubbleWnd)
        DestroyBubbleWindow();

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenWidth = workArea.right - workArea.left;
    int screenBottom = workArea.bottom;

    int windowHeight = CalculateWindowHeight(screenWidth);
    int maxHeight = workArea.bottom - workArea.top - 20;
    if (windowHeight > maxHeight) windowHeight = maxHeight;

    int windowX = workArea.left;
    int windowY = screenBottom - windowHeight;

    g_hBubbleWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        CLASS_NAME, L"BubbleWindow", WS_POPUP,
        windowX, windowY, screenWidth, windowHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (g_hBubbleWnd)
    {
        SetLayeredWindowAttributes(g_hBubbleWnd, 0, 255, LWA_ALPHA);
        ShowWindow(g_hBubbleWnd, SW_SHOWNOACTIVATE);
        UpdateWindow(g_hBubbleWnd);
    }
}

static void DestroyBubbleWindow()
{
    if (g_hBubbleWnd)
    {
        DestroyWindow(g_hBubbleWnd);
        g_hBubbleWnd = NULL;
    }
}

// ==================== 低级键盘钩子 ====================
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lp;
        DWORD vk = pKb->vkCode;

        BOOL isModifier = (vk == VK_CONTROL) || (vk == VK_LCONTROL) || (vk == VK_RCONTROL) ||
            (vk == VK_MENU) || (vk == VK_LMENU) || (vk == VK_RMENU) ||
            (vk == VK_SHIFT) || (vk == VK_LSHIFT) || (vk == VK_RSHIFT) ||
            (vk == VK_LWIN) || (vk == VK_RWIN);

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
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    g_nModifierCount = 0;
    memset(g_modKeyStates, 0, sizeof(g_modKeyStates));
    LoadConfiguration();
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
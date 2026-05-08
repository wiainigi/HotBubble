#include "BubbleWindow.h"
#include "config.h"          // 全局配置变量 (g_nBgAlpha, g_crBg, g_crTitle, g_nTitleSize等)
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
static HWND        g_hBubbleWnd = NULL;      // 单例窗口句柄
static HHOOK       g_hKeyboardHook = NULL;   // 低级键盘钩子句柄
static int         g_nModifierCount = 0;     // 当前按下的修饰键数量
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

// 配置文件解析结果
static std::wstring       g_titleText;                       // 标题文字（来自[Info]）
static std::vector<std::pair<std::wstring, std::wstring>> g_hotkeyList; // 热键条目 <组合键文本, 功能描述>
static bool               g_configLoaded = false;            // 是否已加载配置

// 前向声明
static LRESULT CALLBACK BubbleWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
static void ShowBubbleWindow();
static void DestroyBubbleWindow();
static bool LoadConfiguration();
static int  CalculateWindowHeight(int screenWidth, int& outTitleHeight, int& outHotkeyAreaHeight);
static void DrawHotkeysLayout(HDC hdc, const RECT& rect, int startY, int titleHeight);

// ==================== 辅助函数：获取当前exe所在目录 ====================
static std::wstring GetExeDirectory()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    return std::wstring(path);
}

// ==================== 读取并解析 windows.ini ====================
static bool LoadConfiguration()
{
    if (g_configLoaded) return true;  // 只加载一次

    std::wstring iniPath = GetExeDirectory() + L"keyboards\\windows.ini";
    std::ifstream file(iniPath);
    if (!file.is_open())
    {
        // 文件不存在时显示默认内容
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
        // 跳过空行和注释（以;或#开头）
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        trim(line);
        if (line.empty()) continue;

        // 节匹配
        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = line;
            continue;
        }

        // 键值对解析
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        trim(key);
        trim(value);
        if (key.empty()) continue;

        if (currentSection == sectionInfo)
        {
            // [Info] 节：优先取 Name，其次 Description
            if (key == "Name" && g_titleText.empty())
                g_titleText = utf8ToWide(value);
            else if (key == "Description" && g_titleText.empty())
                g_titleText = utf8ToWide(value);
        }
        else if (currentSection == sectionKeys)
        {
            // [Keys] 节：存储热键
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

// 计算窗口所需高度（基于当前屏幕宽度和内容）
static int CalculateWindowHeight(int screenWidth, int& outTitleHeight, int& outHotkeyAreaHeight)
{
    const int margin = 15;          // 左右边距
    const int titleBottomMargin = 20; // 标题与热键区域间距
    const int hotkeyHorzSpacing = 12; // 热键项水平间距
    const int hotkeyVertSpacing = 8;  // 热键项垂直间距
    const int hotkeyPaddingH = 8;     // 热键项内边距（左右）
    const int hotkeyPaddingV = 4;     // 热键项内边距（上下）

    // 创建临时DC和字体用于测量
    HDC hdc = GetDC(NULL);
    HFONT hTitleFont = CreateFontW(-g_nTitleSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hHotkeyFont = CreateFontW(-g_nLabelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);

    // 测量标题高度
    RECT titleRect = { 0, 0, screenWidth - 2 * margin, 0 };
    DrawTextW(hdc, g_titleText.c_str(), -1, &titleRect, DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK);
    outTitleHeight = titleRect.bottom + titleBottomMargin;

    // 测量热键区域高度（流式布局）
    SelectObject(hdc, hHotkeyFont);
    SIZE textSize;
    GetTextExtentPoint32W(hdc, L"测", 1, &textSize);
    int lineHeight = textSize.cy + hotkeyPaddingV * 2;

    int x = margin;
    int y = 0;
    int maxWidth = screenWidth - 2 * margin;
    int totalHeight = 0;

    for (const auto& item : g_hotkeyList)
    {
        std::wstring displayText = item.first + L"  " + item.second;
        GetTextExtentPoint32W(hdc, displayText.c_str(), (int)displayText.length(), &textSize);
        int itemWidth = textSize.cx + hotkeyPaddingH * 2;

        if (x + itemWidth > maxWidth && x > margin)
        {
            // 换行
            totalHeight += lineHeight + hotkeyVertSpacing;
            x = margin;
            y = totalHeight;
        }
        x += itemWidth + hotkeyHorzSpacing;
    }
    if (!g_hotkeyList.empty())
        totalHeight += lineHeight + hotkeyVertSpacing; // 最后一行高度

    outHotkeyAreaHeight = totalHeight + margin;

    // 清理
    SelectObject(hdc, hOldFont);
    DeleteObject(hTitleFont);
    DeleteObject(hHotkeyFont);
    ReleaseDC(NULL, hdc);

    return outTitleHeight + outHotkeyAreaHeight;
}

// 绘制热键（流式布局）
static void DrawHotkeysLayout(HDC hdc, const RECT& rect, int startY, int titleHeight)
{
    const int margin = 15;
    const int hotkeyHorzSpacing = 12;
    const int hotkeyVertSpacing = 8;
    const int hotkeyPaddingH = 8;
    const int hotkeyPaddingV = 4;

    HFONT hHotkeyFont = CreateFontW(-g_nLabelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hHotkeyFont);
    SetTextColor(hdc, g_crLabel);
    SetBkMode(hdc, TRANSPARENT);

    int x = margin;
    int y = startY + titleHeight;
    int maxWidth = rect.right - margin;
    SIZE textSize;
    int lineHeight = 0;

    for (size_t i = 0; i < g_hotkeyList.size(); ++i)
    {
        std::wstring displayText = g_hotkeyList[i].first + L"  " + g_hotkeyList[i].second;
        GetTextExtentPoint32W(hdc, displayText.c_str(), (int)displayText.length(), &textSize);
        if (lineHeight == 0) lineHeight = textSize.cy + hotkeyPaddingV * 2;

        int itemWidth = textSize.cx + hotkeyPaddingH * 2;

        // 换行判断
        if (x + itemWidth > maxWidth && x > margin)
        {
            y += lineHeight + hotkeyVertSpacing;
            x = margin;
        }

        // 绘制圆角矩形背景（可选，使用半透明背景色）
        RECT itemRect = { x, y, x + itemWidth, y + lineHeight };
        HPEN hPen = CreatePen(PS_SOLID, 1, g_crLabel);
        HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom, 6, 6);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(hPen);

        // 绘制文字
        RECT textRect = { x + hotkeyPaddingH, y + hotkeyPaddingV,
                          x + itemWidth - hotkeyPaddingH, y + lineHeight - hotkeyPaddingV };
        DrawTextW(hdc, displayText.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        x += itemWidth + hotkeyHorzSpacing;
    }

    SelectObject(hdc, hOldFont);
    DeleteObject(hHotkeyFont);
}

// 显示气泡窗口
static void ShowBubbleWindow()
{
    if (!RegisterBubbleClass()) return;
    LoadConfiguration();  // 确保配置已加载

    // 单例管理
    if (g_hBubbleWnd)
        DestroyBubbleWindow();

    // 获取工作区尺寸
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenWidth = workArea.right - workArea.left;
    int screenBottom = workArea.bottom;

    // 计算窗口高度
    int titleHeight = 0, hotkeyHeight = 0;
    int windowHeight = CalculateWindowHeight(screenWidth, titleHeight, hotkeyHeight);
    // 限制最大高度不超过工作区
    int maxHeight = workArea.bottom - workArea.top - 20;
    if (windowHeight > maxHeight) windowHeight = maxHeight;

    int windowX = workArea.left;
    int windowY = screenBottom - windowHeight;

    // 创建窗口：无边框、置顶、分层、鼠标穿透
    g_hBubbleWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        CLASS_NAME, L"BubbleWindow", WS_POPUP,
        windowX, windowY, screenWidth, windowHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!g_hBubbleWnd) return;

    SetLayeredWindowAttributes(g_hBubbleWnd, 0, 255, LWA_ALPHA);
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
    // 创建与目标 DC 兼容的内存 DC 和位图
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // 先在内存 DC 中填充不透明的背景色
    RECT memRect = { 0, 0, width, height };
    HBRUSH brush = CreateSolidBrush(g_crBg);
    FillRect(memDC, &memRect, brush);
    DeleteObject(brush);

    // 使用 AlphaBlend 将内存 DC 按 g_nBgAlpha 透明度混合到目标 DC
    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = g_nBgAlpha;       // 背景颜色透明度
    blend.AlphaFormat = 0;

    AlphaBlend(hdc, rect.left, rect.top, width, height,
        memDC, 0, 0, width, height, blend);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

static void DrawTitle(HDC hdc, const RECT& clientRect, int& outTitleHeight)
{
    const int margin = 15;
    RECT titleRect = clientRect;
    titleRect.left += margin;
    titleRect.top += margin;
    titleRect.right -= margin;

    HFONT hTitleFont = CreateFontW(-g_nTitleSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);
    SetTextColor(hdc, g_crTitle);
    SetBkMode(hdc, TRANSPARENT);

    // 计算标题实际占用高度
    RECT calcRect = titleRect;
    DrawTextW(hdc, g_titleText.c_str(), -1, &calcRect, DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK);
    outTitleHeight = calcRect.bottom - titleRect.top;

    DrawTextW(hdc, g_titleText.c_str(), -1, &titleRect, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hdc, hOldFont);
    DeleteObject(hTitleFont);
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

        int titleHeight = 0;
        DrawTitle(hdc, clientRect, titleHeight);
        DrawHotkeysLayout(hdc, clientRect, clientRect.top, titleHeight + 15); // 15为标题下边距

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return TRUE;
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
    LoadConfiguration(); // 预先加载配置
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
// Config.cpp
#include "config.h"
#include <windows.h>
#include <stdio.h>

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

// ==================== 全局配置变量（定义） ====================
int      g_nTitleSize = 18;       // 标题字体大小
int      g_nLabelSize = 14;       // 标签字体大小
int      g_nBgAlpha = 180;        // 背景透明度（用户语义：0=不透明，255=全透明）
COLORREF g_crTitle = RGB(50, 150, 250);   // 标题文字颜色（默认白）
COLORREF g_crLabel = RGB(255, 255, 255);   // 标签文字颜色（默认白）
COLORREF g_crBg = RGB(0, 0, 0);            // 背景颜色（默认黑）
COLORREF g_crMark = RGB(255, 68, 72);      // 标记颜色（默认红）
bool g_bShowWindows = true;   // 是否显示系统快捷键（默认显示）
bool g_bShowProcess = true;   // 是否显示程序快捷键（默认显示）

// 获取配置文件路径（与可执行文件同目录）
static const wchar_t* GetConfigFilePath()
{
    static wchar_t path[MAX_PATH] = { 0 };
    if (path[0] == L'\0')
    {
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(path, L'\\');
        if (lastSlash)
            *(lastSlash + 1) = L'\0';
        wcscat_s(path, MAX_PATH, L"config.ini");
    }
    return path;
}

// 辅助：从 "R,G,B" 字符串解析颜色
static COLORREF ParseColor(const wchar_t* str)
{
    int r = 0, g = 0, b = 0;
    swscanf_s(str, L"%d,%d,%d", &r, &g, &b);
    return RGB(r, g, b);
}

// 辅助：将 COLORREF 格式化为 "R,G,B" 字符串
static void FormatColor(COLORREF color, wchar_t* out, size_t size)
{
    swprintf(out, size, L"%d,%d,%d", GetRValue(color), GetGValue(color), GetBValue(color));
}

// 加载配置
void LoadConfig()
{
    const wchar_t* file = GetConfigFilePath();

    // 若配置文件不存在，则使用默认值创建
    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES)
    {
        SaveConfig();   // 此时全局变量均为默认值，保存后生成配置文件
    }

    wchar_t buffer[64];

    // 读取整数
    g_nTitleSize = GetPrivateProfileIntW(L"UIConfig", L"titleFontSize", 16, file);
    g_nLabelSize = GetPrivateProfileIntW(L"UIConfig", L"cardFontSize", 12, file);
    g_nBgAlpha = GetPrivateProfileIntW(L"UIConfig", L"bgAlpha", 255, file);

    // 范围限制
    if (g_nTitleSize < 6)  g_nTitleSize = 6;
    if (g_nTitleSize > 72) g_nTitleSize = 72;
    if (g_nLabelSize < 6)  g_nLabelSize = 6;
    if (g_nLabelSize > 72) g_nLabelSize = 72;
    if (g_nBgAlpha < 0)    g_nBgAlpha = 0;
    if (g_nBgAlpha > 255)  g_nBgAlpha = 255;

    // ==================== 新增：读取布尔配置项 ====================
    // 读取 showWindows，默认值为 1（true）
    int valShowWindows = GetPrivateProfileIntW(L"UIConfig", L"showWindows", 1, file);
    g_bShowWindows = (valShowWindows != 0);
    // 读取 showProcess，默认值为 1（true）
    int valShowProcess = GetPrivateProfileIntW(L"UIConfig", L"showProcess", 1, file);
    g_bShowProcess = (valShowProcess != 0);

    // 读取颜色字符串，若 INI 中没有则使用默认值
    GetPrivateProfileStringW(L"UIConfig", L"titleTextColor", L"255,255,255", buffer, 64, file);
    g_crTitle = ParseColor(buffer);

    GetPrivateProfileStringW(L"UIConfig", L"labelTextColor", L"255,255,255", buffer, 64, file);
    g_crLabel = ParseColor(buffer);

    GetPrivateProfileStringW(L"UIConfig", L"bgColor", L"0,0,0", buffer, 64, file);
    g_crBg = ParseColor(buffer);

    GetPrivateProfileStringW(L"UIConfig", L"activeTextColor", L"255,60,60", buffer, 64, file);
    g_crMark = ParseColor(buffer);
}

// 保存配置
void SaveConfig()
{
    const wchar_t* file = GetConfigFilePath();
    wchar_t value[64];

    // 写入整数
    swprintf(value, 64, L"%d", g_nTitleSize);
    WritePrivateProfileStringW(L"UIConfig", L"titleFontSize", value, file);

    swprintf(value, 64, L"%d", g_nLabelSize);
    WritePrivateProfileStringW(L"UIConfig", L"cardFontSize", value, file);

    swprintf(value, 64, L"%d", g_nBgAlpha);
    WritePrivateProfileStringW(L"UIConfig", L"bgAlpha", value, file);

    // ==================== 新增：写入布尔配置项 ====================
    swprintf(value, 64, L"%d", g_bShowWindows ? 1 : 0);
    WritePrivateProfileStringW(L"UIConfig", L"showWindows", value, file);

    swprintf(value, 64, L"%d", g_bShowProcess ? 1 : 0);
    WritePrivateProfileStringW(L"UIConfig", L"showProcess", value, file);

    // 写入颜色
    FormatColor(g_crTitle, value, 64);
    WritePrivateProfileStringW(L"UIConfig", L"titleTextColor", value, file);

    FormatColor(g_crLabel, value, 64);
    WritePrivateProfileStringW(L"UIConfig", L"labelTextColor", value, file);

    FormatColor(g_crBg, value, 64);
    WritePrivateProfileStringW(L"UIConfig", L"bgColor", value, file);

    FormatColor(g_crMark, value, 64);
    WritePrivateProfileStringW(L"UIConfig", L"activeTextColor", value, file);
}
#include "config.h"
#include <windows.h>
#include <stdio.h>

// 全局配置变量
int      g_nTitleSize = 18;
int      g_nLabelSize = 14;
int      g_nBgAlpha = 180;
COLORREF g_crTitle = RGB(50, 150, 250);
COLORREF g_crLabel = RGB(255, 255, 255);
COLORREF g_crBg = RGB(0, 0, 0);
COLORREF g_crMark = RGB(255, 68, 72);
bool g_bShowWindows = true;
bool g_bShowProcess = true;

// 获取可执行文件同目录下的 config.ini 路径
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

static COLORREF ParseColor(const wchar_t* str)
{
    int r = 0, g = 0, b = 0;
    swscanf_s(str, L"%d,%d,%d", &r, &g, &b);
    return RGB(r, g, b);
}

static void FormatColor(COLORREF color, wchar_t* out, size_t size)
{
    swprintf(out, size, L"%d,%d,%d", GetRValue(color), GetGValue(color), GetBValue(color));
}

void LoadConfig()
{
    const wchar_t* file = GetConfigFilePath();

    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES)
        SaveConfig();

    g_nTitleSize = GetPrivateProfileIntW(L"UIConfig", L"titleFontSize", 16, file);
    g_nLabelSize = GetPrivateProfileIntW(L"UIConfig", L"cardFontSize", 12, file);
    g_nBgAlpha = GetPrivateProfileIntW(L"UIConfig", L"bgAlpha", 255, file);

    if (g_nTitleSize < 6)  g_nTitleSize = 6;
    if (g_nTitleSize > 72) g_nTitleSize = 72;
    if (g_nLabelSize < 6)  g_nLabelSize = 6;
    if (g_nLabelSize > 72) g_nLabelSize = 72;
    if (g_nBgAlpha < 0)    g_nBgAlpha = 0;
    if (g_nBgAlpha > 255)  g_nBgAlpha = 255;

    g_bShowWindows = (GetPrivateProfileIntW(L"UIConfig", L"showWindows", 1, file) != 0);
    g_bShowProcess = (GetPrivateProfileIntW(L"UIConfig", L"showProcess", 1, file) != 0);

    wchar_t buffer[64];
    GetPrivateProfileStringW(L"UIConfig", L"titleTextColor", L"255,255,255", buffer, 64, file);
    g_crTitle = ParseColor(buffer);
    GetPrivateProfileStringW(L"UIConfig", L"labelTextColor", L"255,255,255", buffer, 64, file);
    g_crLabel = ParseColor(buffer);
    GetPrivateProfileStringW(L"UIConfig", L"bgColor", L"0,0,0", buffer, 64, file);
    g_crBg = ParseColor(buffer);
    GetPrivateProfileStringW(L"UIConfig", L"activeTextColor", L"255,60,60", buffer, 64, file);
    g_crMark = ParseColor(buffer);
}

void SaveConfig()
{
    const wchar_t* file = GetConfigFilePath();
    wchar_t value[64];

    swprintf(value, 64, L"%d", g_nTitleSize);
    WritePrivateProfileStringW(L"UIConfig", L"titleFontSize", value, file);
    swprintf(value, 64, L"%d", g_nLabelSize);
    WritePrivateProfileStringW(L"UIConfig", L"cardFontSize", value, file);
    swprintf(value, 64, L"%d", g_nBgAlpha);
    WritePrivateProfileStringW(L"UIConfig", L"bgAlpha", value, file);

    swprintf(value, 64, L"%d", g_bShowWindows ? 1 : 0);
    WritePrivateProfileStringW(L"UIConfig", L"showWindows", value, file);
    swprintf(value, 64, L"%d", g_bShowProcess ? 1 : 0);
    WritePrivateProfileStringW(L"UIConfig", L"showProcess", value, file);

    FormatColor(g_crTitle, value, 64);
    WritePrivateProfileStringW(L"UIConfig", L"titleTextColor", value, file);
    FormatColor(g_crLabel, value, 64);
    WritePrivateProfileStringW(L"UIConfig", L"labelTextColor", value, file);
    FormatColor(g_crBg, value, 64);
    WritePrivateProfileStringW(L"UIConfig", L"bgColor", value, file);
    FormatColor(g_crMark, value, 64);
    WritePrivateProfileStringW(L"UIConfig", L"activeTextColor", value, file);
}

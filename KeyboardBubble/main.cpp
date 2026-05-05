#include <Windows.h>
#include <gdiplus.h>
#include <psapi.h>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <locale>
#include <codecvt>

using namespace Gdiplus;
using namespace std;

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"psapi.lib")
#pragma comment(lib,"shell32.lib")

#define WM_TRAY_MSG (WM_USER + 100)

const int CARD_MARGIN = 8;
const int CARD_PADDING = 8;
const int LINE_SPACING = 10;
const int CARD_HEIGHT = 28;

struct ShortcutItem {
    vector<wstring> keys;
    wstring desc;
};

struct AppConfig {
    wstring name;
    wstring description;
    vector<ShortcutItem> keys;
};

// 颜色结构体
struct ColorConfig {
    int r = 0;
    int g = 0;
    int b = 0;
};

// 样式配置结构体
struct StyleConfig {
    float titleFontSize = 10.0f;      // 标题文字大小
    float cardFontSize = 8.0f;        // 快捷键提示文字大小
    int bgAlpha = 200;                // 背景透明度 (0-255)
    ColorConfig bgColor;              // 背景颜色
    ColorConfig activeTextColor;      // 命中按键文字颜色
    ColorConfig normalTextColor;      // 普通文字颜色
};

HINSTANCE g_hInst;
HWND g_hMainWnd = NULL;
HWND g_hFloatWnd = NULL;
HHOOK g_hHook = NULL;
NOTIFYICONDATAW g_TrayData = { 0 };
HANDLE g_hMutex = NULL;

bool g_keyCtrl = false, g_keyAlt = false, g_keyShift = false, g_keyWin = false;
bool g_lastCtrl = false, g_lastAlt = false, g_lastShift = false, g_lastWin = false;
bool g_visible = false;
AppConfig g_currentConfig;
StyleConfig g_styleConfig;
int g_windowWidth = 0;
int g_windowHeight = 0;
int g_windowX = 0;
int g_windowY = 0;

LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK FloatWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK KeyboardHookProc(int, WPARAM, LPARAM);
void InitTrayIcon();
void ShowTrayMenu(HWND hWnd, POINT pt);
void SetWindowLeftBottom(HWND hWnd, int w, int h);
bool GetExeDir(WCHAR* outPath, int maxLen);
bool FileExists(LPCWSTR path);
wstring UTF8ToWString(const string& str);
bool LoadConfigByPath(LPCWSTR path, AppConfig& cfg);
void LoadConfigForApp(LPCWSTR appName);
int ComputeWindowHeight(int maxWidth);
bool IsKeyActive(const wstring& k);
void RenderWithLayeredWindow();
bool IsKeyStateChanged();
void SaveLastKeyState();
bool CheckSingleInstance();
bool LoadStyleConfig();
string Trim(const string& str);
bool ParseFloat(const string& str, float& outVal);
bool ParseInt(const string& str, int& outVal);
bool ParseColor(const string& colorStr, ColorConfig& color);
string FindValueByKey(const string& json, const string& keyName);
string ReadFileUtf8(const wstring& filePath);

string Trim(const string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

bool ParseFloat(const string& str, float& outVal) {
    if (str.empty()) return false;
    char* end;
    outVal = (float)strtod(str.c_str(), &end);
    return true;
}

bool ParseInt(const string& str, int& outVal) {
    if (str.empty()) return false;
    char* end;
    outVal = (int)strtol(str.c_str(), &end, 10);
    return true;
}

// 解析颜色字符串 "r,g,b"
bool ParseColor(const string& colorStr, ColorConfig& color) {
    string str = Trim(colorStr);
    if (str.empty()) return false;

    vector<int> values;
    stringstream ss(str);
    string item;

    while (getline(ss, item, ',')) {
        string trimmed = Trim(item);
        if (!trimmed.empty()) {
            int val = atoi(trimmed.c_str());
            values.push_back(val);
        }
    }

    if (values.size() >= 3) {
        color.r = values[0];
        color.g = values[1];
        color.b = values[2];
        return true;
    }

    return false;
}

// 读取 UTF-8 文件并转换为 ANSI 字符串（去除 BOM）
string ReadFileUtf8(const wstring& filePath) {
    // 使用 Windows API 读取文件，避免 ifstream 的编码问题
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return "";
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return "";
    }

    vector<char> buffer(fileSize + 1, 0);
    DWORD bytesRead = 0;
    ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    string content(buffer.data(), bytesRead);

    // 去除 UTF-8 BOM (EF BB BF)
    if (content.size() >= 3 &&
        (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF) {
        content = content.substr(3);
    }

    return content;
}

// 简化版：直接解析 JSON 数组中指定 key 的 value
string FindValueByKey(const string& json, const string& keyName) {
    // 查找 "keyName": { 模式
    string searchPattern = "\"" + keyName + "\":{";
    size_t pos = json.find(searchPattern);
    if (pos == string::npos) return "";

    // 找到 value 字段
    size_t valuePos = json.find("\"value\"", pos);
    if (valuePos == string::npos) return "";

    // 找到冒号
    size_t colonPos = json.find(":", valuePos);
    if (colonPos == string::npos) return "";
    colonPos++;

    // 跳过空白
    while (colonPos < json.length() && (json[colonPos] == ' ' || json[colonPos] == '\t' || json[colonPos] == '\n' || json[colonPos] == '\r')) {
        colonPos++;
    }

    // 找到引号开始
    if (colonPos >= json.length() || json[colonPos] != '"') return "";
    colonPos++;

    // 找到引号结束
    size_t endQuote = json.find("\"", colonPos);
    if (endQuote == string::npos) return "";

    return json.substr(colonPos, endQuote - colonPos);
}

bool IsKeyActive(const wstring& k) {
    if (k == L"Ctrl")  return g_keyCtrl;
    if (k == L"Win")   return g_keyWin;
    if (k == L"Shift") return g_keyShift;
    if (k == L"Alt")   return g_keyAlt;
    return false;
}

void SaveLastKeyState() {
    g_lastCtrl = g_keyCtrl;
    g_lastAlt = g_keyAlt;
    g_lastShift = g_keyShift;
    g_lastWin = g_keyWin;
}

bool IsKeyStateChanged() {
    return (g_lastCtrl != g_keyCtrl) || (g_lastAlt != g_keyAlt) ||
        (g_lastShift != g_keyShift) || (g_lastWin != g_keyWin);
}

bool GetExeDir(WCHAR* outPath, int maxLen) {
    ZeroMemory(outPath, maxLen * sizeof(WCHAR));
    GetModuleFileNameW(NULL, outPath, maxLen);
    WCHAR* p = wcsrchr(outPath, L'\\');
    if (p) *p = 0;
    return true;
}

bool FileExists(LPCWSTR path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

wstring UTF8ToWString(const string& str) {
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, 0, 0);
    wstring res(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &res[0], len);
    if (!res.empty()) res.pop_back();
    return res;
}

// 加载样式配置文件
bool LoadStyleConfig() {
    WCHAR baseDir[256] = { 0 };
    GetExeDir(baseDir, 256);
    WCHAR configPath[512] = { 0 };
    swprintf_s(configPath, L"%s\\setting", baseDir);

    // 先设置默认值
    g_styleConfig.bgColor = { 0, 0, 0 };
    g_styleConfig.activeTextColor = { 255, 60, 60 };
    g_styleConfig.normalTextColor = { 255, 255, 255 };
    g_styleConfig.titleFontSize = 10.0f;
    g_styleConfig.cardFontSize = 8.0f;
    g_styleConfig.bgAlpha = 200;

    if (!FileExists(configPath)) {
        // 配置文件不存在，使用默认值
        return true;
    }

    // 使用新的 UTF-8 读取函数
    string json = ReadFileUtf8(configPath);
    if (json.empty()) {
        return true;
    }

    string valStr;
    float fVal;
    int iVal;
    ColorConfig color;

    // 读取标题字体大小
    valStr = FindValueByKey(json, "titleFontSize");
    if (ParseFloat(valStr, fVal)) g_styleConfig.titleFontSize = fVal;

    // 读取卡片字体大小
    valStr = FindValueByKey(json, "cardFontSize");
    if (ParseFloat(valStr, fVal)) g_styleConfig.cardFontSize = fVal;

    // 读取背景透明度
    valStr = FindValueByKey(json, "bgAlpha");
    if (ParseInt(valStr, iVal)) g_styleConfig.bgAlpha = iVal;

    // 读取背景颜色
    valStr = FindValueByKey(json, "bgColor");
    if (ParseColor(valStr, color)) g_styleConfig.bgColor = color;

    // 读取命中按键文字颜色
    valStr = FindValueByKey(json, "activeTextColor");
    if (ParseColor(valStr, color)) g_styleConfig.activeTextColor = color;

    // 读取普通文字颜色
    valStr = FindValueByKey(json, "normalTextColor");
    if (ParseColor(valStr, color)) g_styleConfig.normalTextColor = color;

    // 范围限制
    if (g_styleConfig.bgAlpha < 0) g_styleConfig.bgAlpha = 0;
    if (g_styleConfig.bgAlpha > 255) g_styleConfig.bgAlpha = 255;

    return true;
}

bool LoadConfigByPath(LPCWSTR path, AppConfig& cfg) {
    cfg.keys.clear();
    ifstream ifs(path);
    if (!ifs) return false;
    string json((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
    ifs.close();

    string& s = json;
    size_t p = 0;
    auto getStr = [&](const char* field) -> string {
        size_t a = s.find(field, p);
        if (a == string::npos) return "";
        a = s.find("\"", a + strlen(field) + 1);
        size_t b = s.find("\"", a + 1);
        return s.substr(a + 1, b - a - 1);
        };

    cfg.name = UTF8ToWString(getStr("\"name\""));
    cfg.description = UTF8ToWString(getStr("\"description\""));
    p = s.find("\"keys\"", p);
    while (true) {
        size_t obj = s.find("{", p);
        if (obj == string::npos) break;
        p = obj + 1;
        ShortcutItem item;
        size_t kp = s.find("\"key\"", p);
        if (kp != string::npos) {
            size_t arrStart = s.find("[", kp);
            size_t arrEnd = s.find("]", arrStart);
            string arr = s.substr(arrStart + 1, arrEnd - arrStart - 1);
            size_t ap = 0;
            while (true) {
                size_t a = arr.find("\"", ap);
                if (a == string::npos) break;
                size_t b = arr.find("\"", a + 1);
                if (b == string::npos) break;
                item.keys.push_back(UTF8ToWString(arr.substr(a + 1, b - a - 1)));
                ap = b + 1;
            }
            p = arrEnd + 1;
        }
        item.desc = UTF8ToWString(getStr("\"description\""));
        if (!item.keys.empty()) cfg.keys.push_back(item);
    }
    return true;
}

void LoadConfigForApp(LPCWSTR appName) {
    WCHAR baseDir[256] = { 0 };
    GetExeDir(baseDir, 256);

    if (g_keyWin && !g_keyCtrl && !g_keyAlt && !g_keyShift) {
        WCHAR winPath[512] = { 0 };
        swprintf_s(winPath, L"%s\\keyboards\\windows.json", baseDir);
        if (FileExists(winPath)) {
            LoadConfigByPath(winPath, g_currentConfig);
            return;
        }
    }

    WCHAR appPath[512] = { 0 };
    swprintf_s(appPath, L"%s\\keyboards\\%s.json", baseDir, appName);

    WCHAR defPath[512] = { 0 };
    swprintf_s(defPath, L"%s\\keyboards\\default.exe.json", baseDir);

    if (FileExists(appPath)) {
        LoadConfigByPath(appPath, g_currentConfig);
    }
    else {
        LoadConfigByPath(defPath, g_currentConfig);
    }
}

// 修复高度计算函数，使用配置的文字大小
int ComputeWindowHeight(int maxWidth) {
    if (g_currentConfig.keys.empty()) {
        return (int)(35 + CARD_HEIGHT + 20);
    }

    HDC hdc = GetDC(NULL);
    Graphics* g = new Graphics(hdc);
    Font cardFont(L"Microsoft YaHei", g_styleConfig.cardFontSize);

    int x = 20, y = 35;
    int maxY = y;

    for (auto& item : g_currentConfig.keys) {
        wstring fullTxt;
        for (int i = 0; i < item.keys.size(); i++) {
            if (i > 0) fullTxt += L" + ";
            fullTxt += item.keys[i];
        }
        fullTxt += L"：" + item.desc;

        RectF textBounds;
        g->MeasureString(fullTxt.c_str(), (int)fullTxt.size(), &cardFont, PointF(0, 0), &textBounds);
        int cardWidth = (int)(textBounds.Width + CARD_PADDING * 2 + 4);

        if (x + cardWidth + CARD_MARGIN > maxWidth) {
            x = 20;
            y += CARD_HEIGHT + LINE_SPACING;
        }

        if (y + CARD_HEIGHT > maxY) {
            maxY = y + CARD_HEIGHT;
        }

        x += cardWidth + CARD_MARGIN;
    }

    delete g;
    ReleaseDC(NULL, hdc);

    return maxY + 20;
}

void InitTrayIcon() {
    ZeroMemory(&g_TrayData, sizeof(g_TrayData));
    g_TrayData.cbSize = sizeof(NOTIFYICONDATAW);
    g_TrayData.hWnd = g_hMainWnd;
    g_TrayData.uID = 1001;
    g_TrayData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_SHOWTIP;
    g_TrayData.uCallbackMessage = WM_TRAY_MSG;

    // 尝试加载自定义图标，如果失败则使用默认图标
    HICON hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(103));
    if (hIcon == NULL) {
        hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    g_TrayData.hIcon = hIcon;

    wcscpy_s(g_TrayData.szTip, L"快捷键提示");
    Shell_NotifyIconW(NIM_ADD, &g_TrayData);
}

void ShowTrayMenu(HWND hWnd, POINT pt) {
    HMENU m = CreatePopupMenu();
    AppendMenu(m, MF_STRING, 10001, L"退出");
    SetForegroundWindow(hWnd);
    TrackPopupMenu(m, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(m);
}

void SetWindowLeftBottom(HWND hWnd, int w, int h) {
    RECT r; SystemParametersInfo(SPI_GETWORKAREA, 0, &r, 0);
    g_windowX = 0;
    g_windowY = r.bottom - h;
    SetWindowPos(hWnd, HWND_TOPMOST, g_windowX, g_windowY, w, h, SWP_NOACTIVATE);
}

bool CheckSingleInstance() {
    g_hMutex = CreateMutexW(NULL, TRUE, L"Global\\ShortcutHelper_Mutex");

    if (g_hMutex == NULL) {
        return true;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_hMutex);
        g_hMutex = NULL;

        HWND hExistingWnd = FindWindowW(L"MainWnd", NULL);
        if (hExistingWnd) {
            if (IsIconic(hExistingWnd)) {
                ShowWindow(hExistingWnd, SW_RESTORE);
            }
            SetForegroundWindow(hExistingWnd);
        }
        return false;
    }

    return true;
}

// 使用配置的样式进行渲染
void RenderWithLayeredWindow() {
    if (!g_hFloatWnd || !g_visible) return;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, g_windowWidth, g_windowHeight);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBitmap);

    Graphics g(hdcMem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetCompositingQuality(CompositingQualityHighQuality);

    g.Clear(Color(0, 0, 0, 0));

    // 使用配置的背景颜色和透明度
    Color bgColor(g_styleConfig.bgAlpha,
        g_styleConfig.bgColor.r,
        g_styleConfig.bgColor.g,
        g_styleConfig.bgColor.b);
    SolidBrush bgBrush(bgColor);
    g.FillRectangle(&bgBrush, (REAL)0, (REAL)0, (REAL)g_windowWidth, (REAL)g_windowHeight);

    Font titleFont(L"Microsoft YaHei", g_styleConfig.titleFontSize);
    Font cardFont(L"Microsoft YaHei", g_styleConfig.cardFontSize);

    // 使用配置的文字颜色
    SolidBrush normalText(Color(g_styleConfig.normalTextColor.r,
        g_styleConfig.normalTextColor.g,
        g_styleConfig.normalTextColor.b));
    SolidBrush activeText(Color(g_styleConfig.activeTextColor.r,
        g_styleConfig.activeTextColor.g,
        g_styleConfig.activeTextColor.b));
    Pen borderPen(Color(120, 255, 255, 255), 1.0f);

    wstring title = g_currentConfig.name + L" (" + g_currentConfig.description + L")";
    g.DrawString(title.c_str(), (int)title.size(), &titleFont, PointF(20, 10), &normalText);

    int mw = g_windowWidth;
    int x = 20, y = 35;
    for (auto& item : g_currentConfig.keys) {
        wstring fullTxt;
        for (int i = 0; i < item.keys.size(); i++) {
            if (i > 0) fullTxt += L" + ";
            fullTxt += item.keys[i];
        }
        fullTxt += L"：" + item.desc;

        RectF textBounds;
        g.MeasureString(fullTxt.c_str(), (int)fullTxt.size(), &cardFont, PointF(0, 0), &textBounds);
        int cardWidth = (int)(textBounds.Width + CARD_PADDING * 2 + 4);

        if (x + cardWidth + CARD_MARGIN > mw) { x = 20; y += CARD_HEIGHT + LINE_SPACING; }
        RectF card((REAL)x, (REAL)y, (REAL)cardWidth, (REAL)CARD_HEIGHT);
        g.DrawRectangle(&borderPen, card);

        float tx = (float)x + CARD_PADDING;
        float ty = (float)y + (CARD_HEIGHT - textBounds.Height) / 2.0f;
        float ox = 0;

        for (int i = 0; i < item.keys.size(); i++) {
            if (i > 0) {
                wstring sp = L" + ";
                g.DrawString(sp.c_str(), (int)sp.size(), &cardFont, PointF(tx + ox, ty), &normalText);
                RectF r; g.MeasureString(sp.c_str(), (int)sp.size(), &cardFont, PointF(0, 0), &r);
                ox += r.Width;
            }
            wstring k = item.keys[i];
            SolidBrush* br = IsKeyActive(k) ? &activeText : &normalText;
            g.DrawString(k.c_str(), (int)k.size(), &cardFont, PointF(tx + ox, ty), br);
            RectF kr; g.MeasureString(k.c_str(), (int)k.size(), &cardFont, PointF(0, 0), &kr);
            ox += kr.Width;
        }
        g.DrawString(item.desc.c_str(), (int)item.desc.size(), &cardFont, PointF(tx + ox, ty), &normalText);
        x += cardWidth + CARD_MARGIN;
    }

    Gdiplus::Bitmap bitmap(hBitmap, NULL);
    BitmapData bmpData;
    Rect rect(0, 0, g_windowWidth, g_windowHeight);
    bitmap.LockBits(&rect, ImageLockModeRead, PixelFormat32bppPARGB, &bmpData);

    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    POINT srcPos = { 0, 0 };
    POINT dstPos = { g_windowX, g_windowY };
    SIZE winSize = { g_windowWidth, g_windowHeight };

    UpdateLayeredWindow(g_hFloatWnd, NULL, &dstPos, &winSize, hdcMem, &srcPos, 0, &blend, ULW_ALPHA);

    bitmap.UnlockBits(&bmpData);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

LRESULT CALLBACK MainWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_TRAY_MSG: if (LOWORD(l) == WM_RBUTTONUP) { POINT p; GetCursorPos(&p); ShowTrayMenu(h, p); } break;
    case WM_COMMAND: if (w == 10001) { Shell_NotifyIconW(NIM_DELETE, &g_TrayData); PostQuitMessage(0); } break;
    case WM_DESTROY: Shell_NotifyIconW(NIM_DELETE, &g_TrayData); PostQuitMessage(0); break;
    }
    return DefWindowProc(h, msg, w, l);
}

LRESULT CALLBACK FloatWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProc(h, msg, w, l);
}

void RenderOnce() {
    RenderWithLayeredWindow();
}

LRESULT CALLBACK KeyboardHookProc(int n, WPARAM wp, LPARAM lp) {
    if (n >= 0) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lp;
        bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        switch (k->vkCode) {
        case VK_LCONTROL: case VK_RCONTROL: g_keyCtrl = down; break;
        case VK_LMENU: case VK_RMENU: g_keyAlt = down; break;
        case VK_LSHIFT: case VK_RSHIFT: g_keyShift = down; break;
        case VK_LWIN: case VK_RWIN: g_keyWin = down; break;
        }

        bool has = g_keyCtrl || g_keyAlt || g_keyShift || g_keyWin;
        bool stateChanged = IsKeyStateChanged();
        SaveLastKeyState();

        if (!has) {
            g_visible = false;
            ShowWindow(g_hFloatWnd, SW_HIDE);
            return CallNextHookEx(g_hHook, n, wp, lp);
        }

        if (!g_visible || stateChanged) {
            if (!g_visible) {
                g_visible = true;
                WCHAR app[128] = L"";

                bool pureWinPressed = (g_keyWin && !g_keyCtrl && !g_keyAlt && !g_keyShift);

                if (!pureWinPressed) {
                    HWND hwnd = GetForegroundWindow();
                    DWORD pid;
                    GetWindowThreadProcessId(hwnd, &pid);
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                    if (hProc) {
                        WCHAR path[256] = L"";
                        GetModuleFileNameEx(hProc, NULL, path, 256);
                        WCHAR* name = wcsrchr(path, L'\\');
                        if (name) name++;
                        wcscpy_s(app, name);
                        CloseHandle(hProc);
                    }
                }

                LoadConfigForApp(app);
                int sw = GetSystemMetrics(SM_CXSCREEN);
                g_windowHeight = ComputeWindowHeight(sw);
                g_windowWidth = sw;
                RECT r; SystemParametersInfo(SPI_GETWORKAREA, 0, &r, 0);
                g_windowX = 0;
                g_windowY = r.bottom - g_windowHeight;
                SetWindowPos(g_hFloatWnd, NULL, g_windowX, g_windowY, g_windowWidth, g_windowHeight, SWP_NOACTIVATE);
                ShowWindow(g_hFloatWnd, SW_SHOWNOACTIVATE);
            }
            RenderOnce();
        }
    }
    return CallNextHookEx(g_hHook, n, wp, lp);
}

int WINAPI wWinMain(HINSTANCE ins, HINSTANCE prev, LPWSTR cmd, int nShow) {
    if (!CheckSingleInstance()) {
        return 0;
    }

    g_hInst = ins;

    // 加载样式配置
    LoadStyleConfig();

    GdiplusStartupInput gdiIn;
    ULONG_PTR token;
    GdiplusStartup(&token, &gdiIn, NULL);

    // 设置主窗口图标
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = ins;
    wc.lpszClassName = L"MainWnd";
    // 尝试加载自定义程序图标，如果失败则使用默认图标
    HICON hMainIcon = LoadIcon(ins, MAKEINTRESOURCE(102));
    if (hMainIcon == NULL) {
        hMainIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    wc.hIcon = hMainIcon;
    wc.hIconSm = hMainIcon;
    RegisterClassExW(&wc);

    g_hMainWnd = CreateWindowExW(0, L"MainWnd", L"", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, ins, NULL);
    ShowWindow(g_hMainWnd, SW_HIDE);
    InitTrayIcon();

    WNDCLASSEXW wcf = { 0 };
    wcf.cbSize = sizeof(WNDCLASSEXW);
    wcf.lpfnWndProc = FloatWndProc;
    wcf.hInstance = ins;
    wcf.lpszClassName = L"FloatUI";
    wcf.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wcf);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    g_hFloatWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        L"FloatUI", L"", WS_POPUP, 0, 0, sw, 200, NULL, NULL, ins, NULL);

    ShowWindow(g_hFloatWnd, SW_HIDE);
    SaveLastKeyState();

    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandle(NULL), 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    UnhookWindowsHookEx(g_hHook);

    if (g_hMutex) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
    }

    // 清理图标资源
    if (hMainIcon) DestroyIcon(hMainIcon);
    if (g_TrayData.hIcon) DestroyIcon(g_TrayData.hIcon);

    GdiplusShutdown(token);
    return 0;
}
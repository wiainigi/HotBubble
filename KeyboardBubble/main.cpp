#include <Windows.h>
#include <gdiplus.h>
#include <psapi.h>
#include <commdlg.h>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <cstring>

using namespace Gdiplus;
using namespace std;

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"psapi.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"comdlg32.lib")

#define WM_TRAY_MSG (WM_USER + 100)
#define ID_TRAY_ABOUT 10001
#define ID_TRAY_SETTINGS 10002
#define ID_TRAY_EXIT 10003

#define ID_BTN_OK 2001
#define ID_BTN_CANCEL 2002

#define IDC_EDIT_TITLE_FONT 3001
#define IDC_EDIT_CARD_FONT 3002
#define IDC_EDIT_BG_ALPHA 3003
#define IDC_EDIT_BG_COLOR 3004
#define IDC_EDIT_ACTIVE_COLOR 3005
#define IDC_EDIT_NORMAL_COLOR 3006
#define IDC_BTN_BG_COLOR 3014
#define IDC_BTN_ACTIVE_COLOR 3015
#define IDC_BTN_NORMAL_COLOR 3016

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

struct ColorConfig {
    int r = 0;
    int g = 0;
    int b = 0;
};

struct StyleConfig {
    float titleFontSize = 10.0f;
    float cardFontSize = 8.0f;
    int bgAlpha = 200;
    ColorConfig bgColor;
    ColorConfig activeTextColor;
    ColorConfig normalTextColor;
};

HINSTANCE g_hInst;
HWND g_hMainWnd = NULL;
HWND g_hFloatWnd = NULL;
HHOOK g_hHook = NULL;
NOTIFYICONDATAW g_TrayData = { 0 };
HANDLE g_hMutex = NULL;
HWND g_hAboutWnd = NULL;
HWND g_hSettingsWnd = NULL;

bool g_keyCtrl = false, g_keyAlt = false, g_keyShift = false, g_keyWin = false;
bool g_lastCtrl = false, g_lastAlt = false, g_lastShift = false, g_lastWin = false;
bool g_visible = false;
bool g_bubbleOnTop = false;
AppConfig g_currentConfig;
StyleConfig g_styleConfig;
int g_windowWidth = 0;
int g_windowHeight = 0;
int g_windowX = 0;
int g_windowY = 0;

LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK FloatWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK KeyboardHookProc(int, WPARAM, LPARAM);
LRESULT CALLBACK AboutWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
void InitTrayIcon();
void ShowTrayMenu(HWND hWnd, POINT pt);
void SetWindowLeftBottom(HWND hWnd, int w, int h);
bool GetExeDir(WCHAR* outPath, int maxLen);
bool FileExists(LPCWSTR path);
wstring UTF8ToWString(const string& str);
bool LoadConfigByPath(LPCWSTR path, AppConfig& cfg);
void LoadConfigForApp(LPCWSTR appName);
void ComputeBubbleSize(int& outWidth, int& outHeight);
bool IsKeyActive(const wstring& k);
void RenderWithLayeredWindow();
bool IsKeyStateChanged();
void SaveLastKeyState();
bool CheckSingleInstance();
bool LoadStyleConfig();
bool SaveStyleConfig();
string Trim(const string& str);
bool ParseFloat(const string& str, float& outVal);
bool ParseInt(const string& str, int& outVal);
bool ParseColor(const string& colorStr, ColorConfig& color);
string FindValueByKey(const string& json, const string& keyName);
string ReadFileUtf8(const wstring& filePath);
void ShowAboutWindow();
void ShowSettingsWindow();
void SaveAndRefreshSettings();
POINT GetPopupAnchor();
void RepositionBubble();

static const wchar_t* kSettingFileName = L"setting";
static const wchar_t* kAppName = L"KeyboardBubble";
static const wchar_t* kAboutText = L"软件名称：KeyboardBubble\r\n软件作用：按住组合键时显示快捷键提示\r\n开发者：WangZ\r\n网址：https://github.com/";
static const wchar_t* kAboutUrl = L"https://github.com/";

string Trim(const string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

bool ParseFloat(const string& str, float& outVal) {
    if (str.empty()) return false;
    char* end = nullptr;
    outVal = (float)strtod(str.c_str(), &end);
    return end != str.c_str();
}

bool ParseInt(const string& str, int& outVal) {
    if (str.empty()) return false;
    char* end = nullptr;
    outVal = (int)strtol(str.c_str(), &end, 10);
    return end != str.c_str();
}

bool ParseColor(const string& colorStr, ColorConfig& color) {
    string str = Trim(colorStr);
    if (str.empty()) return false;
    vector<int> values;
    stringstream ss(str);
    string item;
    while (getline(ss, item, ',')) {
        string trimmed = Trim(item);
        if (!trimmed.empty()) values.push_back(atoi(trimmed.c_str()));
    }
    if (values.size() >= 3) {
        color.r = values[0]; color.g = values[1]; color.b = values[2];
        return true;
    }
    return false;
}

string ReadFileUtf8(const wstring& filePath) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return "";
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) { CloseHandle(hFile); return ""; }
    vector<char> buffer(fileSize + 1, 0);
    DWORD bytesRead = 0;
    ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    string content(buffer.data(), bytesRead);
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF && (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) content = content.substr(3);
    return content;
}

string FindValueByKey(const string& json, const string& keyName) {
    string searchPattern = "\"" + keyName + "\":{";
    size_t pos = json.find(searchPattern);
    if (pos == string::npos) return "";
    size_t valuePos = json.find("\"value\"", pos);
    if (valuePos == string::npos) return "";
    size_t colonPos = json.find(":", valuePos);
    if (colonPos == string::npos) return "";
    colonPos++;
    while (colonPos < json.length() && isspace((unsigned char)json[colonPos])) colonPos++;
    if (colonPos >= json.length() || json[colonPos] != '"') return "";
    colonPos++;
    size_t endQuote = json.find("\"", colonPos);
    if (endQuote == string::npos) return "";
    return json.substr(colonPos, endQuote - colonPos);
}

bool IsKeyActive(const wstring& k) {
    if (k == L"Ctrl") return g_keyCtrl;
    if (k == L"Win") return g_keyWin;
    if (k == L"Shift") return g_keyShift;
    if (k == L"Alt") return g_keyAlt;
    return false;
}

void SaveLastKeyState() { g_lastCtrl = g_keyCtrl; g_lastAlt = g_keyAlt; g_lastShift = g_keyShift; g_lastWin = g_keyWin; }
bool IsKeyStateChanged() { return g_lastCtrl != g_keyCtrl || g_lastAlt != g_keyAlt || g_lastShift != g_keyShift || g_lastWin != g_keyWin; }

bool GetExeDir(WCHAR* outPath, int maxLen) {
    ZeroMemory(outPath, maxLen * sizeof(WCHAR));
    GetModuleFileNameW(NULL, outPath, maxLen);
    WCHAR* p = wcsrchr(outPath, L'\\');
    if (p) *p = 0;
    return true;
}

bool FileExists(LPCWSTR path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

wstring UTF8ToWString(const string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (len <= 0) return L"";
    wstring res(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &res[0], len);
    return res;
}

bool LoadStyleConfig() {
    WCHAR baseDir[256] = { 0 };
    GetExeDir(baseDir, 256);
    WCHAR configPath[512] = { 0 };
    swprintf_s(configPath, L"%s\\%s", baseDir, kSettingFileName);
    g_styleConfig.bgColor = { 0, 0, 0 };
    g_styleConfig.activeTextColor = { 255, 60, 60 };
    g_styleConfig.normalTextColor = { 255, 255, 255 };
    g_styleConfig.titleFontSize = 10.0f;
    g_styleConfig.cardFontSize = 8.0f;
    g_styleConfig.bgAlpha = 200;
    if (!FileExists(configPath)) return true;
    string json = ReadFileUtf8(configPath);
    if (json.empty()) return true;
    string valStr; float fVal; int iVal; ColorConfig color;
    valStr = FindValueByKey(json, "titleFontSize"); if (ParseFloat(valStr, fVal)) g_styleConfig.titleFontSize = fVal;
    valStr = FindValueByKey(json, "cardFontSize"); if (ParseFloat(valStr, fVal)) g_styleConfig.cardFontSize = fVal;
    valStr = FindValueByKey(json, "bgAlpha"); if (ParseInt(valStr, iVal)) g_styleConfig.bgAlpha = iVal;
    valStr = FindValueByKey(json, "bgColor"); if (ParseColor(valStr, color)) g_styleConfig.bgColor = color;
    valStr = FindValueByKey(json, "activeTextColor"); if (ParseColor(valStr, color)) g_styleConfig.activeTextColor = color;
    valStr = FindValueByKey(json, "normalTextColor"); if (ParseColor(valStr, color)) g_styleConfig.normalTextColor = color;
    if (g_styleConfig.bgAlpha < 0) g_styleConfig.bgAlpha = 0;
    if (g_styleConfig.bgAlpha > 255) g_styleConfig.bgAlpha = 255;
    return true;
}

bool SaveStyleConfig() {
    WCHAR baseDir[256] = { 0 };
    GetExeDir(baseDir, 256);
    WCHAR configPath[512] = { 0 };
    swprintf_s(configPath, L"%s\\%s", baseDir, kSettingFileName);
    wofstream ofs(configPath, ios::binary);
    if (!ofs) return false;
    ofs << L"{\n";
    ofs << L"  \"titleFontSize\": { \"value\": \"" << g_styleConfig.titleFontSize << L"\" },\n";
    ofs << L"  \"cardFontSize\": { \"value\": \"" << g_styleConfig.cardFontSize << L"\" },\n";
    ofs << L"  \"bgAlpha\": { \"value\": \"" << g_styleConfig.bgAlpha << L"\" },\n";
    ofs << L"  \"bgColor\": { \"value\": \"" << g_styleConfig.bgColor.r << L"," << g_styleConfig.bgColor.g << L"," << g_styleConfig.bgColor.b << L"\" },\n";
    ofs << L"  \"activeTextColor\": { \"value\": \"" << g_styleConfig.activeTextColor.r << L"," << g_styleConfig.activeTextColor.g << L"," << g_styleConfig.activeTextColor.b << L"\" },\n";
    ofs << L"  \"normalTextColor\": { \"value\": \"" << g_styleConfig.normalTextColor.r << L"," << g_styleConfig.normalTextColor.g << L"," << g_styleConfig.normalTextColor.b << L"\" }\n";
    ofs << L"}\n";
    return true;
}

bool LoadConfigByPath(LPCWSTR path, AppConfig& cfg) {
    cfg.keys.clear();
    ifstream ifs(path);
    if (!ifs) return false;
    string json((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
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
        if (FileExists(winPath)) { LoadConfigByPath(winPath, g_currentConfig); return; }
    }
    WCHAR appPath[512] = { 0 };
    swprintf_s(appPath, L"%s\\keyboards\\%s.json", baseDir, appName);
    WCHAR defPath[512] = { 0 };
    swprintf_s(defPath, L"%s\\keyboards\\default.exe.json", baseDir);
    if (FileExists(appPath)) LoadConfigByPath(appPath, g_currentConfig); else LoadConfigByPath(defPath, g_currentConfig);
}

void ComputeBubbleSize(int& outWidth, int& outHeight) {
    const int leftPadding = 20;
    const int rightPadding = 20;
    const int topPadding = 35;
    const int bottomPadding = 20;
    const int minWidth = 320;
    if (g_currentConfig.keys.empty()) {
        outWidth = minWidth;
        outHeight = (int)(topPadding + CARD_HEIGHT + bottomPadding);
        return;
    }
    HDC hdc = GetDC(NULL);
    Graphics g(hdc);
    Font cardFont(L"Microsoft YaHei", g_styleConfig.cardFontSize);
    int x = leftPadding, y = topPadding, maxY = y, maxRight = leftPadding;
    for (auto& item : g_currentConfig.keys) {
        wstring fullTxt;
        for (int i = 0; i < (int)item.keys.size(); i++) { if (i > 0) fullTxt += L" + "; fullTxt += item.keys[i]; }
        fullTxt += L"：" + item.desc;
        RectF textBounds;
        g.MeasureString(fullTxt.c_str(), (int)fullTxt.size(), &cardFont, PointF(0, 0), &textBounds);
        int cardWidth = (int)(textBounds.Width + CARD_PADDING * 2 + 4);
        if (x + cardWidth + CARD_MARGIN > 1920) { x = leftPadding; y += CARD_HEIGHT + LINE_SPACING; }
        if (y + CARD_HEIGHT > maxY) maxY = y + CARD_HEIGHT;
        if (x + cardWidth > maxRight) maxRight = x + cardWidth;
        x += cardWidth + CARD_MARGIN;
    }
    ReleaseDC(NULL, hdc);
    outWidth = max(minWidth, maxRight + rightPadding);
    outHeight = maxY + bottomPadding;
}

bool IsCursorInLowerHalf() {
    POINT pt{ 0,0 };
    GetCursorPos(&pt);
    RECT wa; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    return pt.y > (wa.top + wa.bottom) / 2;
}

void RepositionBubble() {
    RECT wa; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    g_windowX = 0;
    g_bubbleOnTop = IsCursorInLowerHalf();
    g_windowY = g_bubbleOnTop ? wa.top : (wa.bottom - g_windowHeight);
    SetWindowPos(g_hFloatWnd, HWND_TOPMOST, g_windowX, g_windowY, g_windowWidth, g_windowHeight, SWP_NOACTIVATE);
}

void InitTrayIcon() {
    ZeroMemory(&g_TrayData, sizeof(g_TrayData));
    g_TrayData.cbSize = sizeof(NOTIFYICONDATAW);
    g_TrayData.hWnd = g_hMainWnd;
    g_TrayData.uID = 1001;
    g_TrayData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_SHOWTIP;
    g_TrayData.uCallbackMessage = WM_TRAY_MSG;
    HICON hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(103));
    if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);
    g_TrayData.hIcon = hIcon;
    wcscpy_s(g_TrayData.szTip, L"快捷键提示");
    Shell_NotifyIconW(NIM_ADD, &g_TrayData);
}

void ShowTrayMenu(HWND hWnd, POINT pt) {
    HMENU m = CreatePopupMenu();
    AppendMenu(m, MF_STRING, ID_TRAY_ABOUT, L"关于");
    AppendMenu(m, MF_STRING, ID_TRAY_SETTINGS, L"设置");
    AppendMenu(m, MF_STRING, ID_TRAY_EXIT, L"退出程序");
    SetForegroundWindow(hWnd);
    TrackPopupMenu(m, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(m);
}

bool CheckSingleInstance() {
    g_hMutex = CreateMutexW(NULL, TRUE, L"Global\\ShortcutHelper_Mutex");
    if (g_hMutex == NULL) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_hMutex); g_hMutex = NULL;
        HWND hExistingWnd = FindWindowW(L"MainWnd", NULL);
        if (hExistingWnd) { if (IsIconic(hExistingWnd)) ShowWindow(hExistingWnd, SW_RESTORE); SetForegroundWindow(hExistingWnd); }
        return false;
    }
    return true;
}

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
    Color bgColor(g_styleConfig.bgAlpha, g_styleConfig.bgColor.r, g_styleConfig.bgColor.g, g_styleConfig.bgColor.b);
    SolidBrush bgBrush(bgColor);
    g.FillRectangle(&bgBrush, 0, 0, g_windowWidth, g_windowHeight);
    Font titleFont(L"Microsoft YaHei", g_styleConfig.titleFontSize);
    Font cardFont(L"Microsoft YaHei", g_styleConfig.cardFontSize);
    SolidBrush normalText(Color(g_styleConfig.normalTextColor.r, g_styleConfig.normalTextColor.g, g_styleConfig.normalTextColor.b));
    SolidBrush activeText(Color(g_styleConfig.activeTextColor.r, g_styleConfig.activeTextColor.g, g_styleConfig.activeTextColor.b));
    Pen borderPen(Color(120, 255, 255, 255), 1.0f);
    wstring title = g_currentConfig.name + L" (" + g_currentConfig.description + L")";
    g.DrawString(title.c_str(), (int)title.size(), &titleFont, PointF(20, 10), &normalText);
    int mw = g_windowWidth;
    int x = 20, y = 35;
    for (auto& item : g_currentConfig.keys) {
        wstring fullTxt;
        for (int i = 0; i < (int)item.keys.size(); i++) { if (i > 0) fullTxt += L" + "; fullTxt += item.keys[i]; }
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
        for (int i = 0; i < (int)item.keys.size(); i++) {
            if (i > 0) {
                wstring sp = L" + ";
                g.DrawString(sp.c_str(), (int)sp.size(), &cardFont, PointF(tx + ox, ty), &normalText);
                RectF r; g.MeasureString(sp.c_str(), (int)sp.size(), &cardFont, PointF(0, 0), &r); ox += r.Width;
            }
            wstring k = item.keys[i];
            SolidBrush* br = IsKeyActive(k) ? &activeText : &normalText;
            g.DrawString(k.c_str(), (int)k.size(), &cardFont, PointF(tx + ox, ty), br);
            RectF kr; g.MeasureString(k.c_str(), (int)k.size(), &cardFont, PointF(0, 0), &kr); ox += kr.Width;
        }
        g.DrawString(item.desc.c_str(), (int)item.desc.size(), &cardFont, PointF(tx + ox, ty), &normalText);
        x += cardWidth + CARD_MARGIN;
    }
    Bitmap bitmap(hBitmap, NULL);
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

void CenterWindow(HWND hWnd, int w, int h) {
    RECT wa{}; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (wa.right - wa.left - w) / 2;
    int y = wa.top + (wa.bottom - wa.top - h) / 2;
    SetWindowPos(hWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void ShowAboutWindow() {
    if (g_hAboutWnd) { ShowWindow(g_hAboutWnd, SW_SHOWNORMAL); SetForegroundWindow(g_hAboutWnd); CenterWindow(g_hAboutWnd, 360, 230); return; }
    const wchar_t* cls = L"AboutWndClass";
    WNDCLASSW wc{};
    wc.lpfnWndProc = AboutWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    g_hAboutWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, cls, L"关于", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 360, 230, g_hMainWnd, NULL, g_hInst, NULL);
    SetLayeredWindowAttributes(g_hAboutWnd, 0, (BYTE)g_styleConfig.bgAlpha, LWA_ALPHA);
    CenterWindow(g_hAboutWnd, 360, 230);
    ShowWindow(g_hAboutWnd, SW_SHOWNORMAL);
}

void ShowSettingsWindow() {
    if (g_hSettingsWnd) { ShowWindow(g_hSettingsWnd, SW_SHOWNORMAL); SetForegroundWindow(g_hSettingsWnd); CenterWindow(g_hSettingsWnd, 540, 390); return; }
    const wchar_t* cls = L"SettingsWndClass";
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    g_hSettingsWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, cls, L"设置", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 540, 390, g_hMainWnd, NULL, g_hInst, NULL);
    SetLayeredWindowAttributes(g_hSettingsWnd, 0, (BYTE)g_styleConfig.bgAlpha, LWA_ALPHA);
    CenterWindow(g_hSettingsWnd, 540, 390);
    ShowWindow(g_hSettingsWnd, SW_SHOWNORMAL);
}

void SaveAndRefreshSettings() {
    SaveStyleConfig();
    if (g_visible) RenderWithLayeredWindow();
}

void SetWindowLeftBottom(HWND hWnd, int w, int h) {
    RECT r; SystemParametersInfo(SPI_GETWORKAREA, 0, &r, 0);
    g_windowX = 0; g_windowY = r.bottom - h;
    SetWindowPos(hWnd, HWND_TOPMOST, g_windowX, g_windowY, w, h, SWP_NOACTIVATE);
}

LRESULT CALLBACK MainWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_TRAY_MSG:
        if (LOWORD(l) == WM_RBUTTONUP) { POINT p; GetCursorPos(&p); ShowTrayMenu(h, p); }
        break;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_TRAY_ABOUT: ShowAboutWindow(); break;
        case ID_TRAY_SETTINGS: ShowSettingsWindow(); break;
        case ID_TRAY_EXIT: Shell_NotifyIconW(NIM_DELETE, &g_TrayData); PostQuitMessage(0); break;
        }
        break;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_TrayData);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(h, msg, w, l);
}

LRESULT CALLBACK FloatWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PAINT) { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); return 0; }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProc(h, msg, w, l);
}

LRESULT CALLBACK AboutWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"BUTTON", L"×", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 312, 12, 28, 28, h, (HMENU)ID_BTN_CANCEL, g_hInst, NULL);
        SetLayeredWindowAttributes(h, 0, (BYTE)g_styleConfig.bgAlpha, LWA_ALPHA);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = (msg == WM_PAINT) ? BeginPaint(h, &ps) : GetDC(h);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.Clear(Color(g_styleConfig.bgAlpha, g_styleConfig.bgColor.r, g_styleConfig.bgColor.g, g_styleConfig.bgColor.b));
        Pen borderPen(Color(255, 255, 255, 255), 2.0f);
        g.DrawRectangle(&borderPen, 1, 1, 357, 227);
        Font titleFont(L"Microsoft YaHei", g_styleConfig.titleFontSize + 4.0f, FontStyleBold);
        Font bodyFont(L"Microsoft YaHei", g_styleConfig.cardFontSize + 2.0f);
        SolidBrush textBrush(Color(g_styleConfig.normalTextColor.r, g_styleConfig.normalTextColor.g, g_styleConfig.normalTextColor.b));
        g.DrawString(L"关于", -1, &titleFont, PointF(20, 18), &textBrush);
        g.DrawString(L"软件名称：KeyboardBubble", -1, &bodyFont, PointF(20, 62), &textBrush);
        g.DrawString(L"软件作用：按住组合键时显示快捷键提示", -1, &bodyFont, PointF(20, 92), &textBrush);
        g.DrawString(L"开发者：WangZ", -1, &bodyFont, PointF(20, 122), &textBrush);
        g.DrawString(L"网址：https://github.com/", -1, &bodyFont, PointF(20, 152), &textBrush);
        if (msg == WM_PAINT) EndPaint(h, &ps); else ReleaseDC(h, hdc);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_BTN_CANCEL) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_hAboutWnd = NULL;
        return 0;
    }
    return DefWindowProc(h, msg, w, l);
}

static void FillEdit(HWND parent, int x, int y, int w, int id, const wchar_t* text) {
    HWND e = CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x, y, w, 24, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

static void FillButton(HWND parent, const wchar_t* text, int x, int y, int w, int id) {
    HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, 26, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

static void SetEditTextInt(HWND h, int id, int v) {
    wchar_t buf[32]; swprintf_s(buf, L"%d", v); SetDlgItemTextW(h, id, buf);
}
static void SetEditTextFloat(HWND h, int id, float v) {
    wchar_t buf[32]; swprintf_s(buf, L"%.2f", v); SetDlgItemTextW(h, id, buf);
}
static void SetEditTextColor(HWND h, int id, const ColorConfig& c) {
    wchar_t buf[64]; swprintf_s(buf, L"%d,%d,%d", c.r, c.g, c.b); SetDlgItemTextW(h, id, buf);
}
static ColorConfig GetColorFromEdit(HWND h, int id) {
    wchar_t buf[64]{}; GetDlgItemTextW(h, id, buf, 64); ColorConfig c; swscanf_s(buf, L"%d,%d,%d", &c.r, &c.g, &c.b); return c;
}

LRESULT CALLBACK SettingsWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        // CreateWindowW(L"STATIC", L"设置", WS_CHILD | WS_VISIBLE, 20, 14, 120, 28, h, NULL, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"×", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 500, 10, 24, 24, h, (HMENU)ID_BTN_CANCEL, g_hInst, NULL);
        int x1 = 22, x2 = 210, y = 58, gap = 36;
        CreateWindowW(L"STATIC", L"标题字体大小", WS_CHILD | WS_VISIBLE, x1, y, 120, 20, h, NULL, g_hInst, NULL);
        FillEdit(h, x2, y - 2, 130, IDC_EDIT_TITLE_FONT, L"");
        y += gap;
        CreateWindowW(L"STATIC", L"卡片字体大小", WS_CHILD | WS_VISIBLE, x1, y, 120, 20, h, NULL, g_hInst, NULL);
        FillEdit(h, x2, y - 2, 130, IDC_EDIT_CARD_FONT, L"");
        y += gap;
        CreateWindowW(L"STATIC", L"背景透明度", WS_CHILD | WS_VISIBLE, x1, y, 120, 20, h, NULL, g_hInst, NULL);
        FillEdit(h, x2, y - 2, 130, IDC_EDIT_BG_ALPHA, L"");
        y += gap;
        CreateWindowW(L"STATIC", L"背景颜色(r,g,b)", WS_CHILD | WS_VISIBLE, x1, y, 120, 20, h, NULL, g_hInst, NULL);
        FillEdit(h, x2, y - 2, 130, IDC_EDIT_BG_COLOR, L"");
        FillButton(h, L"选择", 352, y - 2, 70, IDC_BTN_BG_COLOR);
        y += gap;
        CreateWindowW(L"STATIC", L"高亮文字颜色", WS_CHILD | WS_VISIBLE, x1, y, 120, 20, h, NULL, g_hInst, NULL);
        FillEdit(h, x2, y - 2, 130, IDC_EDIT_ACTIVE_COLOR, L"");
        FillButton(h, L"选择", 352, y - 2, 70, IDC_BTN_ACTIVE_COLOR);
        y += gap;
        CreateWindowW(L"STATIC", L"普通文字颜色", WS_CHILD | WS_VISIBLE, x1, y, 120, 20, h, NULL, g_hInst, NULL);
        FillEdit(h, x2, y - 2, 130, IDC_EDIT_NORMAL_COLOR, L"");
        FillButton(h, L"选择", 352, y - 2, 70, IDC_BTN_NORMAL_COLOR);
        y += 58;
        FillButton(h, L"确定", 200, y, 70, ID_BTN_OK);
        FillButton(h, L"取消", 290, y, 70, ID_BTN_CANCEL);
        SetEditTextFloat(h, IDC_EDIT_TITLE_FONT, g_styleConfig.titleFontSize);
        SetEditTextFloat(h, IDC_EDIT_CARD_FONT, g_styleConfig.cardFontSize);
        SetEditTextInt(h, IDC_EDIT_BG_ALPHA, g_styleConfig.bgAlpha);
        SetEditTextColor(h, IDC_EDIT_BG_COLOR, g_styleConfig.bgColor);
        SetEditTextColor(h, IDC_EDIT_ACTIVE_COLOR, g_styleConfig.activeTextColor);
        SetEditTextColor(h, IDC_EDIT_NORMAL_COLOR, g_styleConfig.normalTextColor);
        SetLayeredWindowAttributes(h, 0, (BYTE)g_styleConfig.bgAlpha, LWA_ALPHA);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(h, &ps);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.Clear(Color(g_styleConfig.bgAlpha, g_styleConfig.bgColor.r, g_styleConfig.bgColor.g, g_styleConfig.bgColor.b));
        Pen borderPen(Color(255, 255, 255, 255), 2.0f);
        g.DrawRectangle(&borderPen, 1, 1, 537, 387);
        Font titleFont(L"Microsoft YaHei", g_styleConfig.titleFontSize + 4.0f, FontStyleBold);
        SolidBrush textBrush(Color(g_styleConfig.normalTextColor.r, g_styleConfig.normalTextColor.g, g_styleConfig.normalTextColor.b));
        g.DrawString(L"设置", -1, &titleFont, PointF(22, 16), &textBrush);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)w;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(g_styleConfig.normalTextColor.r, g_styleConfig.normalTextColor.g, g_styleConfig.normalTextColor.b));
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_BTN_BG_COLOR:
        case IDC_BTN_ACTIVE_COLOR:
        case IDC_BTN_NORMAL_COLOR: {
            int editId = LOWORD(w) == IDC_BTN_BG_COLOR ? IDC_EDIT_BG_COLOR : (LOWORD(w) == IDC_BTN_ACTIVE_COLOR ? IDC_EDIT_ACTIVE_COLOR : IDC_EDIT_NORMAL_COLOR);
            wchar_t buf[64]{}; GetDlgItemTextW(h, editId, buf, 64);
            ColorConfig c{}; swscanf_s(buf, L"%d,%d,%d", &c.r, &c.g, &c.b);
            CHOOSECOLORW cc{}; COLORREF custom[16]{}; cc.lStructSize = sizeof(cc); cc.hwndOwner = h; cc.lpCustColors = custom; cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            cc.rgbResult = RGB(c.r, c.g, c.b);
            if (ChooseColorW(&cc)) {
                ColorConfig nc{ GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult) };
                SetEditTextColor(h, editId, nc);
            }
            return 0;
        }
        case ID_BTN_OK: {
            wchar_t buf[64]{};
            GetDlgItemTextW(h, IDC_EDIT_TITLE_FONT, buf, 64); g_styleConfig.titleFontSize = (float)_wtof(buf);
            GetDlgItemTextW(h, IDC_EDIT_CARD_FONT, buf, 64); g_styleConfig.cardFontSize = (float)_wtof(buf);
            GetDlgItemTextW(h, IDC_EDIT_BG_ALPHA, buf, 64); g_styleConfig.bgAlpha = _wtoi(buf);
            g_styleConfig.bgColor = GetColorFromEdit(h, IDC_EDIT_BG_COLOR);
            g_styleConfig.activeTextColor = GetColorFromEdit(h, IDC_EDIT_ACTIVE_COLOR);
            g_styleConfig.normalTextColor = GetColorFromEdit(h, IDC_EDIT_NORMAL_COLOR);
            if (g_styleConfig.bgAlpha < 0) g_styleConfig.bgAlpha = 0;
            if (g_styleConfig.bgAlpha > 255) g_styleConfig.bgAlpha = 255;
            SaveStyleConfig();
            LoadStyleConfig();
            if (g_hAboutWnd) SetLayeredWindowAttributes(g_hAboutWnd, 0, (BYTE)g_styleConfig.bgAlpha, LWA_ALPHA);
            if (g_hSettingsWnd) SetLayeredWindowAttributes(g_hSettingsWnd, 0, (BYTE)g_styleConfig.bgAlpha, LWA_ALPHA);
            if (g_visible) RenderWithLayeredWindow();
            DestroyWindow(h);
            return 0;
        }
        case ID_BTN_CANCEL:
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        g_hSettingsWnd = NULL;
        return 0;
    }
    return DefWindowProc(h, msg, w, l);
}

void RenderOnce() { RenderWithLayeredWindow(); }

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
        if (!has) { g_visible = false; ShowWindow(g_hFloatWnd, SW_HIDE); return CallNextHookEx(g_hHook, n, wp, lp); }
        if (!g_visible || stateChanged) {
            if (!g_visible) {
                g_visible = true;
                WCHAR app[128] = L"";
                bool pureWinPressed = (g_keyWin && !g_keyCtrl && !g_keyAlt && !g_keyShift);
                if (!pureWinPressed) {
                    HWND hwnd = GetForegroundWindow();
                    DWORD pid = 0;
                    GetWindowThreadProcessId(hwnd, &pid);
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                    if (hProc) {
                        WCHAR path[256] = L"";
                        GetModuleFileNameExW(hProc, NULL, path, 256);
                        WCHAR* name = wcsrchr(path, L'\\');
                        if (name) { name++; wcscpy_s(app, name); }
                        CloseHandle(hProc);
                    }
                }
                LoadConfigForApp(app);
                int sw = GetSystemMetrics(SM_CXSCREEN);
                ComputeBubbleSize(g_windowWidth, g_windowHeight);
                RepositionBubble();
                ShowWindow(g_hFloatWnd, SW_SHOWNOACTIVATE);
            }
            else if (stateChanged) {
                RepositionBubble();
            }
            RenderOnce();
        }
    }
    return CallNextHookEx(g_hHook, n, wp, lp);
}

int WINAPI wWinMain(HINSTANCE ins, HINSTANCE prev, LPWSTR cmd, int nShow) {
    if (!CheckSingleInstance()) return 0;
    g_hInst = ins;
    LoadStyleConfig();
    GdiplusStartupInput gdiIn;
    ULONG_PTR token;
    GdiplusStartup(&token, &gdiIn, NULL);
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = ins;
    wc.lpszClassName = L"MainWnd";
    HICON hMainIcon = LoadIcon(ins, MAKEINTRESOURCE(102));
    if (hMainIcon == NULL) hMainIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIcon = hMainIcon; wc.hIconSm = hMainIcon;
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
    g_hFloatWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, L"FloatUI", L"", WS_POPUP, 0, 0, sw, 200, NULL, NULL, ins, NULL);
    ShowWindow(g_hFloatWnd, SW_HIDE);
    SaveLastKeyState();
    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandle(NULL), 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    if (g_hHook) UnhookWindowsHookEx(g_hHook);
    if (g_hMutex) { ReleaseMutex(g_hMutex); CloseHandle(g_hMutex); }
    if (hMainIcon) DestroyIcon(hMainIcon);
    if (g_TrayData.hIcon) DestroyIcon(g_TrayData.hIcon);
    GdiplusShutdown(token);
    return 0;
}

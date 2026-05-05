#include <Windows.h>
#include <gdiplus.h>
#include <psapi.h>
#include <fstream>
#include <string>
#include <vector>
#include "resource.h"

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

// 修改：修复高度计算函数，正确处理多行布局
int ComputeWindowHeight(int maxWidth) {
	if (g_currentConfig.keys.empty()) {
		return 35 + CARD_HEIGHT + 20; // 只有标题栏的高度
	}

	HDC hdc = GetDC(NULL);
	Graphics* g = new Graphics(hdc);
	Font cardFont(L"Microsoft YaHei", 8.0f);

	int x = 20, y = 35;
	int maxY = y; // 记录最大 Y 坐标

	for (auto& item : g_currentConfig.keys) {
		// 构建完整的显示文本
		wstring fullTxt;
		for (int i = 0; i < item.keys.size(); i++) {
			if (i > 0) fullTxt += L" + ";
			fullTxt += item.keys[i];
		}
		fullTxt += L"：" + item.desc;

		// 测量文本宽度
		RectF textBounds;
		g->MeasureString(fullTxt.c_str(), (int)fullTxt.size(), &cardFont, PointF(0, 0), &textBounds);
		int cardWidth = (int)(textBounds.Width + CARD_PADDING * 2 + 4);

		// 判断是否需要换行
		if (x + cardWidth + CARD_MARGIN > maxWidth) {
			x = 20;
			y += CARD_HEIGHT + LINE_SPACING;
		}

		// 更新最大 Y 坐标
		if (y + CARD_HEIGHT > maxY) {
			maxY = y + CARD_HEIGHT;
		}

		x += cardWidth + CARD_MARGIN;
	}

	delete g;
	ReleaseDC(NULL, hdc);

	// 返回总高度（底部留20像素边距）
	return maxY + 20;
}

void InitTrayIcon() {
	ZeroMemory(&g_TrayData, sizeof(g_TrayData));
	g_TrayData.cbSize = sizeof(NOTIFYICONDATAW);
	g_TrayData.hWnd = g_hMainWnd;
	g_TrayData.uID = 1001;
	g_TrayData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_SHOWTIP;
	g_TrayData.uCallbackMessage = WM_TRAY_MSG;
	g_TrayData.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	g_TrayData.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_TRAY_ICON));
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

	Color bgColor(200, 0, 0, 0);
	SolidBrush bgBrush(bgColor);
	g.FillRectangle(&bgBrush, (REAL)0, (REAL)0, (REAL)g_windowWidth, (REAL)g_windowHeight);

	Font titleFont(L"Microsoft YaHei", 10.0f);
	Font cardFont(L"Microsoft YaHei", 8.0f);
	SolidBrush white(Color(255, 255, 255));
	SolidBrush red(Color(255, 60, 60));
	Pen borderPen(Color(120, 255, 255, 255), 1.0f);

	wstring title = g_currentConfig.name + L" (" + g_currentConfig.description + L")";
	g.DrawString(title.c_str(), (int)title.size(), &titleFont, PointF(20, 10), &white);

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
				g.DrawString(sp.c_str(), (int)sp.size(), &cardFont, PointF(tx + ox, ty), &white);
				RectF r; g.MeasureString(sp.c_str(), (int)sp.size(), &cardFont, PointF(0, 0), &r);
				ox += r.Width;
			}
			wstring k = item.keys[i];
			SolidBrush* br = IsKeyActive(k) ? &red : &white;
			g.DrawString(k.c_str(), (int)k.size(), &cardFont, PointF(tx + ox, ty), br);
			RectF kr; g.MeasureString(k.c_str(), (int)k.size(), &cardFont, PointF(0, 0), &kr);
			ox += kr.Width;
		}
		g.DrawString(item.desc.c_str(), (int)item.desc.size(), &cardFont, PointF(tx + ox, ty), &white);
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
	GdiplusStartupInput gdiIn;
	ULONG_PTR token;
	GdiplusStartup(&token, &gdiIn, NULL);

	WNDCLASSEXW wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.lpfnWndProc = MainWndProc;
	wc.hInstance = ins;
	wc.lpszClassName = L"MainWnd";
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

	GdiplusShutdown(token);
	return 0;
}
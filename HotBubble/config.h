// config.h
#pragma once
#include <windows.h>

extern int      g_nTitleSize;
extern int      g_nLabelSize;
extern int      g_nBgAlpha;
extern bool     g_bShowWindows;
extern bool     g_bShowProcess;
extern COLORREF g_crTitle;
extern COLORREF g_crLabel;
extern COLORREF g_crBg;
extern COLORREF g_crMark;

// 函数声明
void LoadConfig();
void SaveConfig();
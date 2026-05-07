// config.h
#pragma once
#include <windows.h>

// 全局配置变量声明
extern int      g_nTitleSize;
extern int      g_nLabelSize;
extern int      g_nBgAlpha;
extern COLORREF g_crTitle;
extern COLORREF g_crLabel;
extern COLORREF g_crBg;
extern COLORREF g_crMark;

// 函数声明
void LoadConfig();
void SaveConfig();
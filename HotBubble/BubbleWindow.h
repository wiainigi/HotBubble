#pragma once
#include <windows.h>

// 初始化全局键盘钩子（应在 WinMain 开始时调用）
void InitBubbleWindowHook();

// 卸载全局键盘钩子（应在程序退出前调用）
void UninitBubbleWindowHook();
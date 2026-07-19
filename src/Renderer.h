#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

constexpr COLORREF kBarBlue     = RGB(0x4B, 0x8B, 0xF5);
constexpr COLORREF kBarPurple   = RGB(0x8B, 0x5C, 0xF6);
constexpr COLORREF kBarOrange   = RGB(0xFF, 0x95, 0x00);
constexpr COLORREF kBarRed      = RGB(0xFF, 0x3B, 0x30);
constexpr COLORREF kTrackDark   = RGB(0x3A, 0x3A, 0x3A);
constexpr COLORREF kTrackLight  = RGB(0xD0, 0xD0, 0xD0);
constexpr COLORREF kLabelDark   = RGB(0xAA, 0xAA, 0xAA);
constexpr COLORREF kLabelLight  = RGB(0x55, 0x55, 0x55);
constexpr COLORREF kPctDark     = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kPctLight    = RGB(0x00, 0x00, 0x00);

// Bar thickness as percent of row height, dual gap in px (DPI-96)
constexpr int kSingleBarPctH = 50;
constexpr int kDualBarPctH   = 32;
constexpr int kDualBarGapPx  = 2;

// When has_second is true the bar area splits into two thin stacked bars:
// top = pct (blue ramp), bottom = pct2 (purple ramp); the text shows the higher one.
void RenderUsageItem(
    HDC hdc, int x, int y, int w, int h,
    bool dark_mode,
    const wchar_t* label,
    double pct,
    bool has_data,
    bool refreshing,
    bool has_second = false,
    double pct2 = 0.0);

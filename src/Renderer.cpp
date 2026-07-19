#include "Renderer.h"
#include <cstdio>
#include <algorithm>

static COLORREF LerpColor(COLORREF a, COLORREF b, double t)
{
    t = (std::max)(0.0, (std::min)(1.0, t));
    return RGB(
        GetRValue(a) + static_cast<int>((GetRValue(b) - GetRValue(a)) * t),
        GetGValue(a) + static_cast<int>((GetGValue(b) - GetGValue(a)) * t),
        GetBValue(a) + static_cast<int>((GetBValue(b) - GetBValue(a)) * t));
}

static COLORREF BarColorFor(double pct, COLORREF base)
{
    if (pct <= 60.0) return base;
    if (pct <= 80.0) return LerpColor(base, kBarOrange, (pct - 60.0) / 20.0);
    return LerpColor(kBarOrange, kBarRed, (pct - 80.0) / 20.0);
}

static void FillBar(HDC hdc, int x, int y, int w, int h, double pct, COLORREF base)
{
    if (pct <= 0.0) return;
    int fillW = static_cast<int>(w * pct / 100.0);
    if (fillW < 1) fillW = 1;
    RECT fillRect = {x, y, x + fillW, y + h};
    HBRUSH fillBrush = CreateSolidBrush(BarColorFor(pct, base));
    FillRect(hdc, &fillRect, fillBrush);
    DeleteObject(fillBrush);
}

void RenderUsageItem(
    HDC hdc, int x, int y, int w, int h,
    bool dark_mode,
    const wchar_t* label,
    double pct,
    bool has_data,
    bool refreshing,
    bool has_second,
    double pct2)
{
    double displayPct = (has_second && pct2 > pct) ? pct2 : pct;
    RECT itemRect = {x, y, x + w, y + h};
    ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &itemRect, nullptr, 0, nullptr);

    SetBkMode(hdc, TRANSPARENT);

    COLORREF labelColor = dark_mode ? kLabelDark : kLabelLight;
    COLORREF pctColor   = dark_mode ? kPctDark   : kPctLight;
    COLORREF trackColor = dark_mode ? kTrackDark  : kTrackLight;

    bool hasLabel = label && label[0] != L'\0';

    SIZE labelSize = {};
    if (hasLabel)
        GetTextExtentPoint32W(hdc, label, static_cast<int>(wcslen(label)), &labelSize);

    wchar_t pctText[16];
    if (refreshing)
        swprintf_s(pctText, L"...");
    else if (has_data)
        swprintf_s(pctText, L"%.0f%%", displayPct);
    else
        swprintf_s(pctText, L"--");

    SIZE pctSize;
    GetTextExtentPoint32W(hdc, pctText, static_cast<int>(wcslen(pctText)), &pctSize);

    SIZE maxPctSize;
    GetTextExtentPoint32W(hdc, L"100%", 4, &maxPctSize);

    int labelGap = 3;
    int barPctGap = 6;
    int barX = hasLabel ? x + labelSize.cx + labelGap : x;
    int pctAreaX = x + w - maxPctSize.cx;
    int pctX = pctAreaX + (maxPctSize.cx - pctSize.cx);
    int barW = pctAreaX - barX - barPctGap;

    if (barW < 10) barW = 10;

    if (hasLabel) {
        SetTextColor(hdc, labelColor);
        RECT labelRect = {x, y, x + labelSize.cx, y + h};
        DrawTextW(hdc, label, -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    HBRUSH trackBrush = CreateSolidBrush(trackColor);

    if (has_second) {
        int subH = h * kDualBarPctH / 100;
        if (subH < 2) subH = 2;
        int gap = kDualBarGapPx;
        int totalH = subH * 2 + gap;
        int topY = y + (h - totalH) / 2;
        int botY = topY + subH + gap;

        RECT topTrack = {barX, topY, barX + barW, topY + subH};
        RECT botTrack = {barX, botY, barX + barW, botY + subH};
        FillRect(hdc, &topTrack, trackBrush);
        FillRect(hdc, &botTrack, trackBrush);
        DeleteObject(trackBrush);

        if (has_data) {
            FillBar(hdc, barX, topY, barW, subH, pct, kBarBlue);
            FillBar(hdc, barX, botY, barW, subH, pct2, kBarPurple);
        }
    } else {
        int barH = h * kSingleBarPctH / 100;
        if (barH < 3) barH = 3;
        int barY = y + (h - barH) / 2;

        RECT trackRect = {barX, barY, barX + barW, barY + barH};
        FillRect(hdc, &trackRect, trackBrush);
        DeleteObject(trackBrush);

        if (has_data)
            FillBar(hdc, barX, barY, barW, barH, pct, kBarBlue);
    }

    SetTextColor(hdc, pctColor);
    RECT pctRect = {pctX, y, pctX + pctSize.cx, y + h};
    DrawTextW(hdc, pctText, -1, &pctRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

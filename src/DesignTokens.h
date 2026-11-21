// DesignTokens.h — UI design values
//
// Every visual property of the settings window comes from here: colors, corner radii, spacing,
// fonts and control sizes. The UI code never hard-codes a color or a size.
// To restyle the UI, edit the values in this file. It has no dependencies other than GDI+
// and can be reused in other projects.
//
// The default theme is plain and neutral: a single accent color and soft gray surfaces.
#pragma once
#include <windows.h>
#include <algorithm>
using std::min;  // GDI+ headers need min/max (for projects built with NOMINMAX)
using std::max;
#include <objidl.h>
#include <gdiplus.h>

namespace design {

struct Palette {
    Gdiplus::Color background;    // window background
    Gdiplus::Color sidebar;       // category list background
    Gdiplus::Color surface;       // settings group cards
    Gdiplus::Color surfaceHover;  // hovered row / button
    Gdiplus::Color border;        // thin lines, card outlines
    Gdiplus::Color divider;       // row separators inside cards
    Gdiplus::Color text;          // primary text
    Gdiplus::Color textMuted;     // descriptions
    Gdiplus::Color textDisabled;  // disabled items
    Gdiplus::Color accent;        // toggles that are on, selected options, slider fill
    Gdiplus::Color accentHover;
    Gdiplus::Color onAccent;      // text on the accent color
    Gdiplus::Color track;         // toggles that are off, slider track
    Gdiplus::Color knob;          // toggle and slider knobs
    Gdiplus::Color selection;     // selected category
    Gdiplus::Color danger;        // destructive actions (e.g. Quit)
};

struct Metrics {  // values at 96 DPI in pixels; the code scales them for the current DPI
    float windowWidth = 760, windowHeight = 580;
    float sidebarWidth = 200;
    float spaceXS = 4, spaceS = 8, spaceM = 12, spaceL = 16, spaceXL = 24, spaceXXL = 32;
    float radiusS = 4, radiusM = 8, radiusL = 12, radiusPill = 999;
    float rowHeight = 52, rowHeightWithDesc = 62, sidebarItemHeight = 36;
    float toggleWidth = 40, toggleHeight = 20, toggleKnob = 14;
    float sliderWidth = 180, sliderTrack = 4, sliderKnob = 16;
    float segmentHeight = 28, segmentMinItem = 64;
    float buttonHeight = 30;
    float borderWidth = 1;
};

struct Typography {
    const wchar_t* family = L"Segoe UI";
    float title = 22;      // page title
    float heading = 13;    // group heading
    float body = 14;       // setting name
    float caption = 12;    // description
    float sidebar = 14;    // category names
    bool titleBold = true, headingBold = true;
};

inline Palette Dark() {
    using C = Gdiplus::Color;
    return Palette{
        C(255, 32, 32, 36),    // background
        C(255, 26, 26, 30),    // sidebar
        C(255, 43, 43, 48),    // surface
        C(255, 52, 52, 58),    // surfaceHover
        C(255, 60, 60, 66),    // border
        C(255, 54, 54, 60),    // divider
        C(255, 240, 240, 242), // text
        C(255, 160, 160, 168), // textMuted
        C(255, 100, 100, 108), // textDisabled
        C(255, 10, 132, 255),  // accent
        C(255, 64, 156, 255),  // accentHover
        C(255, 255, 255, 255), // onAccent
        C(255, 72, 72, 80),    // track
        C(255, 255, 255, 255), // knob
        C(255, 48, 48, 56),    // selection
        C(255, 255, 99, 89),   // danger
    };
}
inline Palette Light() {
    using C = Gdiplus::Color;
    return Palette{
        C(255, 245, 245, 247), // background
        C(255, 236, 236, 240), // sidebar
        C(255, 255, 255, 255), // surface
        C(255, 242, 242, 246), // surfaceHover
        C(255, 218, 218, 224), // border
        C(255, 232, 232, 236), // divider
        C(255, 28, 28, 30),    // text
        C(255, 110, 110, 118), // textMuted
        C(255, 170, 170, 178), // textDisabled
        C(255, 0, 113, 227),   // accent
        C(255, 0, 98, 204),    // accentHover
        C(255, 255, 255, 255), // onAccent
        C(255, 214, 214, 220), // track
        C(255, 255, 255, 255), // knob
        C(255, 220, 220, 228), // selection
        C(255, 215, 58, 49),   // danger
    };
}

struct Tokens {
    Palette color;
    Metrics size;
    Typography type;
};
inline Tokens Make(bool light) { return Tokens{light ? Light() : Dark(), Metrics{}, Typography{}}; }

}  // namespace design

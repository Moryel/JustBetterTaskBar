// DesignTokens.h — arayüz tasarım değerleri
//
// Ayarlar penceresinin görünüşü yalnızca buradan gelir: renkler, köşe yarıçapları, boşluklar,
// yazı tipleri ve kontrol ölçüleri. Arayüz kodunda renk ya da ölçü sabit yazılmaz.
// Tasarımı değiştirmek için bu dosyadaki değerleri düzenlemek yeterlidir. Dosyanın GDI+ dışında
// bağımlılığı yoktur, başka projelerde de kullanılabilir.
//
// Varsayılan tema sade ve nötrdür: tek vurgu rengi, yumuşak gri yüzeyler.
#pragma once
#include <windows.h>
#include <algorithm>
using std::min;  // GDI+ başlıkları min/max ister (NOMINMAX ile derlenen projelerde)
using std::max;
#include <objidl.h>
#include <gdiplus.h>

namespace design {

struct Palette {
    Gdiplus::Color background;    // pencere zemini
    Gdiplus::Color sidebar;       // sol kategori listesi zemini
    Gdiplus::Color surface;       // ayar gruplarının kart zemini
    Gdiplus::Color surfaceHover;  // üzerine gelinen satır / düğme
    Gdiplus::Color border;        // ince çizgiler, kart kenarı
    Gdiplus::Color divider;       // kart içindeki satır ayırıcıları
    Gdiplus::Color text;          // ana yazı
    Gdiplus::Color textMuted;     // açıklama yazısı
    Gdiplus::Color textDisabled;  // pasif öğe
    Gdiplus::Color accent;        // vurgu (açık anahtar, seçili düğme, kaydırıcı dolgusu)
    Gdiplus::Color accentHover;
    Gdiplus::Color onAccent;      // vurgu rengi üzerindeki yazı
    Gdiplus::Color track;         // kapalı anahtar / kaydırıcı rayı
    Gdiplus::Color knob;          // anahtar ve kaydırıcı topuzu
    Gdiplus::Color selection;     // seçili kategori arka planı
    Gdiplus::Color danger;        // tehlikeli eylem (ör. Dock'tan çık)
};

struct Metrics {  // 96 DPI'daki değerler (piksel); kod DPI'ya göre ölçekler
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
    const wchar_t* family = L"Segoe UI";  // ileride kendi yazı tipin
    float title = 22;      // sayfa başlığı
    float heading = 13;    // grup başlığı
    float body = 14;       // ayar adı
    float caption = 12;    // açıklama
    float sidebar = 14;    // kategori adları
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

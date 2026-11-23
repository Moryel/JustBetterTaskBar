// Just Better TaskBar — a lightweight macOS-style dock and menu bar for Windows (Win32 + GDI+, single .exe)
//
// Features: magnification, pinned + running apps, running indicators,
// launch bounce, tooltips, context menus, drag-and-drop pinning,
// drag to reorder / drag out to remove, bottom / left / right position,
// auto-hide, folder stacks, spacers, lock, Recycle Bin,
// Start (Launchpad) button, hiding the Windows taskbar.

#ifndef UNICODE
#define UNICODE
#endif
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <commoncontrols.h>
#include <dwmapi.h>
#include <appmodel.h>
#include <propsys.h>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <cstdio>
#include <functional>
#include "DesignTokens.h"
#include <iphlpapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <powrprof.h>
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <windows.graphics.effects.interop.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "CoreMessaging.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace Gdiplus;

static const GUID kAppsFolder = {0x1e87508d, 0x89c2, 0x42f0, {0x8a, 0x7e, 0x64, 0x5a, 0x0f, 0x50, 0xca, 0x58}};
static const GUID kDownloads = {0x374de290, 0x123f, 0x4565, {0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b}};
static const PROPERTYKEY kPKeyAumid = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

// ---------------------------------------------------------------- data
struct Settings {
    int size = 50;
    int magnify = 85;
    int position = 0;
    bool autohide = false;
    int hideDelay = 200;
    int hideAnim = 300;
    bool hideTaskbar = true;
    bool reserve = true;
    bool indicators = true;
    int indicatorStyle = 0;
    bool bounce = true;
    int minimizedStyle = 1;
    bool minimizedPreviews = true;
    bool locked = false;
    bool glass = true;
    int glassSaturation = 180;
    int glassStyle = 0;
    int theme = 0;
    int opacity = 45;
    bool tooltips = true;
    bool attention = true;
    bool clickMinimize = true;
    bool hideFullscreen = true;
    bool showLauncher = true, showRunning = true, showTrash = true;
    bool separateApps = false;
    bool showMenubar = true, clockDate = true, clockSeconds = false;
    bool trayIcons = true;
} S;

enum Kind { K_LAUNCHER, K_APP, K_SEP, K_TRASH, K_SPACER, K_STACK, K_MINWIN };

struct Pin { std::wstring launch, key, name; bool stack = false, spacer = false; };

struct Item {
    Kind kind = K_APP;
    std::wstring key, launch, name;
    bool pinned = false, ghost = false;
    std::vector<HWND> wins;
    Bitmap* icon = nullptr;
    float x = 0, w = 0, size = 0;
};

struct WinId { DWORD pid = 0; std::wstring key, launch, path, aumid; };
struct Group { std::wstring key, launch, path, aumid; std::vector<HWND> wins; HWND first = nullptr; };
struct Bounce { ULONGLONG start; int count; };
struct Geo { float base, maxZ, gap, padT, padB, margin, padX, sepW, tipRoom, bounceH, radius, shelf; };

struct Surface {
    HDC dc = nullptr;
    HBITMAP dib = nullptr;
    void* bits = nullptr;
    Bitmap* bmp = nullptr;
    int w = 0, h = 0;
    void Ensure(int W, int H) {
        W = std::max(W, 1);
        H = std::max(H, 1);
        if (!dc) dc = CreateCompatibleDC(nullptr);
        if (W == w && H == h && bmp) return;
        delete bmp;
        bmp = nullptr;
        if (dib) DeleteObject(dib);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        SelectObject(dc, dib);
        bmp = new Bitmap(W, H, W * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        w = W;
        h = H;
    }
    void Clear() { memset(bits, 0, (size_t)w * h * 4); }
    void Present(HWND hw, int x, int y) {
        POINT dst{x, y}, src{0, 0};
        SIZE sz{w, h};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hw, nullptr, &dst, &sz, dc, &src, 0, &bf, ULW_ALPHA);
    }
    void Free() {
        delete bmp;
        bmp = nullptr;
        if (dib) DeleteObject(dib);
        dib = nullptr;
        if (dc) DeleteDC(dc);
        dc = nullptr;
    }
};

enum { T_ANIM = 1, T_REFRESH = 2, T_POLL = 3, T_SIZE = 4, T_EDGE = 5, T_DRAG = 6 };
enum {
    ID_QUIT = 1, ID_HIDETB, ID_RESERVE, ID_AUTOSTART, ID_EDITINI, ID_RELOAD,
    ID_SIZE0 = 10, ID_MAG0 = 20, ID_EMPTYTRASH = 30, ID_OPENTRASH, ID_POS0 = 40,
    ID_AUTOHIDE = 50, ID_DELAY0 = 51, ID_ANIM0 = 55, ID_IND = 60, ID_BOUNCE, ID_DIM, ID_SPACER, ID_LOCK,
    ID_REMOVE, ID_STACKOPEN, ID_FOLDEROPEN,
    ID_GLASS = 70, ID_THEME0 = 71, ID_OPAC0 = 74, ID_TIPS = 80, ID_ATTN, ID_CLICKMIN, ID_FULLHIDE,
    ID_SHOWLAUNCH, ID_SHOWRUN, ID_SHOWTRASH, ID_DOWNLOADS, ID_BAR = 95, ID_BARDATE, ID_BARSEC, ID_BARTRAY, ID_OPENSETTINGS,
    ID_OPEN = 100, ID_PIN, ID_CLOSEALL, ID_INDSTYLE0 = 120, ID_MINSTYLE0 = 125, ID_MINPREV = 128, ID_SEPAPPS = 129, ID_HIDEAPP = 130, ID_GLASSSTYLE0 = 131, ID_WIN0 = 200, ID_FILE0 = 300
};
static const int kSizes[4] = {40, 50, 64, 80};
static const wchar_t* kSizeNames[4] = {L"Small", L"Medium", L"Large", L"Extra large"};
static const int kMags[6] = {0, 30, 60, 85, 110, 140};
static const wchar_t* kMagNames[6] = {L"Off", L"Very low", L"Low", L"Medium", L"High", L"Maximum"};
static const wchar_t* kPosNames[3] = {L"Bottom", L"Left", L"Right"};
static const int kDelays[4] = {0, 200, 500, 1000};
static const wchar_t* kDelayNames[4] = {L"No delay", L"Short (0.2 s)", L"Medium (0.5 s)", L"Long (1 s)"};
static const int kAnims[4] = {0, 150, 300, 600};
static const wchar_t* kAnimNames[4] = {L"No animation", L"Fast", L"Normal", L"Slow"};
static const wchar_t* kThemeNames[3] = {L"Dark", L"Light", L"Match system theme"};
static const int kOpacs[4] = {25, 45, 65, 85};
static const wchar_t* kOpacNames[4] = {L"Very transparent", L"Transparent", L"Slightly transparent", L"Almost opaque"};
static const float kPeriod = 0.6f;
const UINT WM_APPBAR = WM_APP + 1;
const UINT WM_LIQUIDACCESS = WM_APP + 6;

namespace wuc = winrt::Windows::UI::Composition;
#include "LiquidGlass.h"
struct Glass {
    HWND wnd = nullptr;
    RECT rc{};
    bool comp = false;
    wuc::Desktop::DesktopWindowTarget target{nullptr};
    wuc::SpriteVisual visual{nullptr};
    wuc::CompositionRoundedRectangleGeometry geo{nullptr};
};
Glass g_glass, g_barGlass;
HWND g_hwnd = nullptr, g_dragWnd = nullptr, g_barWnd = nullptr;
bool g_barAppbar = false;
RECT g_barRect{};
int g_barX = 0, g_barY = 0;
Font *g_barFont = nullptr, *g_barBold = nullptr, *g_iconFont = nullptr;
const UINT WM_BARAPPBAR = WM_APP + 2;
std::wstring g_clockText;
std::vector<HWND> g_minOrder;
std::map<HWND, HTHUMBNAIL> g_thumbs;
HWND g_trayWnd = nullptr;
ULONGLONG g_ignoreTbCreatedUntil = 0;
static HWND ExplorerTray() {
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, L"Shell_TrayWnd", nullptr)))
        if (h != g_trayWnd) return h;
    return nullptr;
}
static void RenderBar();
static void BarPoll();
static void BarTrackForeground();
static std::wstring ClockText();
static void TrayEnable(bool on);
static void DockCommand(int cmd);
static void OpenSettings();
static void SettingsChanged();
bool g_sysLight = false;
Surface g_dock, g_dragSurf;
int g_W = 0, g_H = 0, g_X = 0, g_Y = 0;
float g_scale = 1.0f;
std::vector<Pin> g_pins;
std::vector<Item> g_items;
std::vector<std::wstring> g_runOrder;
std::vector<std::wstring> g_appOrder;
std::vector<std::wstring> g_hiddenApps;
static bool IsHiddenApp(const std::wstring& key) {
    return std::find(g_hiddenApps.begin(), g_hiddenApps.end(), key) != g_hiddenApps.end();
}
std::map<std::wstring, Bitmap*> g_icons;
std::map<std::wstring, std::wstring> g_names;
std::map<HWND, WinId> g_winCache;
std::map<std::wstring, Bounce> g_bounce;
float g_hover = 0, g_hoverTarget = 0, g_mx = -1;
float g_show = 1, g_showTarget = 1;
bool g_tracking = false, g_animOn = false, g_hiddenFS = false, g_trashFull = false;
int g_pressIdx = -1, g_pollCount = 0, g_spacerSeq = 0;
ULONGLONG g_lastTick = 0, g_edgeSince = 0, g_leaveSince = 0;
UINT g_shellMsg = 0, g_taskbarCreatedMsg = 0;
bool g_appbar = false, g_cleaned = false, g_menuOpen = false;
RECT g_abRect{}, g_shelfScreen{};
std::wstring g_sig, g_iniPath, g_exePath;
Font* g_font = nullptr;
bool g_down = false, g_resizing = false, g_dragging = false, g_dragRemove = false;
int g_resizeStartSize = 0;
POINT g_resizeStart{}, g_pressPt{};
std::wstring g_pressKey, g_dragKey;
Bitmap* g_dragIcon = nullptr;

// ---------------------------------------------------------------- helpers
static std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}
static bool EndsWith(const std::wstring& s, const wchar_t* suf) {
    size_t n = wcslen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}
static std::wstring Hex(HWND h) {
    wchar_t b[40];
    swprintf(b, 40, L"hwnd:%p", (void*)h);
    return b;
}
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n\xFEFF");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::wstring FileStem(const std::wstring& p) {
    std::wstring s = PathFindFileNameW(p.c_str());
    size_t d = s.rfind(L'.');
    if (d != std::wstring::npos) s.resize(d);
    return s;
}
static bool IsDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static MONITORINFO PrimaryMon() {
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi;
}
static void RoundRectPath(GraphicsPath& p, RectF r, float rad) {
    rad = std::min(rad, std::min(r.Width, r.Height) / 2);
    float d = rad * 2;
    if (d <= 0.5f) { p.AddRectangle(r); return; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}
static bool Horizontal() { return S.position == 0; }

static Geo G() {
    float s = g_scale;
    Geo g;
    g.base = S.size * s;
    g.maxZ = g.base * (1.0f + S.magnify / 100.0f);
    g.gap = std::round(6 * s);
    g.padT = 6 * s;
    g.padB = 10 * s;
    g.margin = 6 * s;
    g.padX = 5 * s;
    g.sepW = 18 * s;
    g.tipRoom = Horizontal() ? 44 * s : 240 * s;
    g.bounceH = g.base * 0.55f;
    g.radius = 18 * s;
    g.shelf = g.padB + g.base + g.padT;
    return g;
}
static int IconPx() { return (int)std::ceil(G().maxZ); }

static RectF Rc(float m0, float m1, float c0, float c1) {
    switch (S.position) {
    case 0: return RectF(m0, g_H - c1, m1 - m0, c1 - c0);
    case 1: return RectF(c0, m0, c1 - c0, m1 - m0);
    default: return RectF(g_W - c1, m0, c1 - c0, m1 - m0);
    }
}
static PointF Pt(float m, float c) {
    switch (S.position) {
    case 0: return PointF(m, g_H - c);
    case 1: return PointF(c, m);
    default: return PointF(g_W - c, m);
    }
}
static float MainOf(LPARAM l) { return Horizontal() ? (float)GET_X_LPARAM(l) : (float)GET_Y_LPARAM(l); }

// ---------------------------------------------------------------- process / app info
static std::wstring ProcPath(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t buf[MAX_PATH * 2];
    DWORD n = ARRAYSIZE(buf);
    std::wstring r;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) r.assign(buf, n);
    CloseHandle(h);
    return r;
}
static std::wstring ProcAumid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    std::wstring r;
    UINT32 len = 0;
    if (GetApplicationUserModelId(h, &len, nullptr) == ERROR_INSUFFICIENT_BUFFER && len) {
        std::vector<wchar_t> b(len);
        if (GetApplicationUserModelId(h, &len, b.data()) == ERROR_SUCCESS) r = b.data();
    }
    CloseHandle(h);
    return r;
}
static std::wstring WindowAumid(HWND h) {
    IPropertyStore* ps = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(h, IID_PPV_ARGS(&ps)))) return L"";
    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::wstring r;
    if (SUCCEEDED(ps->GetValue(kPKeyAumid, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) r = pv.pwszVal;
    PropVariantClear(&pv);
    ps->Release();
    return r;
}
static std::wstring FileDesc(const std::wstring& path) {
    DWORD dummy = 0, sz = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (sz) {
        std::vector<BYTE> d(sz);
        if (GetFileVersionInfoW(path.c_str(), 0, sz, d.data())) {
            struct LANG { WORD l, c; }* tr = nullptr;
            UINT len = 0;
            if (VerQueryValueW(d.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &len) && len >= sizeof(LANG)) {
                wchar_t q[100];
                swprintf(q, 100, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].l, tr[0].c);
                wchar_t* v = nullptr;
                UINT vl = 0;
                if (VerQueryValueW(d.data(), q, (void**)&v, &vl) && vl > 1 && v && v[0]) return Trim(v);
            }
        }
    }
    return FileStem(path);
}
static bool ResolveLnk(const std::wstring& lnk, std::wstring& target) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    bool ok = false;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) {
        if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
            wchar_t p[MAX_PATH] = {};
            if (SUCCEEDED(sl->GetPath(p, MAX_PATH, nullptr, 0)) && p[0]) { target = p; ok = true; }
        }
        pf->Release();
    }
    sl->Release();
    return ok;
}
static std::wstring AumidFromLaunch(const std::wstring& l) {
    static const std::wstring pre = L"shell:appsfolder\\";
    if (l.size() > pre.size() && Lower(l.substr(0, pre.size())) == pre) return l.substr(pre.size());
    return L"";
}
static IShellItem* AppItem(const std::wstring& aumid) {
    IShellItem* it = nullptr;
    if (FAILED(SHCreateItemInKnownFolder(kAppsFolder, KF_FLAG_DONT_VERIFY, aumid.c_str(), IID_PPV_ARGS(&it)))) return nullptr;
    return it;
}
static std::wstring AppName(const std::wstring& aumid) {
    std::wstring r;
    if (IShellItem* it = AppItem(aumid)) {
        PWSTR p = nullptr;
        if (SUCCEEDED(it->GetDisplayName(SIGDN_NORMALDISPLAY, &p)) && p) { r = p; CoTaskMemFree(p); }
        it->Release();
    }
    return r.empty() ? aumid : r;
}

// ---------------------------------------------------------------- icons
static Bitmap* PixelsToBitmap(std::vector<DWORD>& px, int w, int h, PixelFormat fmt) {
    Bitmap* b = new Bitmap(w, h, fmt);
    BitmapData bd;
    Rect r(0, 0, w, h);
    if (b->LockBits(&r, ImageLockModeWrite, fmt, &bd) == Ok) {
        for (int y = 0; y < h; y++) memcpy((BYTE*)bd.Scan0 + y * bd.Stride, &px[(size_t)y * w], (size_t)w * 4);
        b->UnlockBits(&bd);
    }
    return b;
}
static bool ReadDib(HBITMAP hb, int w, int h, std::vector<DWORD>& px) {
    px.assign((size_t)w * h, 0);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    int ok = GetDIBits(dc, hb, 0, h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return ok != 0;
}
static Bitmap* HIconToBitmap(HICON hi) {
    ICONINFO ii{};
    if (!GetIconInfo(hi, &ii)) return nullptr;
    Bitmap* out = nullptr;
    BITMAP bm{};
    if (ii.hbmColor && GetObject(ii.hbmColor, sizeof bm, &bm)) {
        int w = bm.bmWidth, h = bm.bmHeight;
        std::vector<DWORD> px, mk;
        if (ReadDib(ii.hbmColor, w, h, px)) {
            bool alpha = false;
            for (DWORD p : px) if (p >> 24) { alpha = true; break; }
            if (!alpha && ReadDib(ii.hbmMask, w, h, mk))
                for (size_t i = 0; i < px.size(); i++) px[i] = (px[i] & 0xFFFFFF) | ((mk[i] & 0xFFFFFF) ? 0 : 0xFF000000);
            out = PixelsToBitmap(px, w, h, PixelFormat32bppARGB);
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return out;
}
static Bitmap* HBitmapToBitmap(HBITMAP hb) {
    BITMAP bm{};
    if (!GetObject(hb, sizeof bm, &bm)) return nullptr;
    int w = bm.bmWidth, h = std::abs(bm.bmHeight);
    std::vector<DWORD> px;
    if (!ReadDib(hb, w, h, px)) return nullptr;
    bool alpha = false;
    for (DWORD p : px) if (p >> 24) { alpha = true; break; }
    if (!alpha) for (auto& p : px) p |= 0xFF000000;
    return PixelsToBitmap(px, w, h, PixelFormat32bppPARGB);
}
// For exes with only small icons the jumbo list puts a 32px icon in the corner of a 256px canvas; detect that.
static bool OnlyTopLeft(Bitmap* b) {
    int w = b->GetWidth(), h = b->GetHeight();
    if (w < 128) return false;
    BitmapData bd;
    Rect r(0, 0, w, h);
    if (b->LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) return false;
    bool empty = true;
    for (int y = 0; y < h && empty; y++) {
        DWORD* row = (DWORD*)((BYTE*)bd.Scan0 + y * bd.Stride);
        for (int x = (y < 64 ? 64 : 0); x < w; x++) if (row[x] >> 24) { empty = false; break; }
    }
    b->UnlockBits(&bd);
    return empty;
}
static Bitmap* IconFromSysIndex(int idx) {
    for (int list : {SHIL_JUMBO, SHIL_EXTRALARGE}) {
        IImageList* il = nullptr;
        if (FAILED(SHGetImageList(list, __uuidof(IImageList), (void**)&il)) || !il) continue;
        HICON hi = nullptr;
        il->GetIcon(idx, ILD_TRANSPARENT, &hi);
        il->Release();
        if (!hi) continue;
        Bitmap* b = HIconToBitmap(hi);
        DestroyIcon(hi);
        if (!b) continue;
        if (list == SHIL_JUMBO && OnlyTopLeft(b)) { delete b; continue; }
        return b;
    }
    return nullptr;
}
static Bitmap* PathIcon(const std::wstring& path) {
    SHFILEINFOW sfi{};
    if (!SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof sfi, SHGFI_SYSICONINDEX)) return nullptr;
    return IconFromSysIndex(sfi.iIcon);
}
static Bitmap* StockIcon(SHSTOCKICONID id) {
    SHSTOCKICONINFO sii{sizeof sii};
    if (FAILED(SHGetStockIconInfo(id, SHGSI_SYSICONINDEX, &sii))) return nullptr;
    return IconFromSysIndex(sii.iSysImageIndex);
}
static Bitmap* AppIcon(const std::wstring& aumid) {
    IShellItem* it = AppItem(aumid);
    if (!it) return nullptr;
    Bitmap* b = nullptr;
    IShellItemImageFactory* f = nullptr;
    if (SUCCEEDED(it->QueryInterface(IID_PPV_ARGS(&f)))) {
        HBITMAP hb = nullptr;
        if (SUCCEEDED(f->GetImage(SIZE{256, 256}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hb)) && hb) {
            b = HBitmapToBitmap(hb);
            DeleteObject(hb);
        }
        f->Release();
    }
    it->Release();
    return b;
}
static Bitmap* WindowIcon(HWND hw) {
    DWORD_PTR r = 0;
    SendMessageTimeoutW(hw, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &r);
    HICON hi = (HICON)r;
    if (!hi) hi = (HICON)GetClassLongPtrW(hw, GCLP_HICON);
    return hi ? HIconToBitmap(hi) : nullptr;
}
static Bitmap* ScaleTo(Bitmap* src, int sz) {
    Bitmap* b = new Bitmap(sz, sz, PixelFormat32bppPARGB);
    Graphics g(b);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    ImageAttributes ia;
    ia.SetWrapMode(WrapModeTileFlipXY);
    g.DrawImage(src, Rect(0, 0, sz, sz), 0, 0, (INT)src->GetWidth(), (INT)src->GetHeight(), UnitPixel, &ia);
    return b;
}
static Bitmap* MakeLauncherIcon(int sz) {
    Bitmap* b = new Bitmap(sz, sz, PixelFormat32bppPARGB);
    Graphics g(b);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    float m = sz * 0.07f;
    RectF r(m, m, sz - 2 * m, sz - 2 * m);
    GraphicsPath p;
    RoundRectPath(p, r, sz * 0.23f);
    LinearGradientBrush lg(PointF(0, m), PointF(0, sz - m), Color(255, 86, 90, 110), Color(255, 36, 38, 50));
    g.FillPath(&lg, &p);
    Pen pen(Color(70, 255, 255, 255), std::max(1.0f, sz / 70.f));
    g.DrawPath(&pen, &p);
    Color cols[9] = {Color(255, 255, 95, 87), Color(255, 255, 189, 46), Color(255, 40, 201, 64),
                     Color(255, 10, 132, 255), Color(255, 191, 90, 242), Color(255, 255, 55, 95),
                     Color(255, 100, 210, 255), Color(255, 255, 159, 10), Color(255, 172, 142, 104)};
    float inner = r.Width * 0.74f, cell = inner / 3, d = cell * 0.66f;
    float ox = r.X + (r.Width - inner) / 2, oy = r.Y + (r.Height - inner) / 2;
    for (int i = 0; i < 9; i++) {
        float cx = ox + cell * (i % 3 + 0.5f), cy = oy + cell * (i / 3 + 0.5f);
        GraphicsPath dp;
        RoundRectPath(dp, RectF(cx - d / 2, cy - d / 2, d, d), d * 0.3f);
        SolidBrush br(cols[i]);
        g.FillPath(&br, &dp);
    }
    return b;
}
static Bitmap* CacheIcon(const std::wstring& ck, Bitmap* raw) {
    Bitmap* sc = raw ? ScaleTo(raw, IconPx()) : nullptr;
    delete raw;
    g_icons[ck] = sc;
    return sc;
}
static Bitmap* IconFor(const std::wstring& launch, HWND hw) {
    std::wstring ck = launch.empty() ? Hex(hw) : Lower(launch);
    auto f = g_icons.find(ck);
    if (f != g_icons.end()) return f->second;
    Bitmap* raw = nullptr;
    std::wstring a = AumidFromLaunch(launch);
    if (!a.empty()) raw = AppIcon(a);
    else if (!launch.empty()) raw = PathIcon(launch);
    if (!raw && hw) raw = WindowIcon(hw);
    if (!raw) raw = StockIcon(SIID_APPLICATION);
    return CacheIcon(ck, raw);
}
static Bitmap* LauncherIcon() {
    auto f = g_icons.find(L"#launcher");
    if (f != g_icons.end()) return f->second;
    return g_icons[L"#launcher"] = MakeLauncherIcon(IconPx());
}
static Bitmap* TrashIcon() {
    std::wstring ck = g_trashFull ? L"#trash-full" : L"#trash-empty";
    auto f = g_icons.find(ck);
    if (f != g_icons.end()) return f->second;
    return CacheIcon(ck, StockIcon(g_trashFull ? SIID_RECYCLERFULL : SIID_RECYCLER));
}
static void ClearIcons() {
    for (auto& kv : g_icons) delete kv.second;
    g_icons.clear();
}
static void CreateFontObj() {
    delete g_font;
    g_font = new Font(L"Segoe UI", 13.0f * g_scale, FontStyleRegular, UnitPixel);
    delete g_barFont;
    delete g_barBold;
    delete g_iconFont;
    g_barFont = new Font(L"Segoe UI", 13.0f * g_scale, FontStyleRegular, UnitPixel);
    g_barBold = new Font(L"Segoe UI", 13.0f * g_scale, FontStyleBold, UnitPixel);
    FontFamily fluent(L"Segoe Fluent Icons");
    g_iconFont = new Font(fluent.IsAvailable() ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets", 15.0f * g_scale, FontStyleRegular, UnitPixel);
}
static HBITMAP SmallIconBitmap(const std::wstring& path) {
    SHFILEINFOW sfi{};
    if (!SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof sfi, SHGFI_ICON | SHGFI_SMALLICON) || !sfi.hIcon) return nullptr;
    int sz = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(g_hwnd));
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hb) {
        HGDIOBJ old = SelectObject(dc, hb);
        DrawIconEx(dc, 0, 0, sfi.hIcon, sz, sz, 0, nullptr, DI_NORMAL);
        SelectObject(dc, old);
    }
    DeleteDC(dc);
    DestroyIcon(sfi.hIcon);
    return hb;
}

// ---------------------------------------------------------------- pins + settings file
static std::wstring KeyForLaunch(const std::wstring& l) {
    std::wstring a = AumidFromLaunch(l);
    if (!a.empty()) return L"aumid:" + Lower(a);
    std::wstring ll = Lower(l);
    if (EndsWith(ll, L".lnk")) {
        std::wstring t;
        if (ResolveLnk(l, t)) return Lower(t);
    }
    return ll;
}
static std::wstring NameForLaunch(const std::wstring& l) {
    std::wstring a = AumidFromLaunch(l);
    if (!a.empty()) return AppName(a);
    if (IsDir(l)) {
        std::wstring n = PathFindFileNameW(l.c_str());
        return n.empty() ? l : n;
    }
    if (EndsWith(Lower(l), L".lnk")) return FileStem(l);
    return FileDesc(l);
}
static bool AddPin(const std::wstring& launch, std::wstring name, int at = -1) {
    if (launch.empty()) return false;
    Pin p;
    p.launch = launch;
    if (launch == L"#spacer") {
        p.spacer = true;
        p.key = L"#spacer:" + std::to_wstring(++g_spacerSeq);
    } else {
        p.key = KeyForLaunch(launch);
        for (auto& q : g_pins) if (q.key == p.key) return false;
        p.stack = IsDir(launch);
        p.name = name.empty() ? NameForLaunch(launch) : name;
    }
    if (at < 0 || at > (int)g_pins.size()) g_pins.push_back(p);
    else g_pins.insert(g_pins.begin() + at, p);
    return true;
}
static void SaveIni() {
    FILE* f = _wfopen(g_iniPath.c_str(), L"wt, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"# Just Better TaskBar settings — after editing, right-click the dock and choose \"Reload settings\".\n");
    fwprintf(f, L"# size: icon size (24-128)          magnify: magnification percent (0 = off, max 150)\n");
    fwprintf(f, L"# position: 0 bottom, 1 left, 2 right   autohide: auto-hide (1/0)\n");
    fwprintf(f, L"# autohide_delay: show delay in ms   autohide_anim: slide duration in ms (0 = no animation)\n");
    fwprintf(f, L"# hide_taskbar: hide the Windows taskbar   reserve: keep maximized windows above the dock\n");
    fwprintf(f, L"# indicators: running app indicators   bounce: bounce when an app opens\n");
    fwprintf(f, L"# minimized_style: minimized apps 0 normal, 1 dimmed, 2 blue line\n");
    fwprintf(f, L"# minimized_previews: show minimized windows as live previews on the right\n");
    fwprintf(f, L"# separate_apps: 1 = pinned apps left of the divider, open apps right (macOS); 0 = one row (order= keeps the order)\n");
    fwprintf(f, L"# lock: lock the dock (no reordering, adding, removing or resizing)\n");
    fwprintf(f, L"# glass: glass background   glass_style: 0 frosted, 1 liquid, 2 clear (beta)\n");
    fwprintf(f, L"# theme: 0 dark, 1 light, 2 system   opacity: shelf opacity %% (10-100)   tooltips: app name labels\n");
    fwprintf(f, L"# attention: bounce apps that need attention   click_minimize: minimize the active app on click   hide_fullscreen: hide in full screen\n");
    fwprintf(f, L"# show_launcher / show_running / show_trash: Start button / unpinned open apps / Recycle Bin\n");
    fwprintf(f, L"# menubar: top menu bar   clock_date: show date in clock   clock_seconds: show seconds\n");
    fwprintf(f, L"# tray_icons: show background app tray icons (Steam, Discord…) in the menu bar\n");
    fwprintf(f, L"# indicator_style: running app indicator 0 dot, 1 highlight, 2 blue line\n");
    fwprintf(f, L"# pin=<path>|<name>   (.exe, .lnk, folder = stack, shell:AppsFolder\\<AppID>, #spacer = spacer)   hide=<app> hides an app\n\n");
    fwprintf(f, L"size=%d\nmagnify=%d\nposition=%d\nautohide=%d\nautohide_delay=%d\nautohide_anim=%d\n", S.size, S.magnify,
             S.position, S.autohide ? 1 : 0, S.hideDelay, S.hideAnim);
    fwprintf(f, L"hide_taskbar=%d\nreserve=%d\nindicators=%d\nbounce=%d\nminimized_style=%d\nlock=%d\n\n", S.hideTaskbar ? 1 : 0,
             S.reserve ? 1 : 0, S.indicators ? 1 : 0, S.bounce ? 1 : 0, S.minimizedStyle, S.locked ? 1 : 0);
    fwprintf(f, L"glass=%d\ntheme=%d\nopacity=%d\ntooltips=%d\nattention=%d\nclick_minimize=%d\nhide_fullscreen=%d\n", S.glass ? 1 : 0,
             S.theme, S.opacity, S.tooltips ? 1 : 0, S.attention ? 1 : 0, S.clickMinimize ? 1 : 0, S.hideFullscreen ? 1 : 0);
    fwprintf(f, L"menubar=%d\nclock_date=%d\nclock_seconds=%d\ntray_icons=%d\n", S.showMenubar ? 1 : 0, S.clockDate ? 1 : 0,
             S.clockSeconds ? 1 : 0, S.trayIcons ? 1 : 0);
    fwprintf(f, L"indicator_style=%d\nminimized_previews=%d\nseparate_apps=%d\nglass_saturation=%d\nglass_style=%d\n", S.indicatorStyle,
             S.minimizedPreviews ? 1 : 0, S.separateApps ? 1 : 0, S.glassSaturation, S.glassStyle);
    fwprintf(f, L"show_launcher=%d\nshow_running=%d\nshow_trash=%d\n\n", S.showLauncher ? 1 : 0, S.showRunning ? 1 : 0, S.showTrash ? 1 : 0);
    {
        int n = 0;
        for (auto& p : g_pins) {
            if (!p.spacer) continue;
            std::wstring nk = L"#spacer:" + std::to_wstring(++n);
            for (auto& k : g_appOrder) if (k == p.key) k = nk;
            p.key = nk;
        }
        g_spacerSeq = n;
    }
    for (auto& p : g_pins) {
        if (p.spacer) fwprintf(f, L"pin=#spacer\n");
        else fwprintf(f, L"pin=%ls|%ls\n", p.launch.c_str(), p.name.c_str());
    }
    if (!g_appOrder.empty()) {
        std::wstring o;
        for (auto& k : g_appOrder) { if (!o.empty()) o += L'|'; o += k; }
        fwprintf(f, L"order=%ls\n", o.c_str());
    }
    for (auto& k : g_hiddenApps) fwprintf(f, L"hide=%ls\n", k.c_str());
    fclose(f);
    SettingsChanged();
}
static bool LoadIni() {
    FILE* f = _wfopen(g_iniPath.c_str(), L"rt, ccs=UTF-8");
    if (!f) return false;
    g_pins.clear();
    g_hiddenApps.clear();
    static wchar_t line[32768];
    while (fgetws(line, 32768, f)) {
        std::wstring s = Trim(line);
        if (s.empty() || s[0] == L'#' || s[0] == L';') continue;
        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = Lower(Trim(s.substr(0, eq))), v = Trim(s.substr(eq + 1));
        int n = _wtoi(v.c_str());
        if (k == L"size") S.size = std::clamp(n, 24, 128);
        else if (k == L"magnify") {
            static const int old[3] = {0, 45, 85};
            S.magnify = n <= 2 ? old[std::max(n, 0)] : std::clamp(n, 0, 150);
        }
        else if (k == L"position") S.position = std::clamp(n, 0, 2);
        else if (k == L"autohide") S.autohide = n != 0;
        else if (k == L"autohide_delay") S.hideDelay = std::clamp(n, 0, 5000);
        else if (k == L"autohide_anim") S.hideAnim = std::clamp(n, 0, 3000);
        else if (k == L"hide_taskbar") S.hideTaskbar = n != 0;
        else if (k == L"reserve") S.reserve = n != 0;
        else if (k == L"indicators") S.indicators = n != 0;
        else if (k == L"bounce") S.bounce = n != 0;
        else if (k == L"dim_minimized") S.minimizedStyle = n != 0 ? 1 : 0;
        else if (k == L"minimized_style") S.minimizedStyle = std::clamp(n, 0, 2);
        else if (k == L"minimized_previews") S.minimizedPreviews = n != 0;
        else if (k == L"separate_apps") S.separateApps = n != 0;
        else if (k == L"hide") { if (!v.empty() && !IsHiddenApp(Lower(v))) g_hiddenApps.push_back(Lower(v)); }
        else if (k == L"order") {
            g_appOrder.clear();
            size_t a = 0;
            while (a <= v.size()) {
                size_t b = v.find(L'|', a);
                std::wstring part = Trim(v.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a));
                if (!part.empty()) g_appOrder.push_back(part);
                if (b == std::wstring::npos) break;
                a = b + 1;
            }
        }
        else if (k == L"lock") S.locked = n != 0;
        else if (k == L"glass") S.glass = n != 0;
        else if (k == L"glass_saturation") S.glassSaturation = std::clamp(n, 100, 250);
        else if (k == L"glass_style") S.glassStyle = std::clamp(n, 0, 2);
        else if (k == L"theme") S.theme = std::clamp(n, 0, 2);
        else if (k == L"opacity") S.opacity = std::clamp(n, 10, 100);
        else if (k == L"tooltips") S.tooltips = n != 0;
        else if (k == L"attention") S.attention = n != 0;
        else if (k == L"click_minimize") S.clickMinimize = n != 0;
        else if (k == L"hide_fullscreen") S.hideFullscreen = n != 0;
        else if (k == L"show_launcher") S.showLauncher = n != 0;
        else if (k == L"show_running") S.showRunning = n != 0;
        else if (k == L"show_trash") S.showTrash = n != 0;
        else if (k == L"menubar") S.showMenubar = n != 0;
        else if (k == L"clock_date") S.clockDate = n != 0;
        else if (k == L"clock_seconds") S.clockSeconds = n != 0;
        else if (k == L"tray_icons") S.trayIcons = n != 0;
        else if (k == L"indicator_style") S.indicatorStyle = std::clamp(n, 0, 2);
        else if (k == L"pin") {
            size_t bar = v.find(L'|');
            AddPin(Trim(v.substr(0, bar)), bar == std::wstring::npos ? L"" : Trim(v.substr(bar + 1)));
        }
    }
    fclose(f);
    return true;
}
static void DefaultPins() {
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    AddPin(std::wstring(win) + L"\\explorer.exe", L"File Explorer");
    wchar_t app[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH);
    std::wstring dir = std::wstring(app) + L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.lnk").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring stem = Lower(FileStem(fd.cFileName)), t;
            if ((stem == L"file explorer" || stem == L"dosya gezgini") && !ResolveLnk(dir + fd.cFileName, t)) continue;
            AddPin(dir + fd.cFileName, L"");
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
static std::wstring DownloadsPath() {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(kDownloads, 0, nullptr, &p)) && p) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}
static void AddDownloadsStack() {
    std::wstring p = DownloadsPath();
    if (!p.empty()) AddPin(p, L"Downloads");
}
static bool HasDownloadsStack() {
    std::wstring k = Lower(DownloadsPath());
    for (auto& p : g_pins) if (p.key == k) return true;
    return false;
}

// ---------------------------------------------------------------- taskbar + AppBar
// The dock registers as a real AppBar, so Windows itself manages the reserved screen space.
// The hidden taskbar is switched to auto-hide so it reserves no space (restored on exit).
static const wchar_t* kRegKey = L"Software\\JustBetterTaskBar";
static int ReserveH() {
    Geo g = G();
    return (int)std::ceil(g.margin + g.shelf);
}
static void AppBarSetPos() {
    if (!g_appbar) return;
    APPBARDATA abd{sizeof abd};
    abd.hWnd = g_hwnd;
    abd.uEdge = S.position == 0 ? ABE_BOTTOM : S.position == 1 ? ABE_LEFT : ABE_RIGHT;
    MONITORINFO mi = PrimaryMon();
    int T = ReserveH();
    auto fit = [&](RECT& r) {
        if (S.position == 0) r.top = r.bottom - T;
        else if (S.position == 1) r.right = r.left + T;
        else r.left = r.right - T;
    };
    abd.rc = mi.rcMonitor;
    fit(abd.rc);
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    fit(abd.rc);
    SHAppBarMessage(ABM_SETPOS, &abd);
    g_abRect = abd.rc;
}
static void AppBarRegister(bool on) {
    APPBARDATA abd{sizeof abd};
    abd.hWnd = g_hwnd;
    if (on && !g_appbar) {
        abd.uCallbackMessage = WM_APPBAR;
        g_appbar = SHAppBarMessage(ABM_NEW, &abd) != 0;
        AppBarSetPos();
    } else if (!on && g_appbar) {
        SHAppBarMessage(ABM_REMOVE, &abd);
        g_appbar = false;
    }
}
static int BarH() { return (int)std::round(26 * g_scale); }
static void BarAppBarSetPos() {
    if (!g_barAppbar) return;
    APPBARDATA abd{sizeof abd};
    abd.hWnd = g_barWnd;
    abd.uEdge = ABE_TOP;
    MONITORINFO mi = PrimaryMon();
    abd.rc = mi.rcMonitor;
    abd.rc.bottom = abd.rc.top + BarH();
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    abd.rc.bottom = abd.rc.top + BarH();
    SHAppBarMessage(ABM_SETPOS, &abd);
    g_barRect = abd.rc;
}
static void BarAppBarRegister(bool on) {
    if (!g_barWnd) return;
    APPBARDATA abd{sizeof abd};
    abd.hWnd = g_barWnd;
    if (on && !g_barAppbar) {
        abd.uCallbackMessage = WM_BARAPPBAR;
        g_barAppbar = SHAppBarMessage(ABM_NEW, &abd) != 0;
        BarAppBarSetPos();
    } else if (!on && g_barAppbar) {
        SHAppBarMessage(ABM_REMOVE, &abd);
        g_barAppbar = false;
    }
}
static UINT TaskbarState() {
    APPBARDATA a{sizeof a};
    return (UINT)SHAppBarMessage(ABM_GETSTATE, &a);
}
static void SetTaskbarAutoHide(bool on) {
    APPBARDATA a{sizeof a};
    a.hWnd = ExplorerTray();
    UINT st = TaskbarState();
    a.lParam = on ? (st | ABS_AUTOHIDE) : (st & ~ABS_AUTOHIDE);
    SHAppBarMessage(ABM_SETSTATE, &a);
}
static void SetTaskbarsVisible(bool vis) {
    auto apply = [vis](HWND t) {
        if (t && (IsWindowVisible(t) != FALSE) != vis) ShowWindow(t, vis ? SW_SHOWNA : SW_HIDE);
    };
    apply(ExplorerTray());
    HWND s = nullptr;
    while ((s = FindWindowExW(nullptr, s, L"Shell_SecondaryTrayWnd", nullptr))) apply(s);
}
static void HideTaskbar() {
    if (!(TaskbarState() & ABS_AUTOHIDE)) {
        DWORD v = 0, sz = sizeof v;
        if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"PrevAutoHide", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) {
            v = 0;
            RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"PrevAutoHide", REG_DWORD, &v, sizeof v);
        }
        SetTaskbarAutoHide(true);
    }
    SetTaskbarsVisible(false);
}
static void RestoreTaskbar() {
    DWORD v = 0, sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"PrevAutoHide", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS) {
        SetTaskbarAutoHide(v != 0);
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegKey, L"PrevAutoHide");
    }
    SetTaskbarsVisible(true);
}
static void ApplyShell() {
    if (S.hideTaskbar) HideTaskbar();
    AppBarRegister(S.reserve && !S.autohide);
    BarAppBarRegister(S.showMenubar);
}
static void Cleanup() {
    if (g_cleaned) return;
    g_cleaned = true;
    AppBarRegister(false);
    BarAppBarRegister(false);
    if (S.hideTaskbar) RestoreTaskbar();
}
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS*) {
    Cleanup();
    return EXCEPTION_CONTINUE_SEARCH;
}
static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD, HWND h, LONG idObj, LONG, DWORD, DWORD) {
    if (idObj != OBJID_WINDOW || !S.hideTaskbar || !h || h == g_trayWnd) return;
    wchar_t c[64] = {};
    GetClassNameW(h, c, 64);
    if (!wcscmp(c, L"Shell_TrayWnd") || !wcscmp(c, L"Shell_SecondaryTrayWnd")) ShowWindow(h, SW_HIDE);
}
static RECT EdgeRect() {
    MONITORINFO mi = PrimaryMon();
    RECT r = S.hideTaskbar ? mi.rcMonitor : mi.rcWork;
    if (g_appbar) {
        if (S.position == 0) r.bottom = g_abRect.bottom;
        else if (S.position == 1) r.left = g_abRect.left;
        else r.right = g_abRect.right;
    }
    return r;
}

// ---------------------------------------------------------------- window list
static bool IsAppWindow(HWND h) {
    if (h == g_hwnd || h == g_dragWnd || !IsWindowVisible(h)) return false;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    if (GetWindow(h, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return false;
    if ((ex & WS_EX_NOACTIVATE) && !(ex & WS_EX_APPWINDOW)) return false;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    if (cloaked) return false;
    if (GetWindowTextLengthW(h) == 0) return false;
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
        !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        return false;
    return true;
}
static BOOL CALLBACK EnumProc(HWND h, LPARAM lp) {
    if (IsAppWindow(h)) ((std::vector<HWND>*)lp)->push_back(h);
    return TRUE;
}
struct ChildFind { DWORD pid, found; };
static BOOL CALLBACK ChildProc(HWND c, LPARAM lp) {
    auto* cf = (ChildFind*)lp;
    DWORD p = 0;
    GetWindowThreadProcessId(c, &p);
    if (p && p != cf->pid) { cf->found = p; return FALSE; }
    return TRUE;
}
static WinId Identify(HWND h) {
    WinId w;
    GetWindowThreadProcessId(h, &w.pid);
    w.path = ProcPath(w.pid);
    std::wstring lp = Lower(w.path);
    if (w.path.empty()) { w.key = Hex(h); return w; }
    if (!wcscmp(PathFindFileNameW(lp.c_str()), L"applicationframehost.exe")) {
        w.aumid = WindowAumid(h);
        if (w.aumid.empty()) {
            ChildFind cf{w.pid, 0};
            EnumChildWindows(h, ChildProc, (LPARAM)&cf);
            if (cf.found) w.aumid = ProcAumid(cf.found);
        }
        if (w.aumid.empty()) { w.key = Hex(h); return w; }
    } else if (lp.find(L"\\windowsapps\\") != std::wstring::npos) {
        w.aumid = ProcAumid(w.pid);
        if (w.aumid.empty()) w.aumid = WindowAumid(h);
    }
    if (!w.aumid.empty()) {
        w.key = L"aumid:" + Lower(w.aumid);
        w.launch = L"shell:AppsFolder\\" + w.aumid;
    } else {
        w.key = lp;
        w.launch = w.path;
    }
    return w;
}
static std::wstring KeyForPid(DWORD pid) {
    std::wstring lp = Lower(ProcPath(pid));
    if (lp.find(L"\\windowsapps\\") != std::wstring::npos) {
        std::wstring a = ProcAumid(pid);
        if (!a.empty()) return L"aumid:" + Lower(a);
    }
    return lp;
}
static std::wstring RunningName(const Group& g) {
    auto f = g_names.find(g.key);
    if (f != g_names.end()) return f->second;
    std::wstring n;
    if (!g.aumid.empty()) n = AppName(g.aumid);
    else if (!g.path.empty() && g.key.rfind(L"hwnd:", 0) != 0) n = FileDesc(g.path);
    if (n.empty()) {
        wchar_t t[256] = {};
        GetWindowTextW(g.first, t, 256);
        n = t;
    }
    if (g.key.rfind(L"hwnd:", 0) != 0) g_names[g.key] = n;
    return n;
}

void Render();
static void StartAnim() {
    if (g_animOn) return;
    g_animOn = true;
    g_lastTick = GetTickCount64();
    SetTimer(g_hwnd, T_ANIM, 10, nullptr);
}

static void Refresh(bool force = false) {
    std::vector<HWND> hw;
    EnumWindows(EnumProc, (LPARAM)&hw);
    std::map<HWND, WinId> nc;
    std::vector<Group> groups;
    for (HWND h : hw) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        WinId id;
        auto f = g_winCache.find(h);
        if (f != g_winCache.end() && f->second.pid == pid && f->second.key.rfind(L"hwnd:", 0) != 0) id = f->second;
        else id = Identify(h);
        nc[h] = id;
        Group* gp = nullptr;
        for (auto& g : groups) if (g.key == id.key) { gp = &g; break; }
        if (!gp) {
            groups.push_back(Group{id.key, id.launch, id.path, id.aumid, {}, h});
            gp = &groups.back();
        }
        gp->wins.push_back(h);
    }
    g_winCache.swap(nc);
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group& g) {
        if (!IsHiddenApp(g.key)) return false;
        for (auto& p : g_pins) if (p.key == g.key) return false;
        return true;
    }), groups.end());

    g_runOrder.erase(std::remove_if(g_runOrder.begin(), g_runOrder.end(), [&](const std::wstring& k) {
        for (auto& g : groups) if (g.key == k) return false;
        return true;
    }), g_runOrder.end());
    for (auto& g : groups)
        if (std::find(g_runOrder.begin(), g_runOrder.end(), g.key) == g_runOrder.end()) g_runOrder.push_back(g.key);

    std::vector<Item> items;
    if (S.showLauncher) {
        Item l;
        l.kind = K_LAUNCHER; l.key = L"#launcher"; l.name = L"Start"; l.icon = LauncherIcon();
        items.push_back(l);
    }
    std::vector<bool> used(groups.size(), false);
    auto pinItem = [&](const Pin& p) {
        Item it;
        it.key = p.key; it.launch = p.launch; it.name = p.name; it.pinned = true;
        it.ghost = g_dragging && p.key == g_dragKey;
        if (p.spacer) { it.kind = K_SPACER; return it; }
        if (p.stack) { it.kind = K_STACK; it.icon = IconFor(p.launch, nullptr); return it; }
        for (size_t i = 0; i < groups.size(); i++)
            if (groups[i].key == p.key) { it.wins = groups[i].wins; used[i] = true; }
        it.icon = IconFor(p.launch, it.wins.empty() ? nullptr : it.wins[0]);
        return it;
    };
    auto findPin = [](const std::wstring& k) -> const Pin* {
        for (auto& p : g_pins) if (p.key == k) return &p;
        return nullptr;
    };
    auto runningItem = [&](size_t i) {
        Item it;
        it.key = groups[i].key; it.launch = groups[i].launch; it.name = RunningName(groups[i]);
        it.wins = groups[i].wins;
        it.ghost = g_dragging && it.key == g_dragKey;
        it.icon = IconFor(groups[i].launch, groups[i].first);
        used[i] = true;
        return it;
    };
    auto pinOrSkip = [&](const Pin& p) {
        if (g_dragging && g_dragRemove && p.key == g_dragKey) {
            for (size_t i = 0; i < groups.size(); i++) if (groups[i].key == p.key) used[i] = true;
            return;
        }
        items.push_back(pinItem(p));
    };
    Item sep;
    sep.kind = K_SEP;
    if (S.separateApps) {
        for (auto& p : g_pins) if (!p.stack) pinOrSkip(p);
        items.push_back(sep);
        bool extra = false;
        if (S.showRunning)
            for (auto& k : g_runOrder)
                for (size_t i = 0; i < groups.size(); i++)
                    if (!used[i] && groups[i].key == k) { items.push_back(runningItem(i)); extra = true; }
        if (extra) items.push_back(sep);
    } else {
        std::vector<std::wstring> present;
        for (auto& p : g_pins) if (!p.stack) present.push_back(p.key);
        if (S.showRunning)
            for (auto& k : g_runOrder) if (!findPin(k)) present.push_back(k);
        auto has = [](const std::vector<std::wstring>& v, const std::wstring& k) { return std::find(v.begin(), v.end(), k) != v.end(); };
        g_appOrder.erase(std::remove_if(g_appOrder.begin(), g_appOrder.end(), [&](const std::wstring& k) { return !has(present, k); }), g_appOrder.end());
        for (auto& k : present) if (!has(g_appOrder, k)) g_appOrder.push_back(k);
        for (auto& k : g_appOrder) {
            if (const Pin* p = findPin(k)) { pinOrSkip(*p); continue; }
            for (size_t i = 0; i < groups.size(); i++)
                if (!used[i] && groups[i].key == k) { items.push_back(runningItem(i)); break; }
        }
        items.push_back(sep);
    }
    {
        std::vector<HWND> nowMin;
        for (auto& g : groups) for (HWND w : g.wins) if (IsIconic(w)) nowMin.push_back(w);
        g_minOrder.erase(std::remove_if(g_minOrder.begin(), g_minOrder.end(), [&](HWND w) {
            return std::find(nowMin.begin(), nowMin.end(), w) == nowMin.end();
        }), g_minOrder.end());
        for (HWND w : nowMin) if (std::find(g_minOrder.begin(), g_minOrder.end(), w) == g_minOrder.end()) g_minOrder.push_back(w);
        if (S.minimizedPreviews) {
            size_t start = g_minOrder.size() > 8 ? g_minOrder.size() - 8 : 0;
            for (size_t k = start; k < g_minOrder.size(); k++) {
                Item it;
                it.kind = K_MINWIN;
                it.key = L"min:" + Hex(g_minOrder[k]);
                it.wins = {g_minOrder[k]};
                wchar_t t[256] = {};
                GetWindowTextW(g_minOrder[k], t, 256);
                it.name = t;
                items.push_back(it);
            }
        }
    }
    for (auto& p : g_pins)
        if (p.stack && !(g_dragging && g_dragRemove && p.key == g_dragKey)) items.push_back(pinItem(p));
    if (S.showTrash) {
        Item t;
        t.kind = K_TRASH; t.key = L"#trash"; t.name = L"Recycle Bin"; t.icon = TrashIcon();
        items.push_back(t);
    }
    g_items.swap(items);

    ULONGLONG now = GetTickCount64();
    for (auto& it : g_items) {
        auto b = g_bounce.find(it.key);
        if (b != g_bounce.end() && !it.wins.empty()) {
            float t2 = (now - b->second.start) / 1000.f;
            if (t2 > 0.3f) b->second.count = std::min(b->second.count, (int)(t2 / kPeriod) + 1);
        }
    }
    std::wstring sig;
    HWND fgRoot = GetAncestor(GetForegroundWindow(), GA_ROOTOWNER);
    for (auto& it : g_items) {
        bool allMin = !it.wins.empty(), active = false;
        for (HWND w : it.wins) if (!IsIconic(w)) { allMin = false; break; }
        for (HWND w : it.wins) if (w == fgRoot) active = true;
        wchar_t b[80];
        swprintf(b, 80, L"|%d:%zu:%p:%d%d%d:", (int)it.kind, it.wins.size(), (void*)it.icon, allMin ? 1 : 0, it.ghost ? 1 : 0, active ? 1 : 0);
        sig += b + it.key;
    }
    if (!force && sig == g_sig) return;
    g_sig = sig;
    Render();
}
static void ScheduleRefresh() { SetTimer(g_hwnd, T_REFRESH, 60, nullptr); }

// ---------------------------------------------------------------- layout + drawing
static float Ease(float t) { return t * t * (3 - 2 * t); }
static void Layout() {
    Geo g = G();
    size_t n = g_items.size();
    std::vector<float> bw(n), aw(n), sz(n, 0.f);
    float baseTotal = 0;
    for (size_t i = 0; i < n; i++) {
        bw[i] = g_items[i].kind == K_SEP ? g.sepW : g.base + g.gap;
        baseTotal += bw[i];
    }
    float extra = (g.maxZ - g.base) * 2.4f + g.padX + 4 * g_scale;
    float L = std::ceil(baseTotal + 2 * extra);
    float C = std::ceil(g.margin + g.padB + std::max(g.maxZ, g.base + g.bounceH) + g.tipRoom);
    if (Horizontal()) { g_W = (int)L; g_H = (int)C; }
    else { g_W = (int)C; g_H = (int)L; }

    float baseLeft = extra;
    float mx = g_mx < 0 ? baseLeft + baseTotal / 2 : g_mx;
    float R = g.base * 2.7f;
    float cum = 0, aTotal = 0;
    for (size_t i = 0; i < n; i++) {
        if (g_items[i].kind == K_SEP) { aw[i] = bw[i]; }
        else {
            float c = baseLeft + cum + bw[i] / 2;
            float d = std::fabs(mx - c) / R;
            float f = d < 1 ? (std::cos(d * 3.14159265f) + 1) / 2 : 0;
            sz[i] = g.base + (g.maxZ - g.base) * f * g_hover;
            aw[i] = sz[i] + g.gap;
        }
        cum += bw[i];
        aTotal += aw[i];
    }
    float mxc = std::clamp(mx, baseLeft, baseLeft + baseTotal - 0.01f);
    float bc = baseLeft, ac = 0, anchor = 0;
    for (size_t i = 0; i < n; i++) {
        if (mxc < bc + bw[i] || i == n - 1) { anchor = ac + (mxc - bc) / bw[i] * aw[i]; break; }
        bc += bw[i];
        ac += aw[i];
    }
    float aLeft = mxc - anchor;
    if (aTotal + 2 * g.padX >= L) aLeft = (L - aTotal) / 2;
    else aLeft = std::clamp(aLeft, g.padX + 1, L - g.padX - 1 - aTotal);
    float x = aLeft;
    for (size_t i = 0; i < n; i++) {
        g_items[i].x = x;
        g_items[i].w = aw[i];
        g_items[i].size = sz[i];
        x += aw[i];
    }
    RECT e = EdgeRect();
    MONITORINFO mi = PrimaryMon();
    int shift = (int)std::round((1 - Ease(g_show)) * (g.margin + g.shelf + 2));
    int cx = (mi.rcMonitor.left + mi.rcMonitor.right) / 2, cy = (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
    switch (S.position) {
    case 0: g_X = cx - g_W / 2; g_Y = e.bottom - g_H + shift; break;
    case 1: g_X = e.left - shift; g_Y = cy - g_H / 2; break;
    default: g_X = e.right - g_W + shift; g_Y = cy - g_H / 2; break;
    }
}
static int HitTest(float m) {
    for (size_t i = 0; i < g_items.size(); i++) {
        auto& it = g_items[i];
        if (it.kind != K_SEP && m >= it.x && m < it.x + it.w) return (int)i;
    }
    return -1;
}
static bool IsOverSep(float m) {
    for (auto& it : g_items)
        if (it.kind == K_SEP && m >= it.x && m < it.x + it.w) return true;
    return false;
}
static float BounceOffset(const std::wstring& key, ULONGLONG now, float h) {
    auto f = g_bounce.find(key);
    if (f == g_bounce.end()) return 0;
    float t = (now - f->second.start) / 1000.f;
    if (t >= kPeriod * f->second.count) return 0;
    float ph = std::fmod(t, kPeriod) / kPeriod;
    return h * std::sin(ph * 3.14159265f);
}
// ---------------------------------------------------------------- frosted glass + theme
// Windows can't blur behind a per-pixel-alpha (layered) window, so a second window sits right
// behind the shelf. It is drawn with Windows Composition: the blurred desktop behind it
// (HostBackdropBrush) is clipped to an anti-aliased rounded shape of any radius.
// If Composition is unavailable it falls back to accent blur + a window region.
winrt::Windows::System::DispatcherQueueController g_dq{nullptr};
wuc::Compositor g_comp{nullptr};
HINSTANCE g_hinst = nullptr;

struct ACCENTPOLICY { int AccentState; int AccentFlags; DWORD GradientColor; int AnimationId; };
struct WINCOMPATTRDATA { int Attribute; PVOID Data; SIZE_T SizeOfData; };
typedef BOOL(WINAPI* SetWindowCompositionAttributeFn)(HWND, WINCOMPATTRDATA*);
static void EnableAccentBlur(HWND h) {
    auto f = (SetWindowCompositionAttributeFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    if (!f) return;
    ACCENTPOLICY ap{3 /*ACCENT_ENABLE_BLURBEHIND*/, 0, 0, 0};
    WINCOMPATTRDATA d{19 /*WCA_ACCENT_POLICY*/, &ap, sizeof ap};
    f(h, &d);
}
static LRESULT CALLBACK GlassProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}
static void InitComposition(HINSTANCE hi) {
    g_hinst = hi;
    WNDCLASSEXW wg{sizeof wg};
    wg.lpfnWndProc = GlassProc;
    wg.hInstance = hi;
    wg.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wg.lpszClassName = L"JustBetterTaskBarGlass";
    RegisterClassExW(&wg);
    try {
        DispatcherQueueOptions o{sizeof o, DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
        ABI::Windows::System::IDispatcherQueueController* p = nullptr;
        winrt::check_hresult(CreateDispatcherQueueController(o, &p));
        g_dq = {p, winrt::take_ownership_from_abi};
        g_comp = wuc::Compositor();
    } catch (...) {
        g_comp = nullptr;
    }
}
struct SaturationEffect : winrt::implements<SaturationEffect, winrt::Windows::Graphics::Effects::IGraphicsEffect,
                                            winrt::Windows::Graphics::Effects::IGraphicsEffectSource,
                                            ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop> {
    float amount;
    winrt::Windows::Graphics::Effects::IGraphicsEffectSource source;
    winrt::hstring name = L"saturation";
    SaturationEffect(float a, winrt::Windows::Graphics::Effects::IGraphicsEffectSource s) : amount(a), source(s) {}
    winrt::hstring Name() { return name; }
    void Name(winrt::hstring const& n) { name = n; }
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) noexcept override {
        *id = GUID{0x5cb2d9cf, 0x327d, 0x459f, {0xa0, 0xce, 0x40, 0xc0, 0xb2, 0x08, 0x6b, 0xf7}};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR, UINT*, ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept override { return E_INVALIDARG; }
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* c) noexcept override { *c = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT i, ABI::Windows::Foundation::IPropertyValue** v) noexcept override {
        if (i != 0) return E_INVALIDARG;
        try {
            winrt::Windows::Foundation::PropertyValue::CreateSingle(amount).as<ABI::Windows::Foundation::IPropertyValue>().copy_to(v);
            return S_OK;
        } catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE GetSource(UINT i, ABI::Windows::Graphics::Effects::IGraphicsEffectSource** s) noexcept override {
        if (i != 0) return E_INVALIDARG;
        try {
            source.as<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>().copy_to(s);
            return S_OK;
        } catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* c) noexcept override { *c = 1; return S_OK; }
};
static bool LightTheme();
static void ApplyGlassBrush(Glass& gl) {
    if (!gl.comp || !gl.visual) return;
    if (&gl == &g_glass && liquid::Active()) {
        liquid::SetLook(LightTheme(), S.glassSaturation / 100.0f, g_scale, S.glassStyle - 1);
        return;
    }
    auto backdrop = g_comp.CreateHostBackdropBrush();
    try {
        if (S.glassSaturation <= 100) { gl.visual.Brush(backdrop); return; }
        wuc::CompositionEffectSourceParameter src(L"backdrop");
        auto fx = winrt::make<SaturationEffect>(S.glassSaturation / 100.0f, src);
        auto brush = g_comp.CreateEffectFactory(fx).CreateBrush();
        brush.SetSourceParameter(L"backdrop", backdrop);
        gl.visual.Brush(brush);
    } catch (...) {
        gl.visual.Brush(backdrop);
    }
}
static void CreateGlass(Glass& gl) {
    DWORD corner = 1;
    COLORREF none = 0xFFFFFFFE;
    try {
        if (!g_comp) throw winrt::hresult_error(E_FAIL);
        gl.wnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                 L"JustBetterTaskBarGlass", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_hinst, nullptr);
        if (!gl.wnd) throw winrt::hresult_error(E_FAIL);
        BOOL t = TRUE;
        DwmSetWindowAttribute(gl.wnd, 17 /*DWMWA_USE_HOSTBACKDROPBRUSH*/, &t, sizeof t);
        DwmSetWindowAttribute(gl.wnd, 33, &corner, sizeof corner);
        DwmSetWindowAttribute(gl.wnd, 34, &none, sizeof none);
        auto interop = g_comp.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        winrt::check_hresult(interop->CreateDesktopWindowTarget(
            gl.wnd, true, reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(gl.target))));
        gl.visual = g_comp.CreateSpriteVisual();
        gl.geo = g_comp.CreateRoundedRectangleGeometry();
        gl.visual.Clip(g_comp.CreateGeometricClip(gl.geo));
        gl.target.Root(gl.visual);
        gl.comp = true;
        ApplyGlassBrush(gl);
    } catch (...) {
        gl.target = nullptr;
        gl.visual = nullptr;
        gl.geo = nullptr;
        if (gl.wnd) DestroyWindow(gl.wnd);
        gl.wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                 L"JustBetterTaskBarGlass", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_hinst, nullptr);
        if (gl.wnd) {
            EnableAccentBlur(gl.wnd);
            DwmSetWindowAttribute(gl.wnd, 33, &corner, sizeof corner);
            DwmSetWindowAttribute(gl.wnd, 34, &none, sizeof none);
        }
        gl.comp = false;
    }
}
static void FreeGlass(Glass& gl) {
    gl.target = nullptr;
    gl.visual = nullptr;
    gl.geo = nullptr;
}
static void HideGlass(Glass& gl) {
    if (&gl == &g_glass) liquid::Pause();
    if (gl.wnd && IsWindowVisible(gl.wnd)) ShowWindow(gl.wnd, SW_HIDE);
    gl.rc = RECT{};
}
static void PlaceGlass(Glass& gl, HWND above, RECT r, float radius) {
    if (!gl.wnd) return;
    if (EqualRect(&r, &gl.rc) && IsWindowVisible(gl.wnd)) return;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) { HideGlass(gl); return; }
    bool resized = w != gl.rc.right - gl.rc.left || h != gl.rc.bottom - gl.rc.top;
    if (resized) {
        if (gl.comp) {
            float rr = std::max(0.0f, radius);
            gl.visual.Size({(float)w, (float)h});
            gl.geo.Size({(float)w, (float)h});
            gl.geo.CornerRadius({rr, rr});
        } else if (radius > 0) {
            int d = (int)std::round(radius * 2);
            SetWindowRgn(gl.wnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d), TRUE);
        }
    }
    SetWindowPos(gl.wnd, above, r.left, r.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    gl.rc = r;
    if (&gl == &g_glass && liquid::Active()) liquid::Place(r, std::max(0.0f, radius));
}
static void HideGlass() { HideGlass(g_glass); }

// Liquid glass needs our own windows left out of screen capture, otherwise the dock would
// refract itself. As a side effect the dock doesn't appear in screenshots while it is on.
static bool g_liquidFailed = false;
static void SetCaptureExclusion(bool on) {
    for (HWND h : {g_hwnd, g_glass.wnd, g_barWnd, g_barGlass.wnd, g_dragWnd})
        if (h) SetWindowDisplayAffinity(h, on ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}
static void SyncLiquid() {
    bool want = S.glass && S.glassStyle >= 1 && g_glass.comp && !g_liquidFailed;
    if (want && !liquid::Active()) {
        SetCaptureExclusion(true);
        if (liquid::Start(g_glass.wnd, g_comp, g_glass.visual, g_scale, LightTheme(), S.glassSaturation / 100.0f, g_hwnd, WM_LIQUIDACCESS)) {
            g_glass.rc = RECT{};
        } else {
            g_liquidFailed = true;
            SetCaptureExclusion(false);
            ApplyGlassBrush(g_glass);
        }
    } else if (!want && liquid::Active()) {
        liquid::Stop();
        SetCaptureExclusion(false);
        ApplyGlassBrush(g_glass);
        g_glass.rc = RECT{};
    }
    if (liquid::Active()) liquid::SetLook(LightTheme(), S.glassSaturation / 100.0f, g_scale, S.glassStyle - 1);
}
static void UpdateGlass(RectF shelf, float radius) {
    SyncLiquid();
    if (!S.glass || g_hiddenFS || !IsWindowVisible(g_hwnd)) { HideGlass(g_glass); return; }
    RECT r{g_X + (LONG)std::floor(shelf.X) + 1, g_Y + (LONG)std::floor(shelf.Y) + 1,
           g_X + (LONG)std::ceil(shelf.X + shelf.Width) - 1, g_Y + (LONG)std::ceil(shelf.Y + shelf.Height) - 1};
    PlaceGlass(g_glass, g_hwnd, r, radius - 1);
}
static bool ReadSysLight() {
    DWORD v = 0, sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) return false;
    return v != 0;
}
static bool LightTheme() { return S.theme == 1 || (S.theme == 2 && g_sysLight); }

static SizeF TooltipSize(Graphics& gr, const std::wstring& text) {
    std::wstring name = text.size() > 48 ? text.substr(0, 46) + L"…" : text;
    RectF tb;
    gr.MeasureString(name.c_str(), -1, g_font, PointF(0, 0), &tb);
    return SizeF(tb.Width + 22 * g_scale, tb.Height + 8 * g_scale);
}
static void UpdateThumbs(const std::vector<std::pair<HWND, RectF>>& v) {
    for (auto it = g_thumbs.begin(); it != g_thumbs.end();) {
        bool keep = false;
        for (auto& p : v) if (p.first == it->first) keep = true;
        if (!keep) { DwmUnregisterThumbnail(it->second); it = g_thumbs.erase(it); }
        else ++it;
    }
    for (auto& p : v) {
        HTHUMBNAIL th = nullptr;
        auto f = g_thumbs.find(p.first);
        if (f == g_thumbs.end()) {
            if (FAILED(DwmRegisterThumbnail(g_hwnd, p.first, &th))) continue;
            g_thumbs[p.first] = th;
        } else {
            th = f->second;
        }
        SIZE src{};
        if (FAILED(DwmQueryThumbnailSourceSize(th, &src)) || src.cx <= 0 || src.cy <= 0) continue;
        RectF r = p.second;
        float pad = 2 * g_scale, ar = (float)src.cx / src.cy;
        r.Inflate(-pad, -pad);
        float w = r.Width, h = r.Height;
        if (ar > 1) h = w / ar; else w = h * ar;
        float x = r.X + (r.Width - w) / 2, y = r.Y + (r.Height - h) / 2;
        DWM_THUMBNAIL_PROPERTIES tp{};
        tp.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        tp.rcDestination = RECT{(LONG)std::lround(x), (LONG)std::lround(y), (LONG)std::lround(x + w), (LONG)std::lround(y + h)};
        tp.fVisible = TRUE;
        tp.opacity = 255;
        tp.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(th, &tp);
    }
}
void Render() {
    if (!g_hwnd || g_items.empty()) return;
    std::vector<std::pair<HWND, RectF>> thumbRects;
    Layout();
    Geo g = G();
    float s = g_scale;
    g_dock.Ensure(g_W, g_H);
    g_dock.Clear();
    float c0 = g.margin, cShelf = g.margin + g.shelf, iconBase = g.margin + g.padB;
    float left = g_items.front().x - g.padX, right = g_items.back().x + g_items.back().w + g.padX;
    {
        Graphics gr(g_dock.bmp);
        gr.SetSmoothingMode(SmoothingModeAntiAlias);
        gr.SetInterpolationMode(InterpolationModeHighQualityBilinear);
        gr.SetPixelOffsetMode(PixelOffsetModeHalf);
        gr.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        if (g_hover > 0.01f) {
            SolidBrush hb(Color(1, 0, 0, 0));
            gr.FillRectangle(&hb, Rc(left, right, c0, iconBase + g.maxZ + 6 * s));
        }
        bool light = LightTheme();
        BYTE alpha = (BYTE)std::clamp(S.opacity * 255 / 100, 10, 255);
        bool liquidOn = S.glass && liquid::Active();
        GraphicsPath shelf;
        RoundRectPath(shelf, Rc(left, right, c0, cShelf), g.radius);
        SolidBrush sb(light ? Color(alpha, 246, 246, 248) : Color(alpha, 28, 28, 34));
        if (!liquidOn) gr.FillPath(&sb, &shelf);
        RectF shelfR = Rc(left, right, c0, cShelf);
        LinearGradientBrush rim(PointF(0, shelfR.Y - 1), PointF(0, shelfR.GetBottom() + 1),
                                light ? Color(220, 255, 255, 255) : Color(135, 255, 255, 255),
                                light ? Color(55, 0, 0, 0) : Color(28, 255, 255, 255));
        Pen border(light ? Color(60, 0, 0, 0) : Color(70, 255, 255, 255), 1.0f * s);
        Pen rimPen(&rim, 1.0f * s);
        if (!liquidOn) gr.DrawPath(S.glass ? &rimPen : &border, &shelf);

        ULONGLONG now = GetTickCount64();
        int hov = (S.tooltips && g_tracking && !g_dragging && g_hover > 0.5f) ? HitTest(g_mx) : -1;
        HWND fgRoot = GetAncestor(GetForegroundWindow(), GA_ROOTOWNER);
        for (size_t i = 0; i < g_items.size(); i++) {
            auto& it = g_items[i];
            float mc = it.x + it.w / 2;
            if (it.kind == K_SEP) {
                Pen sp(light ? Color(70, 0, 0, 0) : Color(80, 255, 255, 255), 1.0f * s);
                gr.DrawLine(&sp, Pt(mc, c0 + 9 * s), Pt(mc, cShelf - 9 * s));
                continue;
            }
            if (it.kind == K_MINWIN) {
                float sz = it.size, m0 = it.x + (it.w - sz) / 2;
                if (!it.wins.empty()) thumbRects.push_back({it.wins[0], Rc(m0, m0 + sz, iconBase, iconBase + sz)});
                continue;
            }
            if (it.ghost || it.kind == K_SPACER || !it.icon) continue;
            float sz = it.size, m0 = it.x + (it.w - sz) / 2;
            float cc = iconBase + (S.bounce ? BounceOffset(it.key, now, g.bounceH) : 0);
            RectF dst = Rc(m0, m0 + sz, cc, cc + sz);
            bool running = S.indicators && !it.wins.empty();
            bool active = false;
            for (HWND w : it.wins) if (w == fgRoot) active = true;
            if (running && S.indicatorStyle == 1) {
                float in = 1.5f * s;
                GraphicsPath sp;
                RoundRectPath(sp, Rc(it.x + in, it.x + it.w - in, c0 + 3 * s, cc + sz + 3 * s), 9 * s);
                SolidBrush sh(light ? Color(active ? 50 : 26, 0, 0, 0) : Color(active ? 72 : 36, 255, 255, 255));
                gr.FillPath(&sh, &sp);
            }
            bool allMin = it.kind == K_APP && !it.wins.empty();
            for (HWND w : it.wins) if (!IsIconic(w)) { allMin = false; break; }
            float rgb = (int)i == g_pressIdx ? 0.55f : 1.0f, a = (allMin && S.minimizedStyle == 1) ? 0.45f : 1.0f;
            if (rgb < 1 || a < 1) {
                ColorMatrix cm = {{{rgb, 0, 0, 0, 0}, {0, rgb, 0, 0, 0}, {0, 0, rgb, 0, 0}, {0, 0, 0, a, 0}, {0, 0, 0, 0, 1}}};
                ImageAttributes ia;
                ia.SetColorMatrix(&cm);
                gr.DrawImage(it.icon, dst, 0, 0, (REAL)it.icon->GetWidth(), (REAL)it.icon->GetHeight(), UnitPixel, &ia);
            } else {
                gr.DrawImage(it.icon, dst);
            }
            if (allMin && S.minimizedStyle == 2) {
                float len = std::max(10 * s, sz * 0.5f), th = 3 * s, cl = c0 + g.padB * 0.45f;
                GraphicsPath lp;
                RoundRectPath(lp, Rc(mc - len / 2, mc + len / 2, cl - th / 2, cl + th / 2), th / 2);
                SolidBrush lb(Color(255, 64, 156, 255));
                gr.FillPath(&lb, &lp);
            } else if (running && S.indicatorStyle == 0) {
                float r = 2.3f * s;
                PointF p = Pt(mc, c0 + g.padB * 0.5f);
                SolidBrush db(light ? Color(210, 40, 40, 44) : Color(230, 235, 235, 240));
                gr.FillEllipse(&db, p.X - r, p.Y - r, 2 * r, 2 * r);
            } else if (running && S.indicatorStyle == 2) {
                float len = std::max(8 * s, sz * (active ? 0.42f : 0.2f)), th = 3 * s, cl = c0 + g.padB * 0.45f;
                GraphicsPath lp;
                RoundRectPath(lp, Rc(mc - len / 2, mc + len / 2, cl - th / 2, cl + th / 2), th / 2);
                SolidBrush lb(active ? Color(255, 64, 156, 255) : Color(200, 64, 156, 255));
                gr.FillPath(&lb, &lp);
            }
        }
        if (hov >= 0 && g_font && !g_items[hov].name.empty()) {
            auto& it = g_items[hov];
            SizeF ts = TooltipSize(gr, it.name);
            float anchorC = iconBase + it.size + 10 * s, mc = it.x + it.w / 2, tx, ty;
            if (S.position == 0) {
                tx = std::clamp(mc - ts.Width / 2, 2.0f, g_W - ts.Width - 2);
                ty = std::max(1.0f, g_H - anchorC - ts.Height);
            } else {
                ty = std::clamp(mc - ts.Height / 2, 2.0f, g_H - ts.Height - 2);
                tx = S.position == 1 ? anchorC : g_W - anchorC - ts.Width;
            }
            GraphicsPath tp;
            RoundRectPath(tp, RectF(tx, ty, ts.Width, ts.Height), 7 * s);
            SolidBrush tbg(Color(225, 38, 38, 44));
            gr.FillPath(&tbg, &tp);
            Pen tpen(Color(60, 255, 255, 255), 1.0f * s);
            gr.DrawPath(&tpen, &tp);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush tc(Color(255, 245, 245, 247));
            std::wstring name = it.name.size() > 48 ? it.name.substr(0, 46) + L"…" : it.name;
            gr.DrawString(name.c_str(), -1, g_font, RectF(tx, ty, ts.Width, ts.Height), &sf, &tc);
        }
    }
    g_dock.Present(g_hwnd, g_X, g_Y);
    UpdateThumbs(thumbRects);
    UpdateGlass(Rc(left, right, c0, cShelf), g.radius);
    RectF sr = Rc(left, right, 0, g_hover > 0.01f ? iconBase + g.maxZ + 6 * s : cShelf);
    g_shelfScreen = RECT{g_X + (LONG)sr.X, g_Y + (LONG)sr.Y, g_X + (LONG)(sr.X + sr.Width), g_Y + (LONG)(sr.Y + sr.Height)};
}

static void UpdateDragWnd() {
    if (!g_dragWnd) return;
    Geo g = G();
    float s = g_scale;
    int sz = (int)g.base, lab = g_dragRemove ? (int)(30 * s) : 0;
    int W = std::max(sz, (int)(110 * s)), H = sz + lab;
    g_dragSurf.Ensure(W, H);
    g_dragSurf.Clear();
    {
        Graphics gr(g_dragSurf.bmp);
        gr.SetSmoothingMode(SmoothingModeAntiAlias);
        gr.SetInterpolationMode(InterpolationModeHighQualityBilinear);
        gr.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        RectF dst((W - sz) / 2.0f, (float)lab, (float)sz, (float)sz);
        if (g_dragIcon) {
            float a = g_dragRemove ? 0.55f : 0.9f;
            ColorMatrix cm = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, a, 0}, {0, 0, 0, 0, 1}}};
            ImageAttributes ia;
            ia.SetColorMatrix(&cm);
            gr.DrawImage(g_dragIcon, dst, 0, 0, (REAL)g_dragIcon->GetWidth(), (REAL)g_dragIcon->GetHeight(), UnitPixel, &ia);
        } else {
            GraphicsPath p;
            RoundRectPath(p, RectF(dst.X + 4 * s, dst.Y + 4 * s, dst.Width - 8 * s, dst.Height - 8 * s), 10 * s);
            Pen pen(Color(180, 255, 255, 255), 2 * s);
            pen.SetDashStyle(DashStyleDash);
            gr.DrawPath(&pen, &p);
        }
        if (g_dragRemove && g_font) {
            SizeF ts = TooltipSize(gr, L"Remove");
            float tx = (W - ts.Width) / 2;
            GraphicsPath tp;
            RoundRectPath(tp, RectF(tx, 0, ts.Width, ts.Height), 7 * s);
            SolidBrush tbg(Color(225, 38, 38, 44));
            gr.FillPath(&tbg, &tp);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush tc(Color(255, 245, 245, 247));
            gr.DrawString(L"Remove", -1, g_font, RectF(tx, 0, ts.Width, ts.Height), &sf, &tc);
        }
    }
    POINT p;
    GetCursorPos(&p);
    g_dragSurf.Present(g_dragWnd, p.x - W / 2, p.y - lab - sz / 2);
    SetWindowPos(g_dragWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void Tick() {
    ULONGLONG now = GetTickCount64();
    float dt = std::min(0.1f, (now - g_lastTick) / 1000.f);
    g_lastTick = now;
    float k = 1 - std::exp(-dt * 13);
    g_hover += (g_hoverTarget - g_hover) * k;
    if (std::fabs(g_hoverTarget - g_hover) < 0.003f) g_hover = g_hoverTarget;
    if (g_show != g_showTarget) {
        float step = S.hideAnim <= 0 ? 1.0f : dt / (S.hideAnim / 1000.0f);
        g_show = g_show < g_showTarget ? std::min(g_showTarget, g_show + step) : std::max(g_showTarget, g_show - step);
    }
    for (auto it = g_bounce.begin(); it != g_bounce.end();) {
        if ((now - it->second.start) / 1000.f >= kPeriod * it->second.count) it = g_bounce.erase(it);
        else ++it;
    }
    Render();
    if (g_hover == g_hoverTarget && g_show == g_showTarget && g_bounce.empty()) {
        KillTimer(g_hwnd, T_ANIM);
        g_animOn = false;
    }
}
static void EdgeTick() {
    if (!S.autohide || g_hiddenFS) return;
    POINT p;
    GetCursorPos(&p);
    RECT e = EdgeRect();
    MONITORINFO mi = PrimaryMon();
    bool atEdge;
    if (S.position == 0) atEdge = p.y >= e.bottom - 2 && p.y <= e.bottom + 1 && p.x >= mi.rcMonitor.left && p.x < mi.rcMonitor.right;
    else if (S.position == 1) atEdge = p.x <= e.left + 1 && p.x >= e.left - 1 && p.y >= mi.rcMonitor.top && p.y < mi.rcMonitor.bottom;
    else atEdge = p.x >= e.right - 2 && p.x <= e.right + 1 && p.y >= mi.rcMonitor.top && p.y < mi.rcMonitor.bottom;
    bool over = g_tracking || g_resizing || g_dragging || g_menuOpen || g_down || (g_show > 0.5f && PtInRect(&g_shelfScreen, p));
    ULONGLONG now = GetTickCount64();
    if (g_showTarget < 0.5f) {
        if (atEdge) {
            if (!g_edgeSince) g_edgeSince = now;
            if (now - g_edgeSince >= (ULONGLONG)S.hideDelay) { g_showTarget = 1; g_edgeSince = 0; StartAnim(); }
        } else {
            g_edgeSince = 0;
        }
    } else {
        if (over || atEdge) g_leaveSince = 0;
        else {
            if (!g_leaveSince) g_leaveSince = now;
            if (now - g_leaveSince >= 250) { g_showTarget = 0; g_leaveSince = 0; StartAnim(); }
        }
    }
}
static void UpdateEdgeTimer() {
    if (S.autohide) SetTimer(g_hwnd, T_EDGE, 40, nullptr);
    else KillTimer(g_hwnd, T_EDGE);
}

// ---------------------------------------------------------------- actions
typedef void(WINAPI* SwitchToThisWindowFn)(HWND, BOOL);
static void Activate(HWND h) {
    if (IsIconic(h)) ShowWindowAsync(h, SW_RESTORE);
    if (SetForegroundWindow(h) && GetForegroundWindow() == h) return;
    HWND cur = GetForegroundWindow();
    DWORD fgT = cur ? GetWindowThreadProcessId(cur, nullptr) : 0, me = GetCurrentThreadId();
    if (fgT && fgT != me) AttachThreadInput(me, fgT, TRUE);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if (fgT && fgT != me) AttachThreadInput(me, fgT, FALSE);
    if (GetForegroundWindow() == h) return;
    static auto sw = (SwitchToThisWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SwitchToThisWindow");
    if (sw) sw(h, TRUE);
}
static void LaunchPath(const std::wstring& launch, const wchar_t* params = nullptr) {
    std::wstring a = AumidFromLaunch(launch);
    if (!a.empty()) ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"shell:AppsFolder\\" + a).c_str(), nullptr, SW_SHOWNORMAL);
    else ShellExecuteW(nullptr, L"open", launch.c_str(), params, nullptr, SW_SHOWNORMAL);
}
static void StartBounce(const std::wstring& key, int count) {
    if (!S.bounce) return;
    g_bounce[key] = Bounce{GetTickCount64(), count};
    StartAnim();
}
static void LaunchItem(const Item& it) {
    if (it.launch.empty()) return;
    LaunchPath(it.launch);
    StartBounce(it.key, 5);
}
static void SendWinKey() {
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_LWIN;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}
static void OpenTrash() {
    ShellExecuteW(nullptr, L"open", L"explorer.exe", L"shell:RecycleBinFolder", nullptr, SW_SHOWNORMAL);
}
static void OpenFolder(const std::wstring& p) {
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"\"" + p + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}
static void MenuAnchor(int idx, POINT& pt, UINT& flags) {
    if (idx < 0 || idx >= (int)g_items.size()) {
        GetCursorPos(&pt);
        flags = TPM_BOTTOMALIGN | TPM_CENTERALIGN;
        return;
    }
    Geo g = G();
    auto& it = g_items[idx];
    PointF p = Pt(it.x + it.w / 2, g.margin + g.shelf + 6 * g_scale);
    pt = POINT{g_X + (LONG)p.X, g_Y + (LONG)p.Y};
    flags = S.position == 0 ? (TPM_BOTTOMALIGN | TPM_CENTERALIGN)
          : S.position == 1 ? (TPM_LEFTALIGN | TPM_VCENTERALIGN) : (TPM_RIGHTALIGN | TPM_VCENTERALIGN);
}
static int RunMenu(HMENU m, int idx) {
    POINT pt;
    UINT flags;
    MenuAnchor(idx, pt, flags);
    g_menuOpen = true;
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | flags, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    g_menuOpen = false;
    TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, g_hwnd, 0};
    g_tracking = TrackMouseEvent(&tme) != FALSE;
    return cmd;
}
static void ShowStack(int idx) {
    Item it = g_items[idx];
    struct Ent { std::wstring path, name; ULONGLONG t; };
    std::vector<Ent> v;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((it.launch + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            ULONGLONG c = ((ULONGLONG)fd.ftCreationTime.dwHighDateTime << 32) | fd.ftCreationTime.dwLowDateTime;
            ULONGLONG w = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            v.push_back(Ent{it.launch + L"\\" + fd.cFileName, fd.cFileName, std::max(c, w)});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(v.begin(), v.end(), [](const Ent& a, const Ent& b) { return a.t > b.t; });
    if (v.size() > 25) v.resize(25);
    HMENU m = CreatePopupMenu();
    std::vector<HBITMAP> bmps;
    for (size_t i = 0; i < v.size(); i++) {
        std::wstring label;
        for (wchar_t c : v[i].name) { label += c; if (c == L'&') label += L'&'; }
        MENUITEMINFOW mii{sizeof mii};
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
        mii.wID = ID_FILE0 + (UINT)i;
        mii.dwTypeData = &label[0];
        mii.hbmpItem = SmallIconBitmap(v[i].path);
        if (mii.hbmpItem) bmps.push_back(mii.hbmpItem);
        InsertMenuItemW(m, (UINT)i, TRUE, &mii);
    }
    if (v.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"(Folder is empty)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_FOLDEROPEN, (L"Open \"" + it.name + L"\" folder").c_str());
    int cmd = RunMenu(m, idx);
    DestroyMenu(m);
    for (HBITMAP b : bmps) DeleteObject(b);
    if (cmd >= ID_FILE0 && cmd < ID_FILE0 + (int)v.size()) ShellExecuteW(nullptr, L"open", v[cmd - ID_FILE0].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == ID_FOLDEROPEN) OpenFolder(it.launch);
}
static void OnClick(int idx, bool newInstance) {
    Item it = g_items[idx];
    if (it.kind == K_LAUNCHER) { SendWinKey(); return; }
    if (it.kind == K_TRASH) { OpenTrash(); return; }
    if (it.kind == K_SPACER) return;
    if (it.kind == K_STACK) { ShowStack(idx); return; }
    if (it.kind == K_MINWIN) { if (!it.wins.empty()) Activate(it.wins[0]); return; }
    if (it.wins.empty() || newInstance) { LaunchItem(it); return; }
    HWND fg = GetForegroundWindow(), fgRoot = GetAncestor(fg, GA_ROOTOWNER);
    bool active = false;
    for (HWND w : it.wins) if ((w == fg || w == fgRoot) && !IsIconic(w)) active = true;
    if (active) {
        if (it.wins.size() > 1) Activate(it.wins.back());
        else if (S.clickMinimize) PostMessageW(it.wins[0], WM_SYSCOMMAND, SC_MINIMIZE, 0);
    } else {
        HWND target = it.wins[0];
        for (HWND w : it.wins) if (!IsIconic(w)) { target = w; break; }
        Activate(target);
    }
}
static bool IsAutostart() {
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"JustBetterTaskBar",
                        RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}
static void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + g_exePath + L"\"";
        RegSetValueExW(k, L"JustBetterTaskBar", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"JustBetterTaskBar");
    }
    RegCloseKey(k);
}
static void UpdateTrash() {
    SHQUERYRBINFO qi{sizeof qi};
    bool full = SUCCEEDED(SHQueryRecycleBinW(nullptr, &qi)) && qi.i64NumItems > 0;
    if (full != g_trashFull) {
        g_trashFull = full;
        for (auto& it : g_items) if (it.kind == K_TRASH) it.icon = TrashIcon();
        Render();
    }
}
static void ApplySettingsChange() {
    SaveIni();
    ClearIcons();
    CreateFontObj();
    AppBarRegister(false);
    ApplyShell();
    UpdateEdgeTimer();
    if (!S.autohide) g_show = g_showTarget = 1;
    Refresh(true);
}
static void RemovePin(const std::wstring& key) {
    g_pins.erase(std::remove_if(g_pins.begin(), g_pins.end(), [&](const Pin& p) { return p.key == key; }), g_pins.end());
    SaveIni();
    Refresh(true);
}
static HMENU BuildDockMenu() {
    HMENU d = CreatePopupMenu(), sz = CreatePopupMenu(), mg = CreatePopupMenu(), ps = CreatePopupMenu(), hd = CreatePopupMenu();
    for (int i = 0; i < 4; i++) AppendMenuW(sz, MF_STRING | (S.size == kSizes[i] ? MF_CHECKED : 0) | (S.locked ? MF_GRAYED : 0), ID_SIZE0 + i, kSizeNames[i]);
    AppendMenuW(sz, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sz, MF_STRING | MF_GRAYED, 0, L"Fine-tune: drag the divider");
    AppendMenuW(sz, MF_STRING | MF_GRAYED, 0, L"or Ctrl + mouse wheel");
    for (int i = 0; i < 6; i++) AppendMenuW(mg, MF_STRING | (S.magnify == kMags[i] ? MF_CHECKED : 0), ID_MAG0 + i, kMagNames[i]);
    AppendMenuW(mg, MF_SEPARATOR, 0, nullptr);
    wchar_t cur[64];
    swprintf(cur, 64, L"Current: %%%d   (Ctrl + Shift + wheel)", S.magnify);
    AppendMenuW(mg, MF_STRING | MF_GRAYED, 0, cur);
    for (int i = 0; i < 3; i++) AppendMenuW(ps, MF_STRING | (S.position == i ? MF_CHECKED : 0), ID_POS0 + i, kPosNames[i]);
    AppendMenuW(hd, MF_STRING | MF_GRAYED, 0, L"Delay before showing");
    for (int i = 0; i < 4; i++) AppendMenuW(hd, MF_STRING | (S.hideDelay == kDelays[i] ? MF_CHECKED : 0), ID_DELAY0 + i, kDelayNames[i]);
    AppendMenuW(hd, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hd, MF_STRING | MF_GRAYED, 0, L"Slide speed");
    for (int i = 0; i < 4; i++) AppendMenuW(hd, MF_STRING | (S.hideAnim == kAnims[i] ? MF_CHECKED : 0), ID_ANIM0 + i, kAnimNames[i]);

    auto chk = [](bool b) -> UINT { return MF_STRING | (b ? MF_CHECKED : 0); };
    HMENU look = CreatePopupMenu();
    AppendMenuW(look, chk(S.glass), ID_GLASS, L"Glass background");
    AppendMenuW(look, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(look, MF_STRING | MF_GRAYED, 0, L"Color");
    for (int i = 0; i < 3; i++) AppendMenuW(look, chk(S.theme == i), ID_THEME0 + i, kThemeNames[i]);
    AppendMenuW(look, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(look, MF_STRING | MF_GRAYED, 0, L"Background transparency");
    for (int i = 0; i < 4; i++) AppendMenuW(look, chk(S.opacity == kOpacs[i]), ID_OPAC0 + i, kOpacNames[i]);
    AppendMenuW(look, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(look, chk(S.tooltips), ID_TIPS, L"Show app names");
    AppendMenuW(look, chk(S.indicators), ID_IND, L"Show indicators for open apps");
    {
        static const wchar_t* names[3] = {L"Indicator: dot", L"Indicator: highlight", L"Indicator: blue line"};
        for (int i = 0; i < 3; i++)
            AppendMenuW(look, chk(S.indicatorStyle == i) | (S.indicators ? 0 : MF_GRAYED), ID_INDSTYLE0 + i, names[i]);
    }
    {
        static const wchar_t* names[3] = {L"Minimized: normal", L"Minimized: dimmed", L"Minimized: blue line"};
        for (int i = 0; i < 3; i++) AppendMenuW(look, chk(S.minimizedStyle == i), ID_MINSTYLE0 + i, names[i]);
    }
    HMENU beh = CreatePopupMenu();
    AppendMenuW(beh, chk(S.bounce), ID_BOUNCE, L"Animate opening apps");
    AppendMenuW(beh, chk(S.attention), ID_ATTN, L"Bounce apps that need attention");
    AppendMenuW(beh, chk(S.clickMinimize), ID_CLICKMIN, L"Minimize the active app on click");
    AppendMenuW(beh, chk(S.hideFullscreen), ID_FULLHIDE, L"Hide the dock in full-screen apps");
    AppendMenuW(beh, chk(S.minimizedPreviews), ID_MINPREV, L"Show minimized windows as previews on the right");
    HMENU con = CreatePopupMenu();
    UINT lk = S.locked ? MF_GRAYED : 0;
    AppendMenuW(con, chk(S.showLauncher), ID_SHOWLAUNCH, L"Start button");
    AppendMenuW(con, chk(S.showRunning), ID_SHOWRUN, L"Unpinned open apps");
    AppendMenuW(con, chk(S.separateApps), ID_SEPAPPS, L"Separate pinned and open apps with a divider");
    AppendMenuW(con, chk(HasDownloadsStack()) | lk, ID_DOWNLOADS, L"Downloads stack");
    AppendMenuW(con, chk(S.showTrash), ID_SHOWTRASH, L"Recycle Bin");
    AppendMenuW(con, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(con, MF_STRING | lk, ID_SPACER, L"Add spacer");

    AppendMenuW(d, MF_STRING, ID_OPENSETTINGS, L"Settings…");
    AppendMenuW(d, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(d, MF_POPUP, (UINT_PTR)sz, L"Icon size");
    AppendMenuW(d, MF_POPUP, (UINT_PTR)mg, L"Magnification");
    AppendMenuW(d, MF_POPUP, (UINT_PTR)ps, L"Position on screen");
    AppendMenuW(d, MF_POPUP, (UINT_PTR)look, L"Appearance");
    AppendMenuW(d, MF_POPUP, (UINT_PTR)beh, L"Behavior");
    AppendMenuW(d, MF_POPUP, (UINT_PTR)con, L"Content");
    HMENU mb = CreatePopupMenu();
    AppendMenuW(mb, chk(S.showMenubar), ID_BAR, L"Show the top menu bar");
    AppendMenuW(mb, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mb, chk(S.clockDate) | (S.showMenubar ? 0 : MF_GRAYED), ID_BARDATE, L"Show date in clock");
    AppendMenuW(mb, chk(S.clockSeconds) | (S.showMenubar ? 0 : MF_GRAYED), ID_BARSEC, L"Show seconds");
    AppendMenuW(mb, chk(S.trayIcons) | (S.showMenubar ? 0 : MF_GRAYED), ID_BARTRAY, L"Show background app icons");
    AppendMenuW(d, MF_POPUP, (UINT_PTR)mb, L"Menu bar");
    AppendMenuW(d, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(d, chk(S.autohide), ID_AUTOHIDE, L"Automatically hide and show the dock");
    AppendMenuW(d, MF_POPUP | (S.autohide ? 0 : MF_GRAYED), (UINT_PTR)hd, L"Hiding options");
    AppendMenuW(d, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(d, chk(S.locked), ID_LOCK, L"Lock the dock");
    AppendMenuW(d, MF_STRING | (S.hideTaskbar ? MF_CHECKED : 0), ID_HIDETB, L"Hide the Windows taskbar");
    AppendMenuW(d, MF_STRING | (S.reserve ? MF_CHECKED : 0) | (S.autohide ? MF_GRAYED : 0), ID_RESERVE, L"Keep windows above the dock");
    AppendMenuW(d, MF_STRING | (IsAutostart() ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with Windows");
    AppendMenuW(d, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(d, MF_STRING, ID_EDITINI, L"Open settings file");
    AppendMenuW(d, MF_STRING, ID_RELOAD, L"Reload settings");
    AppendMenuW(d, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(d, MF_STRING, ID_QUIT, L"Quit Just Better TaskBar");
    return d;
}
static void ShowMenu(int idx) {
    Item it;
    bool has = idx >= 0 && idx < (int)g_items.size();
    if (has) it = g_items[idx];
    HMENU m = CreatePopupMenu();
    std::vector<HWND> wins;
    UINT lockFlag = S.locked ? MF_GRAYED : 0;
    if (has && it.kind == K_APP) {
        for (size_t k = 0; k < it.wins.size() && k < 15; k++) {
            wchar_t t[120] = {};
            GetWindowTextW(it.wins[k], t, 120);
            std::wstring s;
            for (wchar_t* p = t; *p; p++) { s += *p; if (*p == L'&') s += L'&'; }
            AppendMenuW(m, MF_STRING, ID_WIN0 + k, s.c_str());
            wins.push_back(it.wins[k]);
        }
        if (!wins.empty()) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        if (!it.launch.empty()) {
            AppendMenuW(m, MF_STRING, ID_OPEN, it.wins.empty() ? L"Open" : L"New Window");
            AppendMenuW(m, MF_STRING | lockFlag, ID_PIN, it.pinned ? L"Remove from Dock" : L"Keep in Dock");
        }
        if (!it.wins.empty()) AppendMenuW(m, MF_STRING, ID_CLOSEALL, it.wins.size() > 1 ? L"Close All Windows" : L"Close");
        if (!it.pinned && it.key.rfind(L"hwnd:", 0) != 0) AppendMenuW(m, MF_STRING, ID_HIDEAPP, L"Hide from dock");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildDockMenu(), L"Dock settings");
    } else if (has && it.kind == K_STACK) {
        AppendMenuW(m, MF_STRING, ID_STACKOPEN, L"Show contents");
        AppendMenuW(m, MF_STRING, ID_FOLDEROPEN, L"Open folder");
        AppendMenuW(m, MF_STRING | lockFlag, ID_REMOVE, L"Remove from Dock");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildDockMenu(), L"Dock settings");
    } else if (has && it.kind == K_MINWIN && !it.wins.empty()) {
        AppendMenuW(m, MF_STRING, ID_WIN0, L"Restore");
        wins.push_back(it.wins[0]);
        AppendMenuW(m, MF_STRING, ID_CLOSEALL, L"Close Window");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildDockMenu(), L"Dock settings");
    } else if (has && it.kind == K_SPACER) {
        AppendMenuW(m, MF_STRING | lockFlag, ID_REMOVE, L"Remove spacer");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildDockMenu(), L"Dock settings");
    } else if (has && it.kind == K_TRASH) {
        AppendMenuW(m, MF_STRING, ID_OPENTRASH, L"Open");
        AppendMenuW(m, MF_STRING | (g_trashFull ? 0 : MF_GRAYED), ID_EMPTYTRASH, L"Empty Recycle Bin");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildDockMenu(), L"Dock settings");
    } else {
        DestroyMenu(m);
        m = BuildDockMenu();
    }
    int cmd = RunMenu(m, has ? idx : -1);
    DestroyMenu(m);

    if (cmd >= ID_WIN0 && cmd < ID_WIN0 + (int)wins.size()) { Activate(wins[cmd - ID_WIN0]); return; }
    switch (cmd) {
    case ID_OPEN: LaunchItem(it); return;
    case ID_PIN:
        if (it.pinned) RemovePin(it.key);
        else { AddPin(it.launch, it.name); SaveIni(); Refresh(true); }
        return;
    case ID_REMOVE: RemovePin(it.key); return;
    case ID_STACKOPEN: ShowStack(idx); return;
    case ID_FOLDEROPEN: OpenFolder(it.launch); return;
    case ID_CLOSEALL: for (HWND w : it.wins) PostMessageW(w, WM_CLOSE, 0, 0); return;
    case ID_OPENTRASH: OpenTrash(); return;
    case ID_EMPTYTRASH: SHEmptyRecycleBinW(g_hwnd, nullptr, 0); UpdateTrash(); return;
    case ID_HIDEAPP:
        if (!IsHiddenApp(it.key)) g_hiddenApps.push_back(it.key);
        if (!g_names.count(it.key)) g_names[it.key] = it.name;
        SaveIni();
        Refresh(true);
        RenderBar();
        return;
    }
    DockCommand(cmd);
}
static void DockCommand(int cmd) {
    if (cmd >= ID_INDSTYLE0 && cmd < ID_INDSTYLE0 + 3) { S.indicatorStyle = cmd - ID_INDSTYLE0; SaveIni(); Refresh(true); return; }
    if (cmd >= ID_MINSTYLE0 && cmd < ID_MINSTYLE0 + 3) { S.minimizedStyle = cmd - ID_MINSTYLE0; SaveIni(); Refresh(true); return; }
    if (cmd == ID_MINPREV) { S.minimizedPreviews = !S.minimizedPreviews; SaveIni(); Refresh(true); return; }
    if (cmd == ID_SEPAPPS) { S.separateApps = !S.separateApps; Refresh(true); SaveIni(); return; }
    if (cmd >= ID_SIZE0 && cmd < ID_SIZE0 + 4) { S.size = kSizes[cmd - ID_SIZE0]; ApplySettingsChange(); return; }
    if (cmd >= ID_MAG0 && cmd < ID_MAG0 + 6) { S.magnify = kMags[cmd - ID_MAG0]; ApplySettingsChange(); return; }
    if (cmd >= ID_POS0 && cmd < ID_POS0 + 3) { S.position = cmd - ID_POS0; g_mx = -1; ApplySettingsChange(); return; }
    if (cmd >= ID_DELAY0 && cmd < ID_DELAY0 + 4) { S.hideDelay = kDelays[cmd - ID_DELAY0]; SaveIni(); return; }
    if (cmd >= ID_ANIM0 && cmd < ID_ANIM0 + 4) { S.hideAnim = kAnims[cmd - ID_ANIM0]; SaveIni(); return; }
    if (cmd >= ID_THEME0 && cmd < ID_THEME0 + 3) { S.theme = cmd - ID_THEME0; SaveIni(); Render(); RenderBar(); return; }
    if (cmd >= ID_OPAC0 && cmd < ID_OPAC0 + 4) { S.opacity = kOpacs[cmd - ID_OPAC0]; SaveIni(); Render(); RenderBar(); return; }
    switch (cmd) {
    case ID_OPENSETTINGS: OpenSettings(); break;
    case ID_AUTOHIDE:
        S.autohide = !S.autohide;
        g_showTarget = 1;
        ApplySettingsChange();
        break;
    case ID_IND: S.indicators = !S.indicators; SaveIni(); Refresh(true); break;
    case ID_BOUNCE: S.bounce = !S.bounce; SaveIni(); break;
    case ID_SPACER: {
        int at = 0;
        for (size_t i = 0; i < g_pins.size(); i++) if (!g_pins[i].stack) at = (int)i + 1;
        AddPin(L"#spacer", L"", at);
        SaveIni();
        Refresh(true);
        break;
    }
    case ID_LOCK: S.locked = !S.locked; SaveIni(); break;
    case ID_GLASS: S.glass = !S.glass; SaveIni(); g_glass.rc = g_barGlass.rc = RECT{}; Render(); RenderBar(); break;
    case ID_GLASSSTYLE0:
    case ID_GLASSSTYLE0 + 1:
    case ID_GLASSSTYLE0 + 2:
        S.glassStyle = cmd - ID_GLASSSTYLE0;
        g_liquidFailed = false;
        SaveIni();
        Render();
        Render();
        break;
    case ID_TIPS: S.tooltips = !S.tooltips; SaveIni(); break;
    case ID_BAR: S.showMenubar = !S.showMenubar; SaveIni(); ApplyShell(); TrayEnable(S.showMenubar && S.trayIcons); RenderBar(); break;
    case ID_BARTRAY: S.trayIcons = !S.trayIcons; SaveIni(); TrayEnable(S.showMenubar && S.trayIcons); RenderBar(); break;
    case ID_BARDATE: S.clockDate = !S.clockDate; SaveIni(); g_clockText = ClockText(); RenderBar(); break;
    case ID_BARSEC: S.clockSeconds = !S.clockSeconds; SaveIni(); g_clockText = ClockText(); RenderBar(); break;
    case ID_ATTN: S.attention = !S.attention; SaveIni(); break;
    case ID_CLICKMIN: S.clickMinimize = !S.clickMinimize; SaveIni(); break;
    case ID_FULLHIDE:
        S.hideFullscreen = !S.hideFullscreen;
        SaveIni();
        if (!S.hideFullscreen && g_hiddenFS) { g_hiddenFS = false; ShowWindow(g_hwnd, SW_SHOWNOACTIVATE); Render(); }
        break;
    case ID_SHOWLAUNCH: S.showLauncher = !S.showLauncher; SaveIni(); Refresh(true); break;
    case ID_SHOWRUN: S.showRunning = !S.showRunning; SaveIni(); Refresh(true); break;
    case ID_SHOWTRASH: S.showTrash = !S.showTrash; SaveIni(); Refresh(true); break;
    case ID_DOWNLOADS:
        if (HasDownloadsStack()) RemovePin(Lower(DownloadsPath()));
        else { AddDownloadsStack(); SaveIni(); Refresh(true); }
        break;
    case ID_HIDETB:
        S.hideTaskbar = !S.hideTaskbar;
        if (!S.hideTaskbar) RestoreTaskbar();
        ApplySettingsChange();
        break;
    case ID_RESERVE: S.reserve = !S.reserve; ApplySettingsChange(); break;
    case ID_AUTOSTART: SetAutostart(!IsAutostart()); break;
    case ID_EDITINI: ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + g_iniPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL); break;
    case ID_RELOAD: {
        bool wasHidden = S.hideTaskbar;
        LoadIni();
        if (wasHidden && !S.hideTaskbar) RestoreTaskbar();
        ApplySettingsChange();
        break;
    }
    case ID_QUIT: DestroyWindow(g_hwnd); break;
    }
}
static void OnDrop(HDROP hd) {
    POINT pt;
    DragQueryPoint(hd, &pt);
    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> files;
    for (UINT k = 0; k < n; k++) {
        wchar_t buf[MAX_PATH * 4];
        if (DragQueryFileW(hd, k, buf, ARRAYSIZE(buf))) files.push_back(buf);
    }
    DragFinish(hd);
    if (files.empty()) return;
    int i = HitTest(Horizontal() ? (float)pt.x : (float)pt.y);
    if (i >= 0 && g_items[i].kind == K_TRASH) {
        std::wstring multi;
        for (auto& f : files) { multi += f; multi.push_back(L'\0'); }
        multi.push_back(L'\0');
        SHFILEOPSTRUCTW op{};
        op.hwnd = g_hwnd;
        op.wFunc = FO_DELETE;
        op.pFrom = multi.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_WANTNUKEWARNING;
        SHFileOperationW(&op);
        UpdateTrash();
        return;
    }
    if (i >= 0 && g_items[i].kind == K_STACK) {
        std::wstring multi, to = g_items[i].launch;
        for (auto& f : files) { multi += f; multi.push_back(L'\0'); }
        multi.push_back(L'\0');
        to.push_back(L'\0');
        SHFILEOPSTRUCTW op{};
        op.hwnd = g_hwnd;
        op.wFunc = FO_MOVE;
        op.pFrom = multi.c_str();
        op.pTo = to.c_str();
        op.fFlags = FOF_ALLOWUNDO;
        SHFileOperationW(&op);
        return;
    }
    bool pinned = false;
    if (!S.locked)
        for (auto& f : files) {
            std::wstring lf = Lower(f);
            if (EndsWith(lf, L".exe") || EndsWith(lf, L".lnk")) {
                int at = 0;
                for (size_t k = 0; k < g_pins.size(); k++) if (!g_pins[k].stack) at = (int)k + 1;
                pinned |= AddPin(f, L"", at);
            } else if (IsDir(f)) {
                pinned |= AddPin(f, L"");
            }
        }
    if (pinned) { SaveIni(); Refresh(true); return; }
    if (i >= 0 && g_items[i].kind == K_APP && AumidFromLaunch(g_items[i].launch).empty() && !g_items[i].launch.empty()) {
        std::wstring exe = g_items[i].key.rfind(L"hwnd:", 0) == 0 ? g_items[i].launch : g_items[i].key;
        for (auto& f : files) LaunchPath(exe, (L"\"" + f + L"\"").c_str());
        StartBounce(g_items[i].key, 2);
    }
}
static bool ForegroundIsFullscreen() {
    QUERY_USER_NOTIFICATION_STATE st;
    if (SUCCEEDED(SHQueryUserNotificationState(&st)) && (st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE)) return true;
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_hwnd || IsZoomed(fg)) return false;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) return false;
    MONITORINFO pm = PrimaryMon(), mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONULL), &mi) || !EqualRect(&mi.rcMonitor, &pm.rcMonitor)) return false;
    RECT r;
    GetWindowRect(fg, &r);
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}
static void Poll() {
    g_pollCount++;
    ApplyShell();
    bool fs = S.hideFullscreen && ForegroundIsFullscreen();
    if (fs != g_hiddenFS) {
        g_hiddenFS = fs;
        ShowWindow(g_hwnd, fs ? SW_HIDE : SW_SHOWNOACTIVATE);
        if (fs) HideGlass();
        else Render();
        RenderBar();
    }
    BarPoll();
    if (!fs && !g_dragging) {
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (g_barWnd && IsWindowVisible(g_barWnd) && !g_menuOpen) {
            SetWindowPos(g_barWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (g_barGlass.wnd && IsWindowVisible(g_barGlass.wnd))
                SetWindowPos(g_barGlass.wnd, g_barWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (g_glass.wnd && IsWindowVisible(g_glass.wnd))
            SetWindowPos(g_glass.wnd, g_hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (g_pollCount % 3 == 0) UpdateTrash();
    if (!g_dragging) Refresh();
}

// ---------------------------------------------------------------- dragging
static bool CanDrag(int idx) {
    if (S.locked || idx < 0 || idx >= (int)g_items.size()) return false;
    auto& it = g_items[idx];
    if (it.kind == K_APP && !it.pinned) return S.separateApps ? !it.launch.empty() : true;
    return it.pinned && (it.kind == K_APP || it.kind == K_SPACER || it.kind == K_STACK);
}
static void StartDrag(int idx) {
    std::wstring key = g_items[idx].key;
    if (!g_items[idx].pinned && S.separateApps) {
        int at = 0;
        for (size_t i = 0; i < g_pins.size(); i++) if (!g_pins[i].stack) at = (int)i + 1;
        if (!AddPin(g_items[idx].launch, g_items[idx].name, at)) return;
        key = g_pins[at].key;
    }
    g_dragging = true;
    g_dragRemove = false;
    g_dragKey = key;
    g_dragIcon = g_items[idx].kind == K_SPACER ? nullptr : g_items[idx].icon;
    g_pressIdx = -1;
    g_hover = g_hoverTarget = 0;
    Refresh(true);
    ShowWindow(g_dragWnd, SW_SHOWNOACTIVATE);
    UpdateDragWnd();
}
static void DragReorder(float mainPos) {
    auto di = std::find_if(g_pins.begin(), g_pins.end(), [](const Pin& p) { return p.key == g_dragKey; });
    bool stack = di != g_pins.end() && di->stack;
    if (!S.separateApps && !stack) {
        int pos = 0;
        for (auto& it : g_items)
            if ((it.kind == K_APP || it.kind == K_SPACER) && it.key != g_dragKey && it.x + it.w / 2 < mainPos) pos++;
        std::vector<std::wstring> order;
        for (auto& k : g_appOrder) if (k != g_dragKey) order.push_back(k);
        order.insert(order.begin() + std::min(pos, (int)order.size()), g_dragKey);
        if (order != g_appOrder) { g_appOrder.swap(order); Refresh(true); }
        return;
    }
    if (di == g_pins.end()) return;
    int idx = 0;
    for (auto& it : g_items) {
        if (!it.pinned || it.key == g_dragKey || (it.kind == K_STACK) != stack) continue;
        if (it.x + it.w / 2 < mainPos) idx++;
    }
    Pin d = *di;
    std::vector<Pin> out;
    int seen = 0;
    bool ins = false;
    for (auto& p : g_pins) {
        if (p.key == g_dragKey) continue;
        if (!ins && p.stack == stack && seen == idx) { out.push_back(d); ins = true; }
        if (p.stack == stack) seen++;
        out.push_back(p);
    }
    if (!ins) out.push_back(d);
    bool changed = false;
    for (size_t i = 0; i < out.size(); i++) if (out[i].key != g_pins[i].key) { changed = true; break; }
    if (changed) { g_pins.swap(out); Refresh(true); }
}
static void DragMove() {
    POINT p;
    GetCursorPos(&p);
    Geo g = G();
    RECT e = EdgeRect();
    float cross = S.position == 0 ? (float)(e.bottom - p.y) : S.position == 1 ? (float)(p.x - e.left) : (float)(e.right - p.x);
    bool isPin = std::any_of(g_pins.begin(), g_pins.end(), [](const Pin& q) { return q.key == g_dragKey; });
    bool rem = isPin && cross > g.margin + g.shelf + g.base * 0.9f;
    if (rem != g_dragRemove) { g_dragRemove = rem; Refresh(true); }
    if (!rem) {
        POINT c = p;
        ScreenToClient(g_hwnd, &c);
        DragReorder(Horizontal() ? (float)c.x : (float)c.y);
    }
    UpdateDragWnd();
}
static void EndDrag() {
    if (!g_dragging) return;
    g_dragging = false;
    ShowWindow(g_dragWnd, SW_HIDE);
    if (g_dragRemove)
        g_pins.erase(std::remove_if(g_pins.begin(), g_pins.end(), [](const Pin& p) { return p.key == g_dragKey; }), g_pins.end());
    g_dragRemove = false;
    g_dragKey.clear();
    g_dragIcon = nullptr;
    SaveIni();
    Refresh(true);
}
static void FinishPress(POINT up, bool shift) {
    POINT c = up;
    ScreenToClient(g_hwnd, &c);
    int i = HitTest(Horizontal() ? (float)c.x : (float)c.y), target = -1;
    bool still = std::abs(up.x - g_pressPt.x) <= 12 && std::abs(up.y - g_pressPt.y) <= 12;
    if (!g_pressKey.empty()) {
        if (i >= 0 && g_items[i].key == g_pressKey) target = i;
        else if (still)
            for (size_t k = 0; k < g_items.size(); k++)
                if (g_items[k].key == g_pressKey) { target = (int)k; break; }
    }
    g_down = false;
    g_pressIdx = -1;
    g_pressKey.clear();
    if (target >= 0) OnClick(target, shift);
    Render();
}
// The dock is never the foreground window, so Windows stops sending mouse messages once the
// cursor is over a transparent part (magnified icons shrink when a drag starts).
// So while the button is held we poll the cursor and button state ourselves.
static void DragPoll() {
    if (!g_down && !g_dragging && !g_resizing) { KillTimer(g_hwnd, T_DRAG); return; }
    POINT p;
    GetCursorPos(&p);
    int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (!pressed) {
        KillTimer(g_hwnd, T_DRAG);
        if (g_dragging) { g_down = false; EndDrag(); }
        else if (g_resizing) { g_resizing = false; ApplySettingsChange(); }
        else if (g_down) FinishPress(p, false);
        ReleaseCapture();
        return;
    }
    if (g_resizing) {
        int d = S.position == 0 ? g_resizeStart.y - p.y : S.position == 1 ? p.x - g_resizeStart.x : g_resizeStart.x - p.x;
        int ns = std::clamp(g_resizeStartSize + (int)std::lround(d / g_scale), 24, 128);
        if (ns != S.size) { S.size = ns; Render(); }
        return;
    }
    if (g_dragging) { DragMove(); return; }
    if (g_down && std::max(std::abs(p.x - g_pressPt.x), std::abs(p.y - g_pressPt.y)) > 6 * g_scale)
        for (size_t k = 0; k < g_items.size(); k++)
            if (g_items[k].key == g_pressKey && CanDrag((int)k)) { StartDrag((int)k); return; }
}

// ---------------------------------------------------------------- menu bar
enum BarKind { B_LOGO, B_APP, B_MENU, B_SEARCH, B_NET, B_VOL, B_BATT, B_CC, B_CLOCK, B_TRAY };
struct BarItem { BarKind kind; std::wstring text; int menuIndex = -1; float x = 0, w = 0; };
Surface g_barSurf;
std::vector<BarItem> g_barItems;
int g_barHover = -1, g_barOpen = -1, g_barNext = -1, g_menuClosedIdx = -1;
ULONGLONG g_menuClosedAt = 0;
HWND g_barApp = nullptr;
std::wstring g_barAppName = L"Desktop", g_barAppKey, g_barAppLaunch;
std::vector<std::pair<std::wstring, int>> g_barMenus;
bool g_barTracking = false, g_charging = false, g_mute = false;
int g_netState = 0, g_volume = -1, g_batt = -1;
IAudioEndpointVolume* g_vol = nullptr;
HHOOK g_menuHook = nullptr;
std::wstring g_barSig;
enum { ID_ABOUT = 500, ID_SETTINGS, ID_TASKMGR, ID_SLEEP, ID_RESTART, ID_SHUTDOWN, ID_LOCKPC, ID_LOGOFF, ID_START,
       ID_APPNEW = 600, ID_APPHIDE, ID_APPHIDEOTHERS, ID_APPSHOWALL, ID_APPZOOM, ID_APPQUIT, ID_SHOWDESKTOP };

static bool IsOurWindow(HWND h) {
    return h && (h == g_hwnd || h == g_barWnd || h == g_dragWnd || h == g_glass.wnd || h == g_barGlass.wnd);
}
static std::wstring CleanMenuText(const wchar_t* s) {
    std::wstring r;
    for (const wchar_t* p = s; *p && *p != L'\t'; p++) {
        if (*p == L'&') { if (p[1] == L'&') { r += L'&'; p++; } continue; }
        r += *p;
    }
    return Trim(r);
}
static void BarTrackForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    HWND root = GetAncestor(fg, GA_ROOTOWNER);
    if (!root) root = fg;
    if (IsOurWindow(root) || IsOurWindow(fg)) return;
    wchar_t cls[64] = {};
    GetClassNameW(root, cls, 64);
    if (!wcscmp(cls, L"#32768") || !wcscmp(cls, L"Windows.UI.Core.CoreWindow") || !wcscmp(cls, L"XamlExplorerHostIslandWindow")) return;
    bool desktop = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd");
    HWND app = desktop ? nullptr : root;
    std::vector<std::pair<std::wstring, int>> menus;
    if (app) {
        HMENU m = GetMenu(app);
        if (m && IsMenu(m)) {
            int n = GetMenuItemCount(m);
            for (int i = 0; i < n && i < 20; i++) {
                if (!GetSubMenu(m, i)) continue;
                wchar_t b[128] = {};
                GetMenuStringW(m, i, b, 128, MF_BYPOSITION);
                std::wstring t = CleanMenuText(b);
                if (!t.empty()) menus.push_back({t, i});
            }
        }
    }
    if (app == g_barApp && menus == g_barMenus && !g_barItems.empty()) return;
    g_barApp = app;
    g_barMenus = menus;
    g_barAppKey.clear();
    g_barAppLaunch.clear();
    if (!app) {
        g_barAppName = L"Desktop";
    } else {
        WinId id = Identify(app);
        g_barAppKey = id.key;
        g_barAppLaunch = id.launch;
        std::wstring n;
        for (auto& p : g_pins) if (p.key == id.key && !p.name.empty()) n = p.name;
        if (n.empty()) {
            auto f = g_names.find(id.key);
            if (f != g_names.end()) n = f->second;
        }
        if (n.empty()) n = !id.aumid.empty() ? AppName(id.aumid) : (!id.path.empty() ? FileDesc(id.path) : L"");
        if (n.empty()) {
            wchar_t t[256] = {};
            GetWindowTextW(app, t, 256);
            n = t;
        }
        if (id.key.rfind(L"hwnd:", 0) != 0) g_names[id.key] = n;
        g_barAppName = n.size() > 40 ? n.substr(0, 38) + L"…" : n;
    }
    RenderBar();
}
static std::wstring ClockText() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t d[64] = L"", t[32] = L"";
    if (S.clockDate) GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd d MMM", d, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, S.clockSeconds ? L"HH:mm:ss" : L"HH:mm", t, 32);
    return S.clockDate ? std::wstring(d) + L"   " + t : std::wstring(t);
}
static void UpdateVolume() {
    if (g_vol) { g_vol->Release(); g_vol = nullptr; }
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) {
        IMMDevice* dev = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev))) {
            dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&g_vol);
            dev->Release();
        }
        en->Release();
    }
    float v = 0;
    BOOL mu = FALSE;
    if (!g_vol || FAILED(g_vol->GetMasterVolumeLevelScalar(&v)) || FAILED(g_vol->GetMute(&mu))) { g_volume = -1; return; }
    g_volume = (int)std::lround(v * 100);
    g_mute = mu != FALSE;
}
static void UpdateNet() {
    ULONG sz = 16384;
    std::vector<BYTE> buf(sz);
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &sz);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(sz);
        r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &sz);
    }
    int st = 0;
    if (r == NO_ERROR)
        for (auto a = (IP_ADAPTER_ADDRESSES*)buf.data(); a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || !a->FirstGatewayAddress) continue;
            if (a->IfType == IF_TYPE_IEEE80211) { st = 2; break; }
            st = 1;
        }
    g_netState = st;
}
static void UpdateBattery() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps) || ps.BatteryFlag == 128 || ps.BatteryFlag == 255 || ps.BatteryLifePercent > 100) { g_batt = -1; return; }
    g_batt = ps.BatteryLifePercent;
    g_charging = ps.ACLineStatus == 1;
}
// ---- system tray (Steam, Discord… icons)
// Apps send WM_COPYDATA to "Shell_TrayWnd" through Shell_NotifyIcon. Our window is above Explorer's
// in z-order, so messages reach us first; we record the icon and forward the message to Explorer unchanged.
struct NID32 {
    DWORD cbSize; DWORD hWnd; UINT uID; UINT uFlags; UINT uCallbackMessage; DWORD hIcon; WCHAR szTip[128];
    DWORD dwState; DWORD dwStateMask; WCHAR szInfo[256]; UINT uVersion; WCHAR szInfoTitle[64]; DWORD dwInfoFlags;
    GUID guidItem; DWORD hBalloonIcon;
};
struct TrayData { DWORD signature; DWORD message; NID32 nid; };
struct TrayIcon {
    HWND hwnd = nullptr; UINT id = 0; GUID guid{}; bool hasGuid = false; std::wstring appKey;
    UINT cbMsg = 0, version = 0; std::wstring tip; Bitmap* bmp = nullptr; bool hidden = false; DWORD pid = 0;
};
std::vector<TrayIcon> g_tray;

static int FindTray(const NID32& n) {
    bool byGuid = (n.uFlags & NIF_GUID) != 0;
    for (size_t i = 0; i < g_tray.size(); i++) {
        if (byGuid ? (g_tray[i].hasGuid && IsEqualGUID(g_tray[i].guid, n.guidItem))
                   : (g_tray[i].hwnd == (HWND)(ULONG_PTR)n.hWnd && g_tray[i].id == n.uID))
            return (int)i;
    }
    return -1;
}
static void TrayApply(TrayIcon& t, const NID32& n) {
    if (n.uFlags & NIF_MESSAGE) t.cbMsg = n.uCallbackMessage;
    if (n.uFlags & NIF_TIP) t.tip.assign(n.szTip, wcsnlen(n.szTip, 128));
    if ((n.uFlags & NIF_STATE) && (n.dwStateMask & NIS_HIDDEN)) t.hidden = (n.dwState & NIS_HIDDEN) != 0;
    if ((n.uFlags & NIF_ICON) && n.hIcon) {
        if (Bitmap* raw = HIconToBitmap((HICON)(ULONG_PTR)n.hIcon)) {
            delete t.bmp;
            t.bmp = ScaleTo(raw, (int)std::round(16 * g_scale));
            delete raw;
        }
    }
}
static LRESULT TrayCopyData(WPARAM w, COPYDATASTRUCT* cds) {
    BOOL ok = TRUE;
    if (cds && cds->dwData == 1 && cds->lpData && cds->cbData >= offsetof(TrayData, nid) + offsetof(NID32, szTip)) {
        auto* td = (TrayData*)cds->lpData;
        NID32 n{};
        memcpy(&n, &td->nid, std::min<size_t>(sizeof n, cds->cbData - offsetof(TrayData, nid)));
        int i = FindTray(n);
        switch (td->message) {
        case NIM_ADD:
            if (i < 0) {
                TrayIcon t;
                t.hwnd = (HWND)(ULONG_PTR)n.hWnd;
                t.id = n.uID;
                if (n.uFlags & NIF_GUID) { t.guid = n.guidItem; t.hasGuid = true; }
                GetWindowThreadProcessId(t.hwnd, &t.pid);
                t.appKey = KeyForPid(t.pid);
                TrayApply(t, n);
                g_tray.push_back(t);
            } else {
                TrayApply(g_tray[i], n);
            }
            break;
        case NIM_MODIFY:
            if (i >= 0) TrayApply(g_tray[i], n);
            else ok = FALSE;
            break;
        case NIM_DELETE:
            if (i >= 0) { delete g_tray[i].bmp; g_tray.erase(g_tray.begin() + i); }
            else ok = FALSE;
            break;
        case NIM_SETVERSION:
            if (i >= 0) g_tray[i].version = n.uVersion;
            break;
        }
        RenderBar();
    }
    HWND ex = ExplorerTray();
    DWORD_PTR res = 0;
    bool fwd = ex && SendMessageTimeoutW(ex, WM_COPYDATA, w, (LPARAM)cds, SMTO_ABORTIFHUNG, 3000, &res);
    if (cds && cds->dwData == 1) return ok;
    return fwd ? (LRESULT)res : 0;
}
static LRESULT CALLBACK TrayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COPYDATA) return TrayCopyData(w, (COPYDATASTRUCT*)l);
    if (m >= WM_USER && m < 0x8000) {
        HWND ex = ExplorerTray();
        DWORD_PTR r = 0;
        if (ex) SendMessageTimeoutW(ex, m, w, l, SMTO_ABORTIFHUNG, 1000, &r);
        return (LRESULT)r;
    }
    return DefWindowProcW(h, m, w, l);
}
static void TraySync() {
    if (!g_trayWnd) return;
    HWND ex = ExplorerTray();
    RECT r{};
    if (ex) GetWindowRect(ex, &r);
    SetWindowPos(g_trayWnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE);
    bool changed = false;
    for (size_t i = 0; i < g_tray.size();) {
        if (!IsWindow(g_tray[i].hwnd)) { delete g_tray[i].bmp; g_tray.erase(g_tray.begin() + i); changed = true; }
        else i++;
    }
    if (changed) RenderBar();
}
static void TrayEnable(bool on) {
    if (on && !g_trayWnd) {
        WNDCLASSEXW wt{sizeof wt};
        wt.lpfnWndProc = TrayProc;
        wt.hInstance = g_hinst;
        wt.lpszClassName = L"Shell_TrayWnd";
        RegisterClassExW(&wt);
        g_trayWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Shell_TrayWnd", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, g_hinst, nullptr);
        if (!g_trayWnd) return;
        ChangeWindowMessageFilterEx(g_trayWnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
        TraySync();
    } else if (!on && g_trayWnd) {
        DestroyWindow(g_trayWnd);
        g_trayWnd = nullptr;
        for (auto& t : g_tray) delete t.bmp;
        g_tray.clear();
    } else {
        return;
    }
    g_ignoreTbCreatedUntil = GetTickCount64() + 3000;
    PostMessageW(HWND_BROADCAST, g_taskbarCreatedMsg, 0, 0);
}
static void TraySend(const TrayIcon& t, UINT msg) {
    if (!t.cbMsg || !IsWindow(t.hwnd)) return;
    POINT p;
    GetCursorPos(&p);
    WPARAM w;
    LPARAM l;
    if (t.version >= 4) { w = MAKEWPARAM(p.x, p.y); l = MAKELPARAM(msg, t.id); }
    else { w = t.id; l = msg; }
    SendNotifyMessageW(t.hwnd, t.cbMsg, w, l);
}
static void TrayClick(int barIdx, int kind) {
    if (barIdx < 0 || barIdx >= (int)g_barItems.size()) return;
    int ti = g_barItems[barIdx].menuIndex;
    if (ti < 0 || ti >= (int)g_tray.size()) return;
    TrayIcon t = g_tray[ti];
    AllowSetForegroundWindow(t.pid ? t.pid : ASFW_ANY);
    if (kind == 0) {
        TraySend(t, WM_LBUTTONDOWN);
        TraySend(t, WM_LBUTTONUP);
        if (t.version >= 4) TraySend(t, NIN_SELECT);
    } else if (kind == 1) {
        TraySend(t, WM_RBUTTONDOWN);
        TraySend(t, WM_RBUTTONUP);
        if (t.version >= 4) TraySend(t, WM_CONTEXTMENU);
    } else {
        TraySend(t, WM_LBUTTONDBLCLK);
    }
}

static void BuildBarItems() {
    g_barItems.clear();
    g_barItems.push_back({B_LOGO, L""});
    g_barItems.push_back({B_APP, g_barAppName});
    for (auto& m : g_barMenus) g_barItems.push_back({B_MENU, m.first, m.second});
    for (size_t i = 0; i < g_tray.size(); i++)
        if (!g_tray[i].hidden && g_tray[i].bmp && !IsHiddenApp(g_tray[i].appKey)) g_barItems.push_back({B_TRAY, g_tray[i].tip, (int)i});
    g_barItems.push_back({B_SEARCH, L""});
    g_barItems.push_back({B_NET, L""});
    g_barItems.push_back({B_VOL, L""});
    if (g_batt >= 0) g_barItems.push_back({B_BATT, L""});
    g_barItems.push_back({B_CC, L""});
    g_barItems.push_back({B_CLOCK, g_clockText});
}
static bool BarIsMenu(int i) {
    return i >= 0 && i < (int)g_barItems.size() && (g_barItems[i].kind == B_LOGO || g_barItems[i].kind == B_APP || g_barItems[i].kind == B_MENU);
}
static int BarHit(float x) {
    for (size_t i = 0; i < g_barItems.size(); i++)
        if (x >= g_barItems[i].x && x < g_barItems[i].x + g_barItems[i].w) return (int)i;
    return -1;
}
static const wchar_t* BarGlyph(const BarItem& it, wchar_t* buf) {
    wchar_t c = 0;
    switch (it.kind) {
    case B_SEARCH: c = 0xE721; break;
    case B_NET: c = g_netState == 2 ? 0xE701 : g_netState == 1 ? 0xE839 : 0xEB55; break;
    case B_VOL: c = (g_mute || g_volume == 0) ? 0xE74F : 0xE767; break;
    case B_BATT: c = g_charging ? 0xEBB5 : (wchar_t)(0xEBA0 + std::clamp((g_batt + 5) / 10, 0, 10)); break;
    case B_CC: c = 0xE9E9; break;
    default: break;
    }
    buf[0] = c;
    buf[1] = 0;
    return buf;
}
static void RenderBar() {
    if (!g_barWnd) return;
    if (!S.showMenubar || g_hiddenFS) {
        if (IsWindowVisible(g_barWnd)) ShowWindow(g_barWnd, SW_HIDE);
        HideGlass(g_barGlass);
        return;
    }
    float s = g_scale;
    MONITORINFO mi = PrimaryMon();
    int W = mi.rcMonitor.right - mi.rcMonitor.left, H = BarH();
    g_barX = mi.rcMonitor.left;
    g_barY = mi.rcMonitor.top;
    if (g_clockText.empty()) g_clockText = ClockText();
    BuildBarItems();
    g_barSurf.Ensure(W, H);
    g_barSurf.Clear();
    bool light = LightTheme();
    Color fg = light ? Color(255, 24, 24, 26) : Color(255, 245, 245, 247);
    {
        Graphics gr(g_barSurf.bmp);
        gr.SetSmoothingMode(SmoothingModeAntiAlias);
        gr.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        BYTE alpha = (BYTE)std::clamp(S.opacity * 255 / 100 * 4 / 5, 10, 255);
        SolidBrush bg(light ? Color(alpha, 246, 246, 248) : Color(alpha, 28, 28, 34));
        gr.FillRectangle(&bg, 0, 0, W, H);
        Pen line(light ? Color(40, 0, 0, 0) : Color(50, 255, 255, 255), 1.0f);
        gr.DrawLine(&line, 0.0f, H - 0.5f, (REAL)W, H - 0.5f);
        float pad = 9 * s, x = 8 * s;
        RectF tb;
        for (auto& it : g_barItems) {
            if (it.kind > B_MENU) break;
            float w = 34 * s;
            if (it.kind != B_LOGO) {
                gr.MeasureString(it.text.c_str(), -1, it.kind == B_APP ? g_barBold : g_barFont, PointF(0, 0), &tb);
                w = tb.Width + 2 * pad;
            }
            if (x + w > W * 0.6f) { it.w = 0; continue; }
            it.x = x;
            it.w = w;
            x += w;
        }
        float rx = W - 8 * s;
        for (int i = (int)g_barItems.size() - 1; i >= 0 && g_barItems[i].kind > B_MENU; i--) {
            auto& it = g_barItems[i];
            float w = it.kind == B_TRAY ? 26 * s : 32 * s;
            if (it.kind == B_CLOCK) {
                gr.MeasureString(it.text.c_str(), -1, g_barFont, PointF(0, 0), &tb);
                w = tb.Width + 2 * pad;
            }
            rx -= w;
            it.x = rx;
            it.w = w;
        }
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        sf.SetFormatFlags(StringFormatFlagsNoWrap);
        SolidBrush tc(fg);
        for (size_t i = 0; i < g_barItems.size(); i++) {
            auto& it = g_barItems[i];
            if (it.w <= 0) continue;
            if ((int)i == g_barOpen || (int)i == g_barHover) {
                GraphicsPath hp;
                RoundRectPath(hp, RectF(it.x + 1, 3 * s, it.w - 2, H - 6 * s), 5 * s);
                SolidBrush hb((int)i == g_barOpen ? (light ? Color(50, 0, 0, 0) : Color(65, 255, 255, 255))
                                                  : (light ? Color(25, 0, 0, 0) : Color(30, 255, 255, 255)));
                gr.FillPath(&hb, &hp);
            }
            RectF r(it.x, 0, it.w, (REAL)H);
            if (it.kind == B_LOGO) {
                float q = 6 * s, gap = 1.2f * s, cx = it.x + it.w / 2, cy = H / 2.0f;
                for (int k = 0; k < 4; k++)
                    gr.FillRectangle(&tc, cx - q - gap / 2 + (k % 2) * (q + gap), cy - q - gap / 2 + (k / 2) * (q + gap), q, q);
            } else if (it.kind == B_APP) {
                gr.DrawString(it.text.c_str(), -1, g_barBold, r, &sf, &tc);
            } else if (it.kind == B_MENU || it.kind == B_CLOCK) {
                gr.DrawString(it.text.c_str(), -1, g_barFont, r, &sf, &tc);
            } else if (it.kind == B_TRAY) {
                if (it.menuIndex >= 0 && it.menuIndex < (int)g_tray.size() && g_tray[it.menuIndex].bmp) {
                    float sz = std::round(16 * s);
                    gr.DrawImage(g_tray[it.menuIndex].bmp, RectF(std::round(it.x + (it.w - sz) / 2), std::round((H - sz) / 2), sz, sz));
                }
            } else {
                wchar_t gb[2];
                gr.DrawString(BarGlyph(it, gb), -1, g_iconFont, r, &sf, &tc);
            }
        }
    }
    g_barSurf.Present(g_barWnd, g_barX, g_barY);
    if (!IsWindowVisible(g_barWnd)) ShowWindow(g_barWnd, SW_SHOWNOACTIVATE);
    if (S.glass) PlaceGlass(g_barGlass, g_barWnd, RECT{g_barX, g_barY, g_barX + W, g_barY + H}, 0);
    else HideGlass(g_barGlass);
}
static void BarPoll() {
    static int n = 0;
    if (n++ % 5 == 0) { UpdateNet(); UpdateVolume(); UpdateBattery(); }
    TraySync();
    g_clockText = ClockText();
    wchar_t sig[64];
    swprintf(sig, 64, L"%d|%d|%d|%d|%d|", g_netState, g_volume, g_mute ? 1 : 0, g_batt, g_charging ? 1 : 0);
    std::wstring s2 = sig + g_clockText;
    if (s2 != g_barSig) { g_barSig = s2; RenderBar(); }
}
static void SendCombo(WORD mod, WORD key) {
    INPUT in[4] = {};
    for (auto& i : in) i.type = INPUT_KEYBOARD;
    in[0].ki.wVk = mod;
    in[1].ki.wVk = key;
    in[2].ki.wVk = key;
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = mod;
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}
static std::vector<HWND> AppWindows(const std::wstring& key) {
    std::vector<HWND> v;
    for (auto& kv : g_winCache) if (kv.second.key == key) v.push_back(kv.first);
    return v;
}
static HMENU BuildLogoMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"About This PC");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"System Settings…");
    AppendMenuW(m, MF_STRING, ID_OPENSETTINGS, L"Just Better TaskBar Settings…");
    AppendMenuW(m, MF_STRING, ID_START, L"Apps (Start)");
    AppendMenuW(m, MF_STRING, ID_TASKMGR, L"Task Manager…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SLEEP, L"Sleep");
    AppendMenuW(m, MF_STRING, ID_RESTART, L"Restart…");
    AppendMenuW(m, MF_STRING, ID_SHUTDOWN, L"Shut Down…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_LOCKPC, L"Lock Screen");
    AppendMenuW(m, MF_STRING, ID_LOGOFF, L"Sign Out…");
    return m;
}
static HMENU BuildAppMenu() {
    HMENU m = CreatePopupMenu();
    if (!g_barApp) {
        AppendMenuW(m, MF_STRING, ID_APPNEW, L"New File Explorer Window");
        AppendMenuW(m, MF_STRING, ID_SHOWDESKTOP, L"Show Desktop");
        return m;
    }
    std::wstring q = L"\"" + g_barAppName + L"\"";
    if (!g_barAppLaunch.empty()) {
        AppendMenuW(m, MF_STRING, ID_APPNEW, L"New Window");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(m, MF_STRING, ID_APPHIDE, (L"Hide " + g_barAppName).c_str());
    AppendMenuW(m, MF_STRING, ID_APPHIDEOTHERS, L"Hide Others");
    AppendMenuW(m, MF_STRING, ID_APPSHOWALL, L"Show All");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_APPZOOM, IsZoomed(g_barApp) ? L"Restore Size" : L"Zoom");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_APPQUIT, (L"Quit " + g_barAppName).c_str());
    return m;
}
static bool Confirm(const wchar_t* text) {
    return MessageBoxW(nullptr, text, L"Just Better TaskBar", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) == IDYES;
}
static void HandleBarCmd(int cmd) {
    switch (cmd) {
    case ID_ABOUT: ShellExecuteW(nullptr, L"open", L"ms-settings:about", nullptr, nullptr, SW_SHOWNORMAL); break;
    case ID_SETTINGS: ShellExecuteW(nullptr, L"open", L"ms-settings:", nullptr, nullptr, SW_SHOWNORMAL); break;
    case ID_START: SendWinKey(); break;
    case ID_TASKMGR: ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); break;
    case ID_SLEEP: SetSuspendState(FALSE, FALSE, FALSE); break;
    case ID_RESTART: if (Confirm(L"Restart the computer now?")) ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/r /t 0", nullptr, SW_HIDE); break;
    case ID_SHUTDOWN: if (Confirm(L"Shut down the computer now?")) ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/s /t 0", nullptr, SW_HIDE); break;
    case ID_LOCKPC: LockWorkStation(); break;
    case ID_LOGOFF: if (Confirm(L"Sign out now? Unsaved work may be lost.")) ExitWindowsEx(EWX_LOGOFF, 0); break;
    case ID_SHOWDESKTOP: SendCombo(VK_LWIN, 'D'); break;
    case ID_OPENSETTINGS: OpenSettings(); break;
    case ID_APPNEW:
        if (!g_barApp) { wchar_t win[MAX_PATH]; GetWindowsDirectoryW(win, MAX_PATH); LaunchPath(std::wstring(win) + L"\\explorer.exe"); }
        else if (!g_barAppLaunch.empty()) LaunchPath(g_barAppLaunch);
        break;
    case ID_APPHIDE:
        for (HWND w : AppWindows(g_barAppKey)) PostMessageW(w, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        break;
    case ID_APPHIDEOTHERS:
        for (auto& kv : g_winCache)
            if (kv.second.key != g_barAppKey && !IsIconic(kv.first)) PostMessageW(kv.first, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        if (g_barApp) Activate(g_barApp);
        break;
    case ID_APPSHOWALL:
        for (auto& kv : g_winCache) if (IsIconic(kv.first)) ShowWindowAsync(kv.first, SW_RESTORE);
        break;
    case ID_APPZOOM:
        if (g_barApp) { PostMessageW(g_barApp, WM_SYSCOMMAND, IsZoomed(g_barApp) ? SC_RESTORE : SC_MAXIMIZE, 0); Activate(g_barApp); }
        break;
    case ID_APPQUIT:
        for (HWND w : AppWindows(g_barAppKey)) PostMessageW(w, WM_CLOSE, 0, 0);
        if (g_barApp && AppWindows(g_barAppKey).empty()) PostMessageW(g_barApp, WM_CLOSE, 0, 0);
        break;
    }
}
static LRESULT CALLBACK MenuHookProc(int code, WPARAM w, LPARAM l) {
    if (code == MSGF_MENU) {
        MSG* m = (MSG*)l;
        if (m->message == WM_MOUSEMOVE) {
            POINT p = m->pt;
            if (p.y >= g_barY && p.y < g_barY + BarH()) {
                int i = BarHit((float)(p.x - g_barX));
                if (i >= 0 && i != g_barOpen && BarIsMenu(i)) { g_barNext = i; EndMenu(); }
            }
        }
    }
    return CallNextHookEx(g_menuHook, code, w, l);
}
static void OpenBarMenu(int idx) {
    int cur = idx;
    while (cur >= 0 && cur < (int)g_barItems.size()) {
        BarItem it = g_barItems[cur];
        HWND app = g_barApp;
        HMENU own = nullptr, foreign = nullptr;
        if (it.kind == B_LOGO) own = BuildLogoMenu();
        else if (it.kind == B_APP) own = BuildAppMenu();
        else if (it.kind == B_MENU && app) {
            HMENU m = GetMenu(app);
            foreign = m ? GetSubMenu(m, it.menuIndex) : nullptr;
            if (foreign) {
                DWORD_PTR r;
                SendMessageTimeoutW(app, WM_INITMENU, (WPARAM)m, 0, SMTO_ABORTIFHUNG, 200, &r);
                SendMessageTimeoutW(app, WM_INITMENUPOPUP, (WPARAM)foreign, MAKELPARAM(it.menuIndex, FALSE), SMTO_ABORTIFHUNG, 200, &r);
            }
        }
        HMENU show = own ? own : foreign;
        if (!show) break;
        g_barOpen = cur;
        g_barNext = -1;
        RenderBar();
        g_menuHook = SetWindowsHookExW(WH_MSGFILTER, MenuHookProc, nullptr, GetCurrentThreadId());
        g_menuOpen = true;
        SetForegroundWindow(g_barWnd);
        int cmd = TrackPopupMenu(show, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
                                 g_barX + (int)it.x, g_barY + BarH(), 0, g_barWnd, nullptr);
        g_menuOpen = false;
        if (g_menuHook) { UnhookWindowsHookEx(g_menuHook); g_menuHook = nullptr; }
        PostMessageW(g_barWnd, WM_NULL, 0, 0);
        if (own) DestroyMenu(own);
        g_menuClosedAt = GetTickCount64();
        g_menuClosedIdx = cur;
        if (cmd) {
            if (foreign) {
                if (app && IsWindow(app)) { Activate(app); PostMessageW(app, WM_COMMAND, MAKEWPARAM(cmd, 0), 0); }
            } else {
                if (app && IsWindow(app) && cmd >= ID_APPNEW) Activate(app);
                HandleBarCmd(cmd);
            }
            break;
        }
        if (g_barNext < 0) {
            if (app && IsWindow(app)) SetForegroundWindow(app);
            break;
        }
        cur = g_barNext;
    }
    g_barOpen = -1;
    RenderBar();
}
static void OnBarClick(int i) {
    if (i < 0) return;
    if (i == g_menuClosedIdx && GetTickCount64() - g_menuClosedAt < 250) return;
    switch (g_barItems[i].kind) {
    case B_LOGO: case B_APP: case B_MENU: OpenBarMenu(i); break;
    case B_SEARCH: SendCombo(VK_LWIN, 'S'); break;
    case B_NET: ShellExecuteW(nullptr, L"open", L"ms-availablenetworks:", nullptr, nullptr, SW_SHOWNORMAL); break;
    case B_VOL: case B_BATT: case B_CC: SendCombo(VK_LWIN, 'A'); break;
    case B_CLOCK: SendCombo(VK_LWIN, 'N'); break;
    }
}
static LRESULT CALLBACK BarProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        if (!g_barTracking) {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            g_barTracking = TrackMouseEvent(&t) != FALSE;
        }
        int i = BarHit((float)GET_X_LPARAM(l));
        if (i != g_barHover) {
            g_barHover = i;
            if (i >= 0 && g_barItems[i].kind == B_TRAY) {
                int ti = g_barItems[i].menuIndex;
                if (ti >= 0 && ti < (int)g_tray.size()) TraySend(g_tray[ti], WM_MOUSEMOVE);
            }
            RenderBar();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_barTracking = false;
        if (g_barHover != -1) { g_barHover = -1; RenderBar(); }
        return 0;
    case WM_LBUTTONDOWN: {
        int i = BarHit((float)GET_X_LPARAM(l));
        if (i >= 0 && g_barItems[i].kind == B_TRAY) return 0;
        OnBarClick(i);
        return 0;
    }
    case WM_LBUTTONUP: {
        int i = BarHit((float)GET_X_LPARAM(l));
        if (i >= 0 && g_barItems[i].kind == B_TRAY) TrayClick(i, 0);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int i = BarHit((float)GET_X_LPARAM(l));
        if (i >= 0 && g_barItems[i].kind == B_TRAY) TrayClick(i, 2);
        else OnBarClick(i);
        return 0;
    }
    case WM_RBUTTONUP: {
        int i = BarHit((float)GET_X_LPARAM(l));
        if (i >= 0 && g_barItems[i].kind == B_TRAY) TrayClick(i, 1);
        else ShowMenu(-1);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int i = BarHit((float)(p.x - g_barX));
        if (i >= 0 && g_barItems[i].kind == B_VOL && g_vol) {
            float v = 0;
            g_vol->GetMasterVolumeLevelScalar(&v);
            v = std::clamp(v + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 0.02f : -0.02f), 0.0f, 1.0f);
            g_vol->SetMasterVolumeLevelScalar(v, nullptr);
            if (g_mute && v > 0) g_vol->SetMute(FALSE, nullptr);
            g_volume = (int)std::lround(v * 100);
            g_mute = false;
            RenderBar();
        }
        return 0;
    }
    case WM_BARAPPBAR:
        if (w == ABN_POSCHANGED) BarAppBarSetPos();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void CALLBACK OnForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (!g_menuOpen) BarTrackForeground();
}

// ---------------------------------------------------------------- settings window
// All visuals come from DesignTokens.h; no colors or sizes are hard-coded here.
// Every control calls the same DockCommand commands as the context menu, so both always behave the same.
enum WType { W_HEADING, W_TOGGLE, W_SLIDER, W_SEGMENT, W_BUTTON };
struct Widget {
    WType type = W_HEADING;
    std::wstring label, desc, unit;
    std::vector<std::wstring> opts;
    int mn = 0, mx = 100;
    std::function<int()> get;
    std::function<void(int)> set;
    std::function<void(int)> live;
    std::function<bool()> enabled;
    bool danger = false;
    RectF row, ctl;
    std::vector<RectF> segs;
};
HWND g_setWnd = nullptr;
int g_setPage = 0, g_setHover = -1, g_setHoverSeg = -1, g_setDrag = -1, g_setSideHover = -1;
float g_setScroll = 0, g_setContentH = 0, g_setScale = 1;
bool g_setTracking = false;
std::vector<Widget> g_widgets;
std::vector<RectF> g_sideRects;
static const wchar_t* kSetPages[6] = {L"Dock", L"Appearance", L"Behavior", L"Content", L"Menu bar", L"System"};
static const wchar_t* kSetPageDesc[6] = {
    L"Size, position and auto-hide",
    L"Background, color and indicators",
    L"Click and animation behavior",
    L"What appears in the dock",
    L"Top menu bar, clock and tray icons",
    L"Windows taskbar, startup and advanced options",
};

static design::Tokens Tok() { return design::Make(LightTheme()); }
static bool WEnabled(const Widget& w) { return !w.enabled || w.enabled(); }

static Widget& AddW(WType t, const wchar_t* label, const wchar_t* desc = L"") {
    g_widgets.push_back(Widget{});
    Widget& w = g_widgets.back();
    w.type = t;
    w.label = label;
    w.desc = desc ? desc : L"";
    return w;
}
static void Heading(const wchar_t* t) { AddW(W_HEADING, t); }
static Widget& Toggle(const wchar_t* label, const wchar_t* desc, std::function<bool()> get, int cmd) {
    Widget& w = AddW(W_TOGGLE, label, desc);
    w.get = [get] { return get() ? 1 : 0; };
    w.set = [cmd](int) { DockCommand(cmd); };
    return w;
}
static Widget& Segment(const wchar_t* label, const wchar_t* desc, std::vector<std::wstring> opts,
                       std::function<int()> getIdx, std::function<void(int)> setIdx) {
    Widget& w = AddW(W_SEGMENT, label, desc);
    w.opts = opts;
    w.get = getIdx;
    w.set = setIdx;
    return w;
}
static Widget& Slider(const wchar_t* label, const wchar_t* desc, int mn, int mx, const wchar_t* unit,
                      std::function<int()> get, std::function<void(int)> live, std::function<void(int)> commit) {
    Widget& w = AddW(W_SLIDER, label, desc);
    w.mn = mn;
    w.mx = mx;
    w.unit = unit;
    w.get = get;
    w.live = live;
    w.set = commit;
    return w;
}
static Widget& Button(const wchar_t* label, const wchar_t* desc, const wchar_t* text, std::function<void()> act) {
    Widget& w = AddW(W_BUTTON, label, desc);
    w.opts = {text};
    w.set = [act](int) { act(); };
    return w;
}
static int IndexOf(const int* arr, int n, int v) {
    for (int i = 0; i < n; i++) if (arr[i] == v) return i;
    return -1;
}

static void BuildSettingsPage() {
    g_widgets.clear();
    auto unlocked = [] { return !S.locked; };
    auto autohideOn = [] { return S.autohide; };
    auto barOn = [] { return S.showMenubar; };
    switch (g_setPage) {
    case 0:
        Heading(L"Size");
        Slider(L"Icon size", L"You can also drag the divider", 24, 128, L" px",
               [] { return S.size; }, [](int v) { S.size = v; Render(); }, [](int) { ApplySettingsChange(); }).enabled = unlocked;
        Slider(L"Magnification", L"Zoom on hover", 0, 150, L" %",
               [] { return S.magnify; }, [](int v) { S.magnify = v; Render(); }, [](int) { ApplySettingsChange(); });
        Heading(L"Position");
        Segment(L"Position on screen", L"", {L"Bottom", L"Left", L"Right"},
                [] { return S.position; }, [](int i) { DockCommand(ID_POS0 + i); });
        Heading(L"Hiding");
        Toggle(L"Automatically hide and show", L"The dock appears when the pointer reaches the screen edge", [] { return S.autohide; }, ID_AUTOHIDE);
        Segment(L"Show delay", L"", {L"None", L"0.2 s", L"0.5 s", L"1 s"},
                [] { return IndexOf(kDelays, 4, S.hideDelay); }, [](int i) { DockCommand(ID_DELAY0 + i); }).enabled = autohideOn;
        Segment(L"Slide speed", L"", {L"Instant", L"Fast", L"Normal", L"Slow"},
                [] { return IndexOf(kAnims, 4, S.hideAnim); }, [](int i) { DockCommand(ID_ANIM0 + i); }).enabled = autohideOn;
        Heading(L"Lock");
        Toggle(L"Lock the dock", L"Prevents reordering, adding, removing and resizing", [] { return S.locked; }, ID_LOCK);
        break;
    case 1:
        Heading(L"Background");
        Toggle(L"Glass background", L"Blurs the desktop behind the dock and the menu bar", [] { return S.glass; }, ID_GLASS);
        Segment(L"Dock glass", L"Liquid and Clear (beta) bend the desktop at the edges. The dock is left out of screenshots while they're on",
                {L"Frosted", L"Liquid", L"Clear"}, [] { return S.glassStyle; }, [](int i) { DockCommand(ID_GLASSSTYLE0 + i); })
            .enabled = [] { return S.glass; };
        Slider(L"Color vibrancy", L"Colors behind the glass", 100, 250, L" %", [] { return S.glassSaturation; },
               [](int v) { S.glassSaturation = v; ApplyGlassBrush(g_glass); ApplyGlassBrush(g_barGlass); }, [](int) { SaveIni(); })
            .enabled = [] { return S.glass; };
        Segment(L"Color", L"", {L"Dark", L"Light", L"System"}, [] { return S.theme; }, [](int i) { DockCommand(ID_THEME0 + i); });
        Slider(L"Background opacity", L"Lower values are more transparent", 10, 100, L" %",
               [] { return S.opacity; }, [](int v) { S.opacity = v; Render(); RenderBar(); }, [](int) { SaveIni(); });
        Heading(L"Icons");
        Toggle(L"Show app names", L"Shows the app name on hover", [] { return S.tooltips; }, ID_TIPS);
        Toggle(L"Running app indicator", L"Marks running apps", [] { return S.indicators; }, ID_IND);
        Segment(L"Indicator style", L"Highlight and line are stronger for the active app", {L"Dot", L"Highlight", L"Blue line"},
                [] { return S.indicatorStyle; }, [](int i) { DockCommand(ID_INDSTYLE0 + i); }).enabled = [] { return S.indicators; };
        Segment(L"Minimized apps", L"Apps with all windows minimized", {L"Normal", L"Dimmed", L"Blue line"},
                [] { return S.minimizedStyle; }, [](int i) { DockCommand(ID_MINSTYLE0 + i); });
        break;
    case 2:
        Heading(L"Animations");
        Toggle(L"Animate opening apps", L"", [] { return S.bounce; }, ID_BOUNCE);
        Toggle(L"Bounce apps that need attention", L"The icon bounces when an app needs attention", [] { return S.attention; }, ID_ATTN);
        Heading(L"Clicking");
        Toggle(L"Minimize the active app on click", L"When off, clicking does nothing (like macOS)", [] { return S.clickMinimize; }, ID_CLICKMIN);
        Heading(L"Minimizing");
        Toggle(L"Show minimized windows as previews", L"Live thumbnails next to the Recycle Bin",
               [] { return S.minimizedPreviews; }, ID_MINPREV);
        Heading(L"Full screen");
        Toggle(L"Hide in full-screen apps", L"Hides the dock and the menu bar in games and videos", [] { return S.hideFullscreen; }, ID_FULLHIDE);
        break;
    case 3:
        Heading(L"Shown in the dock");
        Toggle(L"Start button", L"", [] { return S.showLauncher; }, ID_SHOWLAUNCH);
        Toggle(L"Unpinned open apps", L"Also shows running apps that aren't pinned", [] { return S.showRunning; }, ID_SHOWRUN);
        Toggle(L"Separate pinned and open apps", L"When off, all apps share one row in any order", [] { return S.separateApps; }, ID_SEPAPPS);
        Toggle(L"Downloads stack", L"Lists recent downloads when clicked", [] { return HasDownloadsStack(); }, ID_DOWNLOADS).enabled = unlocked;
        Toggle(L"Recycle Bin", L"", [] { return S.showTrash; }, ID_SHOWTRASH);
        if (!g_hiddenApps.empty()) {
            Heading(L"Hidden apps");
            for (auto& k : g_hiddenApps) {
                std::wstring name;
                if (g_names.count(k)) name = g_names[k];
                else if (k.rfind(L"aumid:", 0) == 0) name = AppName(k.substr(6));
                else name = FileDesc(k);
                std::wstring key = k;
                Button(name.c_str(), L"Hidden from the dock and the menu bar", L"Show", [key] {
                    g_hiddenApps.erase(std::remove(g_hiddenApps.begin(), g_hiddenApps.end(), key), g_hiddenApps.end());
                    SaveIni();
                    Refresh(true);
                    RenderBar();
                    PostMessageW(g_setWnd, WM_APP + 1, 0, 0);
                });
            }
        }
        Heading(L"Layout");
        Button(L"Add spacer", L"Adds empty space between icons; drag it to move it", L"Add",
               [] { DockCommand(ID_SPACER); }).enabled = unlocked;
        break;
    case 4:
        Heading(L"Menu bar");
        Toggle(L"Show the top menu bar", L"", [] { return S.showMenubar; }, ID_BAR);
        Toggle(L"Background app icons", L"Tray icons such as Steam and Discord", [] { return S.trayIcons; }, ID_BARTRAY).enabled = barOn;
        Heading(L"Clock");
        Toggle(L"Show date", L"", [] { return S.clockDate; }, ID_BARDATE).enabled = barOn;
        Toggle(L"Show seconds", L"", [] { return S.clockSeconds; }, ID_BARSEC).enabled = barOn;
        break;
    case 5:
        Heading(L"Windows");
        Toggle(L"Hide the Windows taskbar", L"", [] { return S.hideTaskbar; }, ID_HIDETB);
        Toggle(L"Keep windows above the dock", L"Maximized windows don't cover the dock", [] { return S.reserve; }, ID_RESERVE)
            .enabled = [] { return !S.autohide; };
        Toggle(L"Start with Windows", L"Starts automatically when you sign in", [] { return IsAutostart(); }, ID_AUTOSTART);
        Heading(L"Advanced");
        Button(L"Settings file", L"Opens dock.ini in Notepad", L"Open", [] { DockCommand(ID_EDITINI); });
        Button(L"Reload settings", L"Applies changes made to dock.ini by hand", L"Reload", [] { DockCommand(ID_RELOAD); });
        Button(L"Quit Just Better TaskBar", L"The Windows taskbar comes back", L"Quit", [] { DockCommand(ID_QUIT); }).danger = true;
        break;
    }
}

static void ApplySettingsChrome() {
    if (!g_setWnd) return;
    design::Tokens t = Tok();
    BOOL dark = LightTheme() ? FALSE : TRUE;
    DwmSetWindowAttribute(g_setWnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    COLORREF cap = RGB(t.color.background.GetR(), t.color.background.GetG(), t.color.background.GetB());
    COLORREF txt = RGB(t.color.text.GetR(), t.color.text.GetG(), t.color.text.GetB());
    DwmSetWindowAttribute(g_setWnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof cap);
    DwmSetWindowAttribute(g_setWnd, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof txt);
}
static void SettingsChanged() {
    if (!g_setWnd) return;
    ApplySettingsChrome();
    InvalidateRect(g_setWnd, nullptr, FALSE);
}

static void PaintSettings(HDC hdc) {
    RECT cr;
    GetClientRect(g_setWnd, &cr);
    int W = cr.right, H = cr.bottom;
    if (W <= 0 || H <= 0) return;
    design::Tokens t = Tok();
    const auto& c = t.color;
    const auto& m = t.size;
    float s = g_setScale;
    Bitmap buf(W, H, PixelFormat32bppPARGB);
    Graphics g(&buf);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(c.background);
    Font fTitle(t.type.family, t.type.title * s, t.type.titleBold ? FontStyleBold : FontStyleRegular, UnitPixel);
    Font fHead(t.type.family, t.type.heading * s, t.type.headingBold ? FontStyleBold : FontStyleRegular, UnitPixel);
    Font fBody(t.type.family, t.type.body * s, FontStyleRegular, UnitPixel);
    Font fCap(t.type.family, t.type.caption * s, FontStyleRegular, UnitPixel);
    Font fSide(t.type.family, t.type.sidebar * s, FontStyleRegular, UnitPixel);
    StringFormat left, center;
    left.SetLineAlignment(StringAlignmentCenter);
    left.SetFormatFlags(StringFormatFlagsNoWrap);
    left.SetTrimming(StringTrimmingEllipsisCharacter);
    center.SetAlignment(StringAlignmentCenter);
    center.SetLineAlignment(StringAlignmentCenter);
    center.SetFormatFlags(StringFormatFlagsNoWrap);
    auto rounded = [](GraphicsPath& p, RectF r, float rad) { RoundRectPath(p, r, rad); };

    float sw = m.sidebarWidth * s;
    SolidBrush sideBg(c.sidebar);
    g.FillRectangle(&sideBg, 0.0f, 0.0f, sw, (REAL)H);
    Pen edge(c.border, m.borderWidth * s);
    g.DrawLine(&edge, sw - 0.5f, 0.0f, sw - 0.5f, (REAL)H);
    SolidBrush txt(c.text), muted(c.textMuted), dis(c.textDisabled), acc(c.accent), onAcc(c.onAccent), dangerB(c.danger);
    float y = m.spaceXL * s;
    g.DrawString(L"Just Better\nTaskBar", -1, &fTitle, RectF(m.spaceL * s, y, sw - 2 * m.spaceL * s, t.type.title * 2.7f * s), &left, &txt);
    y += t.type.title * 2.7f * s + m.spaceL * s;
    g_sideRects.clear();
    for (int i = 0; i < 6; i++) {
        RectF r(m.spaceS * s, y, sw - 2 * m.spaceS * s, m.sidebarItemHeight * s);
        g_sideRects.push_back(r);
        if (i == g_setPage || i == g_setSideHover) {
            GraphicsPath p;
            rounded(p, r, m.radiusM * s);
            SolidBrush sel(i == g_setPage ? c.selection : c.surfaceHover);
            g.FillPath(&sel, &p);
        }
        if (i == g_setPage) {
            GraphicsPath bar;
            rounded(bar, RectF(r.X + 2 * s, r.Y + r.Height * 0.25f, 3 * s, r.Height * 0.5f), 1.5f * s);
            g.FillPath(&acc, &bar);
        }
        g.DrawString(kSetPages[i], -1, &fSide, RectF(r.X + m.spaceM * s + 4 * s, r.Y, r.Width - m.spaceL * s, r.Height), &left, &txt);
        y += r.Height + m.spaceXS * s;
    }

    float cx = sw + m.spaceXXL * s, cw = W - cx - m.spaceXXL * s;
    float pad = m.spaceL * s;
    y = m.spaceXL * s;
    float titleY = y;
    y += t.type.title * 1.4f * s;
    float descY = y;
    y += t.type.caption * 1.6f * s + m.spaceS * s;
    struct Card { float top, bottom; };
    std::vector<Card> cards;
    std::vector<bool> firstInCard(g_widgets.size(), false);
    bool inCard = false;
    for (size_t i = 0; i < g_widgets.size(); i++) {
        Widget& w = g_widgets[i];
        if (w.type == W_HEADING) {
            if (inCard) { cards.back().bottom = y; inCard = false; }
            y += m.spaceL * s;
            w.row = RectF(cx + m.spaceXS * s, y, cw, t.type.heading * 1.8f * s);
            y += w.row.Height + m.spaceXS * s;
            continue;
        }
        if (!inCard) { cards.push_back({y, y}); inCard = true; firstInCard[i] = true; }
        float h = (w.desc.empty() ? m.rowHeight : m.rowHeightWithDesc) * s;
        w.row = RectF(cx, y, cw, h);
        float cy = y + h / 2, right = cx + cw - pad;
        RectF tb;
        switch (w.type) {
        case W_TOGGLE:
            w.ctl = RectF(right - m.toggleWidth * s, cy - m.toggleHeight * s / 2, m.toggleWidth * s, m.toggleHeight * s);
            break;
        case W_SLIDER: {
            float valW = 64 * s;
            w.ctl = RectF(right - valW - m.sliderWidth * s, cy - m.sliderKnob * s / 2, m.sliderWidth * s, m.sliderKnob * s);
            break;
        }
        case W_SEGMENT: {
            w.segs.clear();
            float total = 0;
            std::vector<float> ws;
            for (auto& o : w.opts) {
                g.MeasureString(o.c_str(), -1, &fCap, PointF(0, 0), &tb);
                float iw = std::max(m.segmentMinItem * s, tb.Width + 2 * m.spaceM * s);
                ws.push_back(iw);
                total += iw;
            }
            float x0 = right - total, sh = m.segmentHeight * s;
            w.ctl = RectF(x0, cy - sh / 2, total, sh);
            for (float iw : ws) { w.segs.push_back(RectF(x0, cy - sh / 2, iw, sh)); x0 += iw; }
            break;
        }
        case W_BUTTON: {
            g.MeasureString(w.opts[0].c_str(), -1, &fBody, PointF(0, 0), &tb);
            float bw = std::max(80 * s, tb.Width + 2 * m.spaceL * s), bh = m.buttonHeight * s;
            w.ctl = RectF(right - bw, cy - bh / 2, bw, bh);
            break;
        }
        default: break;
        }
        y += h;
        cards.back().bottom = y;
    }
    g_setContentH = y + m.spaceXXL * s;
    g_setScroll = std::clamp(g_setScroll, 0.0f, std::max(0.0f, g_setContentH - H));

    g.SetClip(RectF(sw, 0, W - sw, (REAL)H));
    g.TranslateTransform(0, -g_setScroll);
    g.DrawString(kSetPages[g_setPage], -1, &fTitle, RectF(cx, titleY, cw, t.type.title * 1.4f * s), &left, &txt);
    g.DrawString(kSetPageDesc[g_setPage], -1, &fCap, RectF(cx, descY, cw, t.type.caption * 1.6f * s), &left, &muted);
    SolidBrush surf(c.surface);
    Pen cardPen(c.border, m.borderWidth * s), divPen(c.divider, m.borderWidth * s);
    for (auto& cd : cards) {
        GraphicsPath p;
        rounded(p, RectF(cx, cd.top, cw, cd.bottom - cd.top), m.radiusL * s);
        g.FillPath(&surf, &p);
        g.DrawPath(&cardPen, &p);
    }
    for (size_t i = 0; i < g_widgets.size(); i++) {
        Widget& w = g_widgets[i];
        bool en = WEnabled(w);
        if (w.type == W_HEADING) {
            g.DrawString(w.label.c_str(), -1, &fHead, w.row, &left, &muted);
            continue;
        }
        if (!firstInCard[i]) g.DrawLine(&divPen, w.row.X + pad, w.row.Y, w.row.X + w.row.Width - pad, w.row.Y);
        float textW = w.ctl.X - w.row.X - 2 * pad;
        if (w.desc.empty()) {
            g.DrawString(w.label.c_str(), -1, &fBody, RectF(w.row.X + pad, w.row.Y, textW, w.row.Height), &left, en ? &txt : &dis);
        } else {
            float mid = w.row.Y + w.row.Height / 2;
            g.DrawString(w.label.c_str(), -1, &fBody, RectF(w.row.X + pad, mid - t.type.body * 1.45f * s, textW, t.type.body * 1.4f * s), &left, en ? &txt : &dis);
            g.DrawString(w.desc.c_str(), -1, &fCap, RectF(w.row.X + pad, mid + 1 * s, textW, t.type.caption * 1.5f * s), &left, en ? &muted : &dis);
        }
        bool hov = (int)i == g_setHover && en;
        switch (w.type) {
        case W_TOGGLE: {
            bool on = w.get() != 0;
            GraphicsPath tp;
            rounded(tp, w.ctl, w.ctl.Height / 2);
            SolidBrush tb(on ? (hov ? c.accentHover : c.accent) : c.track);
            if (!en) tb.SetColor(Color(110, on ? c.accent.GetR() : c.track.GetR(), on ? c.accent.GetG() : c.track.GetG(), on ? c.accent.GetB() : c.track.GetB()));
            g.FillPath(&tb, &tp);
            float k = m.toggleKnob * s, kx = on ? w.ctl.GetRight() - k - (w.ctl.Height - k) / 2 : w.ctl.X + (w.ctl.Height - k) / 2;
            SolidBrush kb(c.knob);
            g.FillEllipse(&kb, kx, w.ctl.Y + (w.ctl.Height - k) / 2, k, k);
            break;
        }
        case W_SLIDER: {
            int v = w.get();
            float f = (float)(v - w.mn) / std::max(1, w.mx - w.mn);
            float th = m.sliderTrack * s, ty = w.ctl.Y + w.ctl.Height / 2 - th / 2;
            GraphicsPath full, fill;
            rounded(full, RectF(w.ctl.X, ty, w.ctl.Width, th), th / 2);
            SolidBrush trk(c.track);
            g.FillPath(&trk, &full);
            if (f > 0) {
                rounded(fill, RectF(w.ctl.X, ty, std::max(th, w.ctl.Width * f), th), th / 2);
                SolidBrush fb(en ? c.accent : c.textDisabled);
                g.FillPath(&fb, &fill);
            }
            float k = m.sliderKnob * s, kx = w.ctl.X + w.ctl.Width * f - k / 2;
            SolidBrush kb(c.knob);
            Pen kp(c.border, m.borderWidth * s);
            g.FillEllipse(&kb, kx, w.ctl.Y, k, k);
            g.DrawEllipse(&kp, kx, w.ctl.Y, k, k);
            std::wstring val = std::to_wstring(v) + w.unit;
            StringFormat rf;
            rf.SetAlignment(StringAlignmentFar);
            rf.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(val.c_str(), -1, &fBody, RectF(w.ctl.GetRight(), w.row.Y, w.row.GetRight() - pad - w.ctl.GetRight(), w.row.Height), &rf, en ? &muted : &dis);
            break;
        }
        case W_SEGMENT: {
            int sel = w.get();
            GraphicsPath box;
            rounded(box, w.ctl, m.radiusM * s);
            SolidBrush boxB(c.background);
            g.FillPath(&boxB, &box);
            g.DrawPath(&cardPen, &box);
            for (size_t k = 0; k < w.segs.size(); k++) {
                RectF r = w.segs[k];
                bool isSel = (int)k == sel;
                if (isSel || ((int)i == g_setHover && (int)k == g_setHoverSeg && en)) {
                    GraphicsPath sp;
                    rounded(sp, RectF(r.X + 2 * s, r.Y + 2 * s, r.Width - 4 * s, r.Height - 4 * s), m.radiusS * s);
                    SolidBrush sb(isSel ? (en ? c.accent : c.textDisabled) : c.surfaceHover);
                    g.FillPath(&sb, &sp);
                }
                g.DrawString(w.opts[k].c_str(), -1, &fCap, r, &center, isSel ? &onAcc : (en ? &txt : &dis));
            }
            break;
        }
        case W_BUTTON: {
            GraphicsPath bp;
            rounded(bp, w.ctl, m.radiusM * s);
            SolidBrush bb(hov ? c.surfaceHover : c.background);
            g.FillPath(&bb, &bp);
            g.DrawPath(&cardPen, &bp);
            g.DrawString(w.opts[0].c_str(), -1, &fBody, w.ctl, &center, !en ? &dis : (w.danger ? &dangerB : &txt));
            break;
        }
        default: break;
        }
    }
    g.ResetTransform();
    g.ResetClip();
    Graphics out(hdc);
    out.DrawImage(&buf, 0, 0);
}

static int SettingsHit(int x, int y, int* seg) {
    *seg = -1;
    float cy = y + g_setScroll;
    for (size_t i = 0; i < g_widgets.size(); i++) {
        Widget& w = g_widgets[i];
        if (w.type == W_HEADING || !w.row.Contains((REAL)x, cy)) continue;
        if (w.type == W_SEGMENT) {
            for (size_t k = 0; k < w.segs.size(); k++) if (w.segs[k].Contains((REAL)x, cy)) *seg = (int)k;
            return (int)i;
        }
        if (w.type == W_TOGGLE) return (int)i;
        RectF big = w.ctl;
        big.Inflate(6 * g_setScale, 8 * g_setScale);
        if (big.Contains((REAL)x, cy)) return (int)i;
        return -1;
    }
    return -1;
}
static void SliderSet(Widget& w, int x, bool commit) {
    float f = std::clamp((x - w.ctl.X) / std::max(1.0f, w.ctl.Width), 0.0f, 1.0f);
    int v = w.mn + (int)std::lround(f * (w.mx - w.mn));
    if (commit) { if (w.live) w.live(v); w.set(v); }
    else if (v != w.get() && w.live) w.live(v);
}
static LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintSettings(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (!g_setTracking) {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            g_setTracking = TrackMouseEvent(&t) != FALSE;
        }
        if (g_setDrag >= 0 && g_setDrag < (int)g_widgets.size()) {
            SliderSet(g_widgets[g_setDrag], x, false);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        int side = -1;
        for (size_t i = 0; i < g_sideRects.size(); i++) if (g_sideRects[i].Contains((REAL)x, (REAL)y)) side = (int)i;
        int seg;
        int hit = SettingsHit(x, y, &seg);
        if (side != g_setSideHover || hit != g_setHover || seg != g_setHoverSeg) {
            g_setSideHover = side;
            g_setHover = hit;
            g_setHoverSeg = seg;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_setTracking = false;
        g_setHover = g_setSideHover = g_setHoverSeg = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && (g_setHover >= 0 || g_setSideHover >= 0 || g_setDrag >= 0)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        for (size_t i = 0; i < g_sideRects.size(); i++)
            if (g_sideRects[i].Contains((REAL)x, (REAL)y)) {
                g_setPage = (int)i;
                g_setScroll = 0;
                g_setHover = -1;
                BuildSettingsPage();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
        int seg;
        int hit = SettingsHit(x, y, &seg);
        if (hit < 0 || !WEnabled(g_widgets[hit])) return 0;
        Widget& w = g_widgets[hit];
        switch (w.type) {
        case W_TOGGLE: w.set(w.get() ? 0 : 1); break;
        case W_SEGMENT: if (seg >= 0 && seg != w.get()) w.set(seg); break;
        case W_BUTTON: w.set(0); break;
        case W_SLIDER:
            g_setDrag = hit;
            SetCapture(h);
            SliderSet(w, x, false);
            break;
        default: break;
        }
        if (IsWindow(h)) InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_setDrag >= 0) {
            int d = g_setDrag;
            g_setDrag = -1;
            ReleaseCapture();
            if (d < (int)g_widgets.size()) SliderSet(g_widgets[d], GET_X_LPARAM(lp), true);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_APP + 1:  // page content changed (e.g. a hidden app was restored)
        g_setHover = -1;
        BuildSettingsPage();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        g_setDrag = -1;
        return 0;
    case WM_MOUSEWHEEL:
        g_setScroll -= GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 48 * g_setScale;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        g_setScale = HIWORD(wp) / 96.0f;
        RECT* r = (RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mi = (MINMAXINFO*)lp;
        mi->ptMinTrackSize = POINT{(LONG)(560 * g_setScale), (LONG)(400 * g_setScale)};
        return 0;
    }
    case WM_DESTROY:
        g_setWnd = nullptr;
        g_widgets.clear();
        g_setDrag = -1;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static void OpenSettings() {
    if (g_setWnd) {
        if (IsIconic(g_setWnd)) ShowWindow(g_setWnd, SW_RESTORE);
        SetForegroundWindow(g_setWnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(g_hinst, MAKEINTRESOURCEW(1));
        wc.hIconSm = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        wc.lpszClassName = L"JustBetterTaskBarSettings";
        RegisterClassExW(&wc);
        registered = true;
    }
    MONITORINFO mi = PrimaryMon();
    UINT dpi = GetDpiForWindow(g_hwnd);
    g_setScale = dpi / 96.0f;
    design::Tokens t = Tok();
    RECT r{0, 0, (LONG)(t.size.windowWidth * g_setScale), (LONG)(t.size.windowHeight * g_setScale)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    int w = r.right - r.left, hgt = r.bottom - r.top;
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - hgt) / 2;
    g_setWnd = CreateWindowExW(0, L"JustBetterTaskBarSettings", L"Just Better TaskBar Settings", style, x, y, w, hgt, nullptr, nullptr, g_hinst, nullptr);
    if (!g_setWnd) return;
    g_setPage = 0;
    g_setScroll = 0;
    BuildSettingsPage();
    ApplySettingsChrome();
    ShowWindow(g_setWnd, SW_SHOW);
    SetForegroundWindow(g_setWnd);
}

// ---------------------------------------------------------------- window procedure
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_shellMsg && m == g_shellMsg) {
        if (w == HSHELL_FLASH && S.attention) {
            for (auto& it : g_items)
                for (HWND x : it.wins)
                    if (x == (HWND)l && !g_bounce.count(it.key)) StartBounce(it.key, 3);
        }
        ScheduleRefresh();
        return 0;
    }
    if (g_taskbarCreatedMsg && m == g_taskbarCreatedMsg) {
        if (GetTickCount64() < g_ignoreTbCreatedUntil) return 0;
        g_appbar = false;
        ApplyShell();
        return 0;
    }
    if (m == WM_APPBAR) {
        if (w == ABN_POSCHANGED) AppBarSetPos();
        return 0;
    }
    if (m == WM_APP + 5) {
        OpenSettings();
        return 0;
    }
    if (m == WM_LIQUIDACCESS) {
        liquid::OnAccessReady();
        return 0;
    }
    switch (m) {
    case WM_TIMER:
        if (w == T_ANIM) Tick();
        else if (w == T_REFRESH) { KillTimer(h, T_REFRESH); if (!g_dragging) Refresh(); }
        else if (w == T_POLL) Poll();
        else if (w == T_SIZE) { KillTimer(h, T_SIZE); ApplySettingsChange(); }
        else if (w == T_EDGE) EdgeTick();
        else if (w == T_DRAG) DragPoll();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR: {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(h, &p);
        if (!S.locked && (g_resizing || IsOverSep(Horizontal() ? (float)p.x : (float)p.y))) {
            SetCursor(LoadCursor(nullptr, Horizontal() ? IDC_SIZENS : IDC_SIZEWE));
            return TRUE;
        }
        break;
    }
    case WM_MOUSEWHEEL:
        if ((GET_KEYSTATE_WPARAM(w) & MK_CONTROL) && (GET_KEYSTATE_WPARAM(w) & MK_SHIFT)) {
            S.magnify = std::clamp(S.magnify + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 5 : -5), 0, 150);
            Render();
            SetTimer(h, T_SIZE, 400, nullptr);
        } else if ((GET_KEYSTATE_WPARAM(w) & MK_CONTROL) && !S.locked) {
            S.size = std::clamp(S.size + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 4 : -4), 24, 128);
            Render();
            SetTimer(h, T_SIZE, 400, nullptr);
        }
        return 0;
    case WM_CAPTURECHANGED: {
        int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
        if ((GetAsyncKeyState(vk) & 0x8000) && (g_down || g_dragging || g_resizing)) return 0;
        if (g_resizing) {
            g_resizing = false;
            ApplySettingsChange();
        }
        if (g_dragging) { g_down = false; EndDrag(); }
        if (g_down) { g_down = false; g_pressIdx = -1; g_pressKey.clear(); Render(); }
        {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            g_tracking = TrackMouseEvent(&t) != FALSE;
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p;
        GetCursorPos(&p);
        if (g_resizing) {
            int d = S.position == 0 ? g_resizeStart.y - p.y : S.position == 1 ? p.x - g_resizeStart.x : g_resizeStart.x - p.x;
            int ns = std::clamp(g_resizeStartSize + (int)std::lround(d / g_scale), 24, 128);
            if (ns != S.size) { S.size = ns; Render(); }
            return 0;
        }
        if (g_dragging) { DragMove(); return 0; }
        if (g_down && std::max(std::abs(p.x - g_pressPt.x), std::abs(p.y - g_pressPt.y)) > 6 * g_scale) {
            for (size_t k = 0; k < g_items.size(); k++)
                if (g_items[k].key == g_pressKey && CanDrag((int)k)) { StartDrag((int)k); return 0; }
        }
        g_mx = MainOf(l);
        if (!g_tracking) {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            g_tracking = TrackMouseEvent(&t) != FALSE;
        }
        if (!g_down) g_hoverTarget = 1;
        StartAnim();
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false;
        g_hoverTarget = 0;
        if (!g_down) g_pressIdx = -1;
        StartAnim();
        return 0;
    case WM_LBUTTONDOWN:
        if (!S.locked && IsOverSep(MainOf(l))) {
            GetCursorPos(&g_resizeStart);
            g_resizing = true;
            g_resizeStartSize = S.size;
            g_hover = g_hoverTarget = 0;
            SetCapture(h);
            SetTimer(h, T_DRAG, 15, nullptr);
            Render();
            return 0;
        }
        g_down = true;
        g_pressIdx = HitTest(MainOf(l));
        g_pressKey = g_pressIdx >= 0 ? g_items[g_pressIdx].key : L"";
        GetCursorPos(&g_pressPt);
        SetCapture(h);
        SetTimer(h, T_DRAG, 15, nullptr);
        Render();
        return 0;
    case WM_LBUTTONUP: {
        if (g_resizing || g_dragging) { ReleaseCapture(); return 0; }
        KillTimer(h, T_DRAG);
        if (g_down) {
            POINT up;
            GetCursorPos(&up);
            FinishPress(up, (w & MK_SHIFT) != 0);
        }
        ReleaseCapture();
        return 0;
    }
    case WM_MBUTTONUP: {
        int i = HitTest(MainOf(l));
        if (i >= 0 && g_items[i].kind == K_APP) LaunchItem(g_items[i]);
        return 0;
    }
    case WM_RBUTTONUP:
        ShowMenu(HitTest(MainOf(l)));
        return 0;
    case WM_DROPFILES:
        OnDrop((HDROP)w);
        return 0;
    case WM_DPICHANGED:
        g_scale = HIWORD(w) / 96.0f;
        CreateFontObj();
        ClearIcons();
        AppBarSetPos();
        BarAppBarSetPos();
        Refresh(true);
        RenderBar();
        return 0;
    case WM_DISPLAYCHANGE:
        AppBarSetPos();
        ScheduleRefresh();
        return 0;
    case WM_SETTINGCHANGE:
        if (l && !wcscmp((const wchar_t*)l, L"ImmersiveColorSet")) {
            g_sysLight = ReadSysLight();
            if (S.theme == 2) { Render(); RenderBar(); }
        }
        return 0;
    case WM_ENDSESSION:
        if (w) Cleanup();
        return 0;
    case WM_DESTROY:
        TrayEnable(false);
        Cleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void EnableDarkMenus() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    typedef int(WINAPI * SetPreferredAppModeFn)(int);
    typedef void(WINAPI * FlushMenuThemesFn)();
    auto setMode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    auto flush = (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (setMode) setMode(1);
    if (flush) flush();
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsstr(cmdLine, L"--quit")) {
        EnumWindows([](HWND w, LPARAM) -> BOOL {
            wchar_t c[64] = {};
            GetClassNameW(w, c, 64);
            if (!wcscmp(c, L"JustBetterTaskBarWnd")) PostMessageW(w, WM_CLOSE, 0, 0);
            return TRUE;
        }, 0);
        return 0;
    }
    bool wantSettings = cmdLine && wcsstr(cmdLine, L"--settings");
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\JustBetterTaskBar_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (wantSettings)
            EnumWindows([](HWND w, LPARAM) -> BOOL {
                wchar_t c[64] = {};
                GetClassNameW(w, c, 64);
                if (!wcscmp(c, L"JustBetterTaskBarWnd")) PostMessageW(w, WM_APP + 5, 0, 0);
                return TRUE;
            }, 0);
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    GdiplusStartupInput gsi;
    ULONG_PTR gtok = 0;
    GdiplusStartup(&gtok, &gsi, nullptr);
    SetUnhandledExceptionFilter(CrashFilter);
    EnableDarkMenus();

    wchar_t exe[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    g_exePath = exe;
    std::wstring dir = g_exePath.substr(0, g_exePath.find_last_of(L'\\'));
    g_iniPath = dir + L"\\dock.ini";
    if (!LoadIni()) { DefaultPins(); AddDownloadsStack(); SaveIni(); }
    else {
        bool anyStack = false;
        for (auto& p : g_pins) anyStack |= p.stack;
        DWORD v = 0, sz = sizeof v;
        if (!anyStack && RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"StackAdded", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) {
            AddDownloadsStack();
            SaveIni();
        }
    }
    {
        DWORD one = 1;
        RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"StackAdded", REG_DWORD, &one, sizeof one);
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"JustBetterTaskBarWnd";
    RegisterClassExW(&wc);
    WNDCLASSEXW wd{sizeof wd};
    wd.lpfnWndProc = DefWindowProcW;
    wd.hInstance = hi;
    wd.lpszClassName = L"JustBetterTaskBarDrag";
    RegisterClassExW(&wd);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_ACCEPTFILES,
                             wc.lpszClassName, L"Just Better TaskBar", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hi, nullptr);
    if (!g_hwnd) return 1;
    g_dragWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                wd.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hi, nullptr);
    InitComposition(hi);
    CreateGlass(g_glass);
    CreateGlass(g_barGlass);
    WNDCLASSEXW wb{sizeof wb};
    wb.lpfnWndProc = BarProc;
    wb.style = CS_DBLCLKS;
    wb.hInstance = hi;
    wb.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wb.lpszClassName = L"JustBetterTaskBarBar";
    RegisterClassExW(&wb);
    g_barWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                               wb.lpszClassName, L"Just Better TaskBar menu bar", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hi, nullptr);
    g_sysLight = ReadSysLight();
    g_scale = GetDpiForWindow(g_hwnd) / 96.0f;
    CreateFontObj();
    UpdateNet();
    UpdateVolume();
    UpdateBattery();
    g_clockText = ClockText();

    g_shellMsg = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(g_hwnd);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_hwnd, g_taskbarCreatedMsg, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, 0x0049 /*WM_COPYGLOBALDATA*/, MSGFLT_ALLOW, nullptr);

    SHQUERYRBINFO qi{sizeof qi};
    g_trashFull = SUCCEEDED(SHQueryRecycleBinW(nullptr, &qi)) && qi.i64NumItems > 0;
    ApplyShell();
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnWinEvent, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (S.autohide) g_show = g_showTarget = 0;
    UpdateEdgeTimer();
    Refresh(true);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    Render();
    HWINEVENTHOOK fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnForegroundEvent, 0, 0,
                                           WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    BarTrackForeground();
    TrayEnable(S.showMenubar && S.trayIcons);
    RenderBar();
    SetTimer(g_hwnd, T_POLL, 1000, nullptr);
    if (wantSettings) OpenSettings();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (hook) UnhookWinEvent(hook);
    if (fgHook) UnhookWinEvent(fgHook);
    for (auto& kv : g_thumbs) DwmUnregisterThumbnail(kv.second);
    g_thumbs.clear();
    DeregisterShellHookWindow(g_hwnd);
    if (g_vol) g_vol->Release();
    g_items.clear();
    ClearIcons();
    g_dock.Free();
    g_dragSurf.Free();
    g_barSurf.Free();
    delete g_font;
    delete g_barFont;
    delete g_barBold;
    delete g_iconFont;
    GdiplusShutdown(gtok);
    liquid::Stop();
    FreeGlass(g_glass);
    FreeGlass(g_barGlass);
    g_comp = nullptr;
    if (mtx) CloseHandle(mtx);
    return 0;
}

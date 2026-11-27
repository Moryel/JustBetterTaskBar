// Liquid glass: the screen area under the dock is captured with Windows.Graphics.Capture
// (our own windows are excluded from capture), and a pixel shader adds a light blur,
// lens-like refraction near the edges, slight color dispersion and a specular rim.
// The result is shown through a composition swap chain on the existing glass visual.
#pragma once
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#include "LiquidShader.h"

namespace liquid {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

struct State {
    std::mutex mtx;
    bool active = false, hasData = false;
    winrt::com_ptr<ID3D11Device> dev;
    winrt::com_ptr<ID3D11DeviceContext> ctx;
    winrt::com_ptr<ID3D11VertexShader> vs;
    winrt::com_ptr<ID3D11PixelShader> ps;
    winrt::com_ptr<ID3D11Buffer> cb;
    winrt::com_ptr<ID3D11SamplerState> smp;
    winrt::com_ptr<ID3D11Texture2D> band;
    winrt::com_ptr<ID3D11ShaderResourceView> bandSrv;
    winrt::com_ptr<IDXGISwapChain1> sc;
    int scW = 0, scH = 0;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice rtDev{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    winrt::event_token frameToken{};
    winrt::Windows::Graphics::SizeInt32 poolSize{};
    wuc::CompositionSurfaceBrush brush{nullptr};
    wuc::SpriteVisual visual{nullptr};
    HMONITOR mon = nullptr;
    RECT monRc{};
    RECT rc{};
    RECT bandRc{};
    float radius = 0, scale = 1, sat = 1.8f;
    int mode = 0;
    bool nearTint = false;
    float inX = 0, inY = -1;   // from the dock's screen edge towards the middle of the screen
    float bandInX = 0, bandInY = 0;   // the edge the current band was made for
    std::vector<HWND> ownWnds;
    bool light = false;
    HWND notify = nullptr;
    UINT notifyMsg = 0;
};
inline State g;
inline std::atomic<ULONGLONG> g_ownChange{0};
inline std::atomic<bool> g_paused{false}, g_borderFailed{false};
// Called whenever the app redraws or re-stacks its own windows.
inline void NoteOwnChange() { g_ownChange = GetTickCount64(); }
inline std::atomic<bool> g_accessAsked{false};

inline bool Active() { return g.active; }

static void RenderLocked() {
    if (!g.active || !g.hasData || !g.sc) return;
    int w = g.rc.right - g.rc.left, h = g.rc.bottom - g.rc.top;
    if (w <= 0 || h <= 0) return;
    if (w > g.scW || h > g.scH) {
        int nw = std::max(w + 64, g.scW), nh = std::max(h + 16, g.scH);
        if (FAILED(g.sc->ResizeBuffers(2, nw, nh, DXGI_FORMAT_B8G8R8A8_UNORM, 0))) return;
        g.scW = nw;
        g.scH = nh;
    }
    winrt::com_ptr<ID3D11Texture2D> bb;
    if (FAILED(g.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), bb.put_void()))) return;
    winrt::com_ptr<ID3D11RenderTargetView> rtv;
    if (FAILED(g.dev->CreateRenderTargetView(bb.get(), nullptr, rtv.put()))) return;

    CB c = MakeCB(w, h, (float)(g.rc.left - g.monRc.left - g.bandRc.left), (float)(g.rc.top - g.monRc.top - g.bandRc.top),
                  g.bandRc.right - g.bandRc.left, g.bandRc.bottom - g.bandRc.top, g.radius, g.scale, g.light, g.sat, g.mode,
                  g.inX, g.inY, g.nearTint);
    g.ctx->UpdateSubresource(g.cb.get(), 0, nullptr, &c, 0, 0);

    float clear[4] = {0, 0, 0, 0};
    g.ctx->ClearRenderTargetView(rtv.get(), clear);
    ID3D11RenderTargetView* rt = rtv.get();
    g.ctx->OMSetRenderTargets(1, &rt, nullptr);
    D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
    g.ctx->RSSetViewports(1, &vp);
    g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.ctx->IASetInputLayout(nullptr);
    g.ctx->VSSetShader(g.vs.get(), nullptr, 0);
    g.ctx->PSSetShader(g.ps.get(), nullptr, 0);
    ID3D11Buffer* b = g.cb.get();
    g.ctx->PSSetConstantBuffers(0, 1, &b);
    ID3D11ShaderResourceView* srv = g.bandSrv.get();
    g.ctx->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* sm = g.smp.get();
    g.ctx->PSSetSamplers(0, 1, &sm);
    g.ctx->Draw(3, 0);
    ID3D11RenderTargetView* none = nullptr;
    g.ctx->OMSetRenderTargets(1, &none, nullptr);
    NoteOwnChange();
    g.sc->Present(0, 0);
}

static void OnFrameImpl(wgc::Direct3D11CaptureFramePool const& pool);
static void OnFrame(wgc::Direct3D11CaptureFramePool const& pool, winrt::Windows::Foundation::IInspectable const&) {
    try { OnFrameImpl(pool); } catch (...) {}
}
// True when something changed inside the captured band. Frames for changes elsewhere on
// the screen (a video, another app) are dropped without touching the GPU.
static bool BandChanged(wgc::Direct3D11CaptureFrame const& frame) {
    if (!g.hasData) return true;
    try {
        auto rects = frame.DirtyRegions();
        if (!rects || rects.Size() == 0) return true;
        // Our own windows are excluded from capture, but drawing or re-stacking them still marks
        // their area dirty. Right after we touched them, changes that stay inside them are ignored,
        // or the glass would redraw forever; at other times they are real changes behind the dock.
        RECT own[8];
        int nOwn = 0;
        if (GetTickCount64() - g_ownChange.load() < 150)
        for (HWND h : g.ownWnds) {
            RECT w;
            if (h && IsWindowVisible(h) && GetWindowRect(h, &w) && nOwn < 8)
                own[nOwn++] = RECT{w.left - g.monRc.left, w.top - g.monRc.top, w.right - g.monRc.left, w.bottom - g.monRc.top};
        }
        for (auto const& r : rects) {
            RECT d{r.X, r.Y, r.X + r.Width, r.Y + r.Height}, x;
            bool mine = false;
            for (int i = 0; i < nOwn && !mine; i++) {
                UnionRect(&x, &d, &own[i]);
                mine = EqualRect(&x, &own[i]) != 0;
            }
            if (!mine && IntersectRect(&x, &d, &g.bandRc)) return true;
        }
        return false;
    } catch (...) {
        return true;
    }
}

static void OnFrameImpl(wgc::Direct3D11CaptureFramePool const& pool) {
    // While paused the frame stays in the one-buffer pool, so capture stalls at no cost.
    if (g_paused) return;
    auto frame = pool.TryGetNextFrame();
    if (!frame) return;
    // hand the buffer back to the pool right away instead of whenever the wrapper is released
    struct CloseFrame {
        wgc::Direct3D11CaptureFrame& f;
        ~CloseFrame() { try { f.Close(); } catch (...) {} }
    } closer{frame};
    std::lock_guard<std::mutex> lk(g.mtx);
    if (!g.active || !g.band) return;
    auto size = frame.ContentSize();
    if (size.Width != g.poolSize.Width || size.Height != g.poolSize.Height) {
        try { frame.Close(); } catch (...) {}
        g.poolSize = size;
        pool.Recreate(g.rtDev, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
        g.hasData = false;
        return;
    }
    if (!BandChanged(frame)) return;
    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> tex;
    if (FAILED(access->GetInterface(__uuidof(ID3D11Texture2D), tex.put_void()))) return;
    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    LONG maxW = std::min<LONG>((LONG)td.Width, size.Width), maxH = std::min<LONG>((LONG)td.Height, size.Height);
    D3D11_BOX box{(UINT)std::max<LONG>(g.bandRc.left, 0), (UINT)std::max<LONG>(g.bandRc.top, 0), 0,
                  (UINT)std::min<LONG>(g.bandRc.right, maxW), (UINT)std::min<LONG>(g.bandRc.bottom, maxH), 1};
    if (box.right > box.left && box.bottom > box.top) {
        g.ctx->CopySubresourceRegion(g.band.get(), 0, box.left - g.bandRc.left, box.top - g.bandRc.top, 0, tex.get(), 0, &box);
        g.ctx->GenerateMips(g.bandSrv.get());
        g.hasData = true;
    }
    RenderLocked();
}

// There is one session for the whole life of the pipeline. Closing a session or its pool does
// not give the frame buffer back on every system (8 MB per 1080p frame was lost each time),
// and a pool can't start a second session, so pausing never closes anything.
static void StopSession() {
    wgc::GraphicsCaptureSession session{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    {
        std::lock_guard<std::mutex> lk(g.mtx);
        session = g.session;
        pool = g.pool;
        if (pool && g.frameToken.value) pool.FrameArrived(g.frameToken);
        g.frameToken = {};
        g.session = nullptr;
        g.pool = nullptr;
    }
    try { if (session) session.Close(); } catch (...) {}
    try { if (pool) pool.Close(); } catch (...) {}
}

static bool StartSession() {
    if (!g.active || g.session || !g.item) return g.session != nullptr;
    try {
        auto size = g.item.Size();
        auto pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(g.rtDev, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
        auto session = pool.CreateCaptureSession(g.item);
        try { session.IsCursorCaptureEnabled(false); } catch (...) {}
        try { session.IsBorderRequired(false); g_borderFailed = false; } catch (...) { g_borderFailed = true; }
        try { session.DirtyRegionMode(wgc::GraphicsCaptureDirtyRegionMode::ReportOnly); } catch (...) {}
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            g.poolSize = size;
            g.frameToken = pool.FrameArrived(&OnFrame);
            g.pool = pool;
            g.session = session;
        }
        session.StartCapture();
        return true;
    } catch (...) {
        return false;
    }
}

static void RequestBorderless() {
    if (g_accessAsked.exchange(true)) return;
    HWND notify = g.notify;
    UINT msg = g.notifyMsg;
    std::thread([notify, msg] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless).get();
        } catch (...) {}
        if (notify) PostMessageW(notify, msg, 0, 0);
        winrt::uninit_apartment();
    }).detach();
}

// Called on the UI thread once the borderless access request has finished. Only a session that
// could not turn the capture border off is restarted, so most systems never restart at all.
inline void OnAccessReady() {
    if (!g.active || !g.session || !g_borderFailed) return;
    StopSession();
    StartSession();
}

static void EnsureBand(RECT need) {
    // a different screen edge starts a fresh band; growing the old one would end up covering the whole screen
    bool sameEdge = g.bandInX == g.inX && g.bandInY == g.inY;
    if (g.band && sameEdge && need.left >= g.bandRc.left && need.top >= g.bandRc.top && need.right <= g.bandRc.right && need.bottom <= g.bandRc.bottom)
        return;
    RECT nb = need;
    if (g.band && sameEdge) UnionRect(&nb, &need, &g.bandRc);
    g.bandInX = g.inX;
    g.bandInY = g.inY;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = std::max<LONG>(nb.right - nb.left, 1);
    d.Height = std::max<LONG>(nb.bottom - nb.top, 1);
    d.MipLevels = 5;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    winrt::com_ptr<ID3D11Texture2D> t;
    winrt::com_ptr<ID3D11ShaderResourceView> v;
    if (FAILED(g.dev->CreateTexture2D(&d, nullptr, t.put())) || FAILED(g.dev->CreateShaderResourceView(t.get(), nullptr, v.put())))
        return;
    g.band = t;
    g.bandSrv = v;
    g.bandRc = nb;
    g.hasData = false;   // filled by the next frame
}

// Picks up capturing after Pause(): taking the frame that waited in the pool lets new ones flow.
static void Resume() {
    if (!g_paused) return;
    g_paused = false;
    if (g.pool) {
        try { OnFrameImpl(g.pool); } catch (...) {}
    }
}

// rect: the glass window in screen pixels. The captured band runs along the whole screen edge
// the dock sits on and reaches well past the dock, so moving, magnifying or sliding the dock
// in and out needs no new frame.
inline void Place(RECT rect, float radius) {
    if (!g.active) return;
    {
        std::lock_guard<std::mutex> lk(g.mtx);
        g.rc = rect;
        g.radius = std::max(0.0f, radius);
        int pad = Pad(g.scale);
        LONG mw = g.monRc.right - g.monRc.left, mh = g.monRc.bottom - g.monRc.top;
        RECT r{rect.left - g.monRc.left - pad, rect.top - g.monRc.top - pad, rect.right - g.monRc.left + pad, rect.bottom - g.monRc.top + pad};
        LONG w = rect.right - rect.left, h = rect.bottom - rect.top;
        bool horizontal = w >= h;
        if (horizontal) {
            r.left = 0;
            r.right = mw;
            if ((r.top + r.bottom) / 2 > mh / 2) { r.bottom = mh; r.top -= 2 * h; g.inX = 0; g.inY = -1; }
            else { r.top = 0; r.bottom += 2 * h; g.inX = 0; g.inY = 1; }
        } else {
            r.top = 0;
            r.bottom = mh;
            if ((r.left + r.right) / 2 < mw / 2) { r.left = 0; r.right += 2 * w; g.inX = 1; g.inY = 0; }
            else { r.right = mw; r.left -= 2 * w; g.inX = -1; g.inY = 0; }
        }
        r.left = std::max<LONG>(r.left, 0);
        r.top = std::max<LONG>(r.top, 0);
        r.right = std::min<LONG>(r.right, mw);
        r.bottom = std::min<LONG>(r.bottom, mh);
        if (r.right > r.left && r.bottom > r.top) EnsureBand(r);
        RenderLocked();
    }
    if (!g.session) {
        g_paused = false;
        StartSession();
    } else {
        Resume();
    }
}

// The app's own windows: changes inside them may come from the app itself.
inline void SetOwnWindows(std::vector<HWND> wnds) {
    std::lock_guard<std::mutex> lk(g.mtx);
    g.ownWnds = std::move(wnds);
}

inline void SetLook(bool light, float sat, float scale, int mode, bool nearTint) {
    if (!g.active) return;
    std::lock_guard<std::mutex> lk(g.mtx);
    if (light == g.light && sat == g.sat && scale == g.scale && mode == g.mode && nearTint == g.nearTint) return;
    g.mode = mode;
    g.nearTint = nearTint;
    g.light = light;
    g.sat = sat;
    g.scale = scale;
    RenderLocked();
}

// Stops capturing while the glass is hidden (full screen, dock hidden); Place() resumes it.
inline void Pause() {
    if (g.active) g_paused = true;
}

inline void Stop() {
    if (!g.active) return;
    StopSession();
    g_paused = false;
    std::lock_guard<std::mutex> lk(g.mtx);
    g.active = false;
    g.hasData = false;
    if (g.visual) g.visual.Brush(nullptr);
    g.brush = nullptr;
    g.visual = nullptr;
    g.item = nullptr;
    g.rtDev = nullptr;
    g.sc = nullptr;
    g.scW = g.scH = 0;
    g.band = nullptr;
    g.bandSrv = nullptr;
    g.bandRc = RECT{};
    g.smp = nullptr;
    g.cb = nullptr;
    g.ps = nullptr;
    g.vs = nullptr;
    // D3D11 frees released resources lazily; flush so the memory really goes back now
    if (g.ctx) { g.ctx->ClearState(); g.ctx->Flush(); }
    g.ctx = nullptr;
    g.dev = nullptr;
}

// The shader compiler is only needed once at startup, so it is loaded for the compile
// and unloaded right after instead of staying in memory.
// The blobs belong to the compiler DLL, so their bytes are copied out before it is unloaded.
static bool CompileShaders(std::vector<char>& vs, std::vector<char>& ps) {
    HMODULE m = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) return false;
    auto compile = (pD3DCompile)GetProcAddress(m, "D3DCompile");
    bool ok = false;
    if (compile) {
        auto one = [&](const char* entry, const char* target, std::vector<char>& out) {
            winrt::com_ptr<ID3DBlob> code, err;
            if (FAILED(compile(kShader, strlen(kShader), nullptr, nullptr, nullptr, entry, target,
                               D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), err.put())))
                return false;
            auto p = (const char*)code->GetBufferPointer();
            out.assign(p, p + code->GetBufferSize());
            return true;
        };
        ok = one("VS", "vs_4_0", vs) && one("PS", "ps_4_0", ps);
    }
    FreeLibrary(m);
    return ok;
}

// Returns false (and leaves nothing behind) when capture or Direct3D is unavailable.
inline bool Start(HWND glassWnd, wuc::Compositor const& comp, wuc::SpriteVisual const& visual, float scale, bool light,
                  float sat, HWND notify, UINT notifyMsg) {
    if (g.active) return true;
    try {
        if (!wgc::GraphicsCaptureSession::IsSupported()) return false;
        g.notify = notify;
        g.notifyMsg = notifyMsg;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fl, ARRAYSIZE(fl),
                                               D3D11_SDK_VERSION, g.dev.put(), nullptr, g.ctx.put()));
        if (auto mt = g.dev.try_as<ID3D10Multithread>()) mt->SetMultithreadProtected(TRUE);

        std::vector<char> vsb, psb;
        if (!CompileShaders(vsb, psb)) throw winrt::hresult_error(E_FAIL);
        winrt::check_hresult(g.dev->CreateVertexShader(vsb.data(), vsb.size(), nullptr, g.vs.put()));
        winrt::check_hresult(g.dev->CreatePixelShader(psb.data(), psb.size(), nullptr, g.ps.put()));
        D3D11_BUFFER_DESC bd{sizeof(CB), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        winrt::check_hresult(g.dev->CreateBuffer(&bd, nullptr, g.cb.put()));
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        winrt::check_hresult(g.dev->CreateSamplerState(&sd, g.smp.put()));

        auto dxgiDev = g.dev.as<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> insp;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDev.get(), insp.put()));
        g.rtDev = insp.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

        winrt::com_ptr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDev->GetAdapter(adapter.put()));
        winrt::com_ptr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()));
        g.monRc = RECT{};
        g.mon = MonitorFromWindow(glassWnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(g.mon, &mi);
        g.monRc = mi.rcMonitor;
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.Width = g.scW = std::max<LONG>(mi.rcMonitor.right - mi.rcMonitor.left, 64);
        scd.Height = g.scH = (int)std::ceil(160 * scale);
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.Scaling = DXGI_SCALING_STRETCH;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        winrt::check_hresult(factory->CreateSwapChainForComposition(g.dev.get(), &scd, nullptr, g.sc.put()));

        auto ci = comp.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionSurface> surf;
        winrt::check_hresult(ci->CreateCompositionSurfaceForSwapChain(g.sc.get(), surf.put()));
        g.brush = comp.CreateSurfaceBrush(surf.as<wuc::ICompositionSurface>());
        g.brush.Stretch(wuc::CompositionStretch::None);
        g.brush.HorizontalAlignmentRatio(0);
        g.brush.VerticalAlignmentRatio(0);

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForMonitor(g.mon, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                       winrt::put_abi(g.item)));
        g.scale = scale;
        g.light = light;
        g.sat = sat;
        g.visual = visual;
        g.active = true;
        visual.Brush(g.brush);
        RequestBorderless();
        if (auto d3 = g.dev.try_as<IDXGIDevice3>()) d3->Trim();
        return true;
    } catch (...) {
        g.active = true;
        Stop();
        return false;
    }
}

}  // namespace liquid

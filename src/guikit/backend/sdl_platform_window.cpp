#include "guikit/backend/sdl_platform_window.hpp"
#include "guikit/backend/sdl_canvas.hpp"
#include "guikit/context.hpp"
#include <SDL3/SDL.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef DrawText
#undef DrawText
#endif
#ifdef small
#undef small
#endif
#ifdef RGB
#undef RGB
#endif
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "keel/format.h"
#include "keel/math.h"
#include "keel/string.h"
#include "keel/time.h"

namespace guikit {

namespace {

[[nodiscard]] inline keel::u32 AtomicExchangeU32(keel::u32& slot, keel::u32 val) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    return keel::as<keel::u32>(_InterlockedExchange(reinterpret_cast<volatile long*>(&slot), keel::as<long>(val))); // keel-lint: ignore
#else
    return __atomic_exchange_n(&slot, val, __ATOMIC_ACQ_REL);
#endif
}

[[nodiscard]] inline keel::u32 AtomicLoadAcquireU32(const keel::u32& slot) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    const keel::u32 v = *static_cast<const volatile keel::u32*>(&slot);
    MemoryBarrier();
    return v;
#else
    return __atomic_load_n(&slot, __ATOMIC_ACQUIRE);
#endif
}

#ifndef __EMSCRIPTEN__
bool SDLCALL LiveResizeEventWatchThunk(void* userdata, SDL_Event* event) {
    return SdlPlatformWindow::LiveResizeEventWatch(userdata, event);
}
#endif

#if defined(_WIN32)
struct SdlWin32DwmMargins {
    keel::i32 cxLeftWidth;
    keel::i32 cxRightWidth;
    keel::i32 cyTopHeight;
    keel::i32 cyBottomHeight;
};
using PfnSdlDwmExtendFrame = HRESULT(WINAPI*)(HWND, const SdlWin32DwmMargins*);
using PfnSdlDwmSetWindowAttr = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

void ApplyWin32CustomTitleBarShadow(SDL_Window* window, bool enabled) noexcept {
    if (!window) return;
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    auto extendFn = reinterpret_cast<PfnSdlDwmExtendFrame>(::GetProcAddress(dwm, "DwmExtendFrameIntoClientArea"));
    if (extendFn) {
        const SdlWin32DwmMargins margins{0, 0, 0, enabled ? 1 : 0};
        (void)extendFn(hwnd, &margins);
    }
    ::FreeLibrary(dwm);
}

void ApplyWin32DwmAttr(SDL_Window* window, DWORD attr, DWORD value) noexcept {
    if (!window) return;
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    auto setAttr = reinterpret_cast<PfnSdlDwmSetWindowAttr>(::GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (setAttr) {
        (void)setAttr(hwnd, attr, &value, sizeof(value));
    }
    ::FreeLibrary(dwm);
}

keel::f32 QueryHdcPixelsPerMm(HDC hdc) noexcept {
    if (hdc == nullptr) {
        return 0.0f;
    }
    const keel::i32 widthMm = ::GetDeviceCaps(hdc, HORZSIZE);
    const keel::i32 heightMm = ::GetDeviceCaps(hdc, VERTSIZE);
    const keel::i32 widthPx = ::GetDeviceCaps(hdc, DESKTOPHORZRES);
    const keel::i32 heightPx = ::GetDeviceCaps(hdc, DESKTOPVERTRES);
    if (widthMm > 50 && heightMm > 50 && widthPx > 320 && heightPx > 240) {
        const keel::f32 pxPerMmX = keel::as<keel::f32>(widthPx) / keel::as<keel::f32>(widthMm);
        const keel::f32 pxPerMmY = keel::as<keel::f32>(heightPx) / keel::as<keel::f32>(heightMm);
        const keel::f32 avgPxPerMm = 0.5f * (pxPerMmX + pxPerMmY);
        if (avgPxPerMm >= 1.5f && avgPxPerMm <= 25.0f) {
            return avgPxPerMm;
        }
    }
    return 0.0f;
}
#endif

#ifdef __EMSCRIPTEN__
bool GetEmscriptenCanvasPhysicalSize(keel::i32& outW, keel::i32& outH, bool highDpi) noexcept {
    keel::f64 cssW = 0.0;
    keel::f64 cssH = 0.0;
    if (emscripten_get_element_css_size("#canvas", &cssW, &cssH) == EMSCRIPTEN_RESULT_SUCCESS &&
        cssW >= 200.0 && cssH >= 200.0) {
        keel::f64 dpr = highDpi ? emscripten_get_device_pixel_ratio() : 1.0;
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
        outW = keel::round_i32(cssW * dpr);
        outH = keel::round_i32(cssH * dpr);
        return true;
    }
    return false;
}
#endif

[[nodiscard]] SDL_PixelFormat ResolveSdlPixelFormat(SurfacePixelFormat fmt) noexcept {
    switch (fmt) {
        case SurfacePixelFormat::ARGB8888_Premul:
            return SDL_PIXELFORMAT_ARGB8888;
        case SurfacePixelFormat::ABGR8888_Premul:
        default:
            return SDL_PIXELFORMAT_ABGR8888;
    }
}

[[nodiscard]] keel::String NormalizeSdlFilterPattern(keel::StringRef pattern) {
    // Convert "*.cpp;*.hpp;*.h" or "*.*" into SDL3's bare-extension format ("cpp;hpp;h" / "*")
    keel::String out;
    keel::usize pos = 0u;
    while (pos < pattern.size()) {
        keel::usize sep = pattern.find(';', pos);
        if (sep == keel::StringRef::npos) sep = pattern.size();
        keel::StringRef tok = pattern.substr(pos, sep - pos).trim();
        if (!tok.empty()) {
            if (tok == "*.*" || tok == "*") {
                tok = "*";
            } else {
                if (tok.size() >= 2u && tok[0] == '*' && tok[1] == '.') {
                    tok = tok.substr(2u);
                } else if (tok.size() >= 1u && tok[0] == '.') {
                    tok = tok.substr(1u);
                }
            }
            if (!tok.empty()) {
                if (!out.empty()) out.push_back(';');
                out.append(tok);
            }
        }
        pos = (sep < pattern.size()) ? (sep + 1u) : pattern.size();
    }
    if (out.empty()) {
        out.assign("*");
    }
    return out;
}

#ifndef __EMSCRIPTEN__
SDL_HitTestResult SDLCALL SdlCustomTitleBarHitTest(
    SDL_Window* /*win*/,
    const SDL_Point* area,
    void* userdata
) {
    auto* self = static_cast<SdlPlatformWindow*>(userdata);
    if (!self || !area) {
        return SDL_HITTEST_NORMAL;
    }
    const Vec2 phys = SdlWindowToPhysicalPixels(
        {keel::as<keel::f32>(area->x), keel::as<keel::f32>(area->y)},
        self->GetSdlWindow() ? SDL_GetWindowPixelDensity(self->GetSdlWindow()) : 1.0f
    );
    const keel::f32 dpi = self->GetDpiScale();
    const keel::f32 safeDpi = (dpi > 0.1f) ? dpi : 1.0f;
    const Vec2 logical{phys.x / safeDpi, phys.y / safeDpi};
    switch (self->DispatchHitTest(logical)) {
        case WindowHitRegion::Caption:           return SDL_HITTEST_DRAGGABLE;
        case WindowHitRegion::ResizeTopLeft:     return SDL_HITTEST_RESIZE_TOPLEFT;
        case WindowHitRegion::ResizeTop:         return SDL_HITTEST_RESIZE_TOP;
        case WindowHitRegion::ResizeTopRight:    return SDL_HITTEST_RESIZE_TOPRIGHT;
        case WindowHitRegion::ResizeRight:       return SDL_HITTEST_RESIZE_RIGHT;
        case WindowHitRegion::ResizeBottomRight: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        case WindowHitRegion::ResizeBottom:      return SDL_HITTEST_RESIZE_BOTTOM;
        case WindowHitRegion::ResizeBottomLeft:  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        case WindowHitRegion::ResizeLeft:        return SDL_HITTEST_RESIZE_LEFT;
        case WindowHitRegion::MinimizeButton:
        case WindowHitRegion::MaximizeButton:
        case WindowHitRegion::CloseButton:
        case WindowHitRegion::Client:
        default:                                 return SDL_HITTEST_NORMAL;
    }
}
#endif

} // namespace

struct SdlPlatformWindow::AsyncDialogState {
    struct CompletedEntry {
        FileDialogResult result{};
        keel::Function<void(FileDialogResult)> onDone{};
    };

    SDL_AtomicInt refCount{1};
    SDL_Mutex* mutex{nullptr};
    SdlPlatformWindow* owner{nullptr};
    bool windowAlive{true};
    keel::Array<CompletedEntry> completed;

    explicit AsyncDialogState(SdlPlatformWindow* win) noexcept : owner(win) {
        SDL_SetAtomicInt(&refCount, 1);
        mutex = SDL_CreateMutex();
    }
    ~AsyncDialogState() {
        if (mutex) {
            SDL_DestroyMutex(mutex);
            mutex = nullptr;
        }
    }

    void Retain() noexcept {
        (void)SDL_AddAtomicInt(&refCount, 1);
    }

    static void Release(AsyncDialogState* state) noexcept {
        if (!state) return;
        if (SDL_AddAtomicInt(&state->refCount, -1) == 1) {
            delete state;
        }
    }

    void DetachOwner() noexcept {
        if (mutex) SDL_LockMutex(mutex);
        windowAlive = false;
        owner = nullptr;
        completed.clear();
        if (mutex) SDL_UnlockMutex(mutex);
    }

    void EnqueueCompleted(FileDialogResult res, keel::Function<void(FileDialogResult)> onDone) {
        if (!mutex) return;
        SDL_LockMutex(mutex);
        if (windowAlive && owner) {
            completed.push_back(CompletedEntry{
                .result = keel::move(res),
                .onDone = keel::move(onDone)
            });
            owner->WakeUp();
        }
        SDL_UnlockMutex(mutex);
    }
};

keel::f32 QueryDisplayPixelsPerMm(SDL_Window* window) noexcept {
#if defined(_WIN32)
    if (window != nullptr) {
        const SDL_PropertiesID props = SDL_GetWindowProperties(window);
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (hwnd != nullptr) {
            HMONITOR hmon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (hmon != nullptr) {
                MONITORINFOEXW mi{};
                mi.cbSize = sizeof(mi);
                if (::GetMonitorInfoW(hmon, &mi)) {
                    HDC ic = ::CreateICW(L"DISPLAY", mi.szDevice, nullptr, nullptr);
                    if (ic != nullptr) {
                        const keel::f32 monitorPxPerMm = QueryHdcPixelsPerMm(ic);
                        ::DeleteDC(ic);
                        if (monitorPxPerMm > 0.0f) {
                            return monitorPxPerMm;
                        }
                    }
                }
            }
        }
    }
    HDC screenDc = ::GetDC(nullptr);
    if (screenDc != nullptr) {
        const keel::f32 primaryPxPerMm = QueryHdcPixelsPerMm(screenDc);
        ::ReleaseDC(nullptr, screenDc);
        if (primaryPxPerMm > 0.0f) {
            return primaryPxPerMm;
        }
    }
#elif defined(__EMSCRIPTEN__)
    const keel::f64 dpr = emscripten_get_device_pixel_ratio();
    if (dpr > 0.1) {
        return keel::as<keel::f32>((96.0 * dpr) / 25.4);
    }
#endif
    const keel::f32 displayScale = (window != nullptr) ? SDL_GetWindowDisplayScale(window) : 1.0f;
    const keel::f32 safeScale = (displayScale > 0.1f) ? displayScale : 1.0f;
    return (96.0f * safeScale) / 25.4f;
}

keel::UniquePtr<IPlatformWindow> CreateSdlPlatformWindow() {
    return keel::make_unique<SdlPlatformWindow>();
}

void RegisterSdlPlatformWindow() noexcept {
    RegisterDefaultPlatform(PlatformRegistration{
        .createWindow = &CreateSdlPlatformWindow,
        .createCanvas = nullptr,
        .graphicsApi = GraphicsApi::None
    });
}

SdlPlatformWindow::~SdlPlatformWindow() {
    Destroy();
}

void SdlPlatformWindow::UpdateDisplayMetrics() noexcept {
    m_displayPxPerMm = QueryDisplayPixelsPerMm(m_window);
    const keel::f32 rawPixelDensity = (m_window != nullptr) ? SDL_GetWindowPixelDensity(m_window) : 1.0f;
    const keel::f32 pixelDensity = (rawPixelDensity > 0.01f) ? rawPixelDensity : 1.0f;

    if (m_desc.highDpi) {
#ifdef __EMSCRIPTEN__
        const keel::f64 dpr = emscripten_get_device_pixel_ratio();
        m_dpiScale = (dpr > 0.1) ? keel::as<keel::f32>(dpr) : 1.0f;
#else
        const keel::f32 dispScale = (m_window != nullptr) ? SDL_GetWindowDisplayScale(m_window) : 1.0f;
        m_dpiScale = (dispScale > 0.1f) ? dispScale : 1.0f;
#endif
    } else {
        m_dpiScale = 1.0f;
    }
    m_windowPixelDensity = pixelDensity;
}

bool SdlPlatformWindow::Create(
    const PlatformWindowDesc& desc,
    Context& ui,
    WindowHostCallbacks callbacks
) {
    Destroy();

    m_desc = desc;
    m_callbacks = keel::move(callbacks);
    m_boundUi = &ui;
    m_attachedCanvas = nullptr;
    m_width = keel::max(1, desc.width);
    m_height = keel::max(1, desc.height);
    m_vsyncEnabled = desc.vsync;
    m_shouldExit = false;
    m_minimized = false;
    (void)AtomicExchangeU32(m_wakePending, 0u);

#if defined(__linux__) && !defined(__ANDROID__)
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11,wayland", SDL_HINT_DEFAULT);
#endif

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        keel::eprintln("Failed to initialize SDL3 video subsystem: {}", SDL_GetError());
        return false;
    }
    m_sdlVideoInitialized = true;

#ifdef __EMSCRIPTEN__
    GetEmscriptenCanvasPhysicalSize(m_width, m_height, m_desc.highDpi);
    const SDL_WindowFlags windowFlags = 0;
#else
    SDL_WindowFlags windowFlags = 0;
    if (m_desc.resizable) {
        windowFlags |= SDL_WINDOW_RESIZABLE;
    }
    if (m_desc.maximized) {
        windowFlags |= SDL_WINDOW_MAXIMIZED;
    }
    if (m_desc.fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN;
    }
    if (m_desc.borderless || (m_desc.customTitleBar && !m_desc.fullscreen)) {
        windowFlags |= SDL_WINDOW_BORDERLESS;
    }
    if (m_desc.highDpi) {
        windowFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif

    if (!SDL_CreateWindowAndRenderer(m_desc.title.c_str(), m_width, m_height,
                                     windowFlags, &m_window, &m_renderer)) {
#ifndef __EMSCRIPTEN__
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, nullptr);
        if (!SDL_CreateWindowAndRenderer(m_desc.title.c_str(), m_width, m_height,
                                         windowFlags, &m_window, &m_renderer)) {
            keel::eprintln("Failed to create SDL window & renderer: {}", SDL_GetError());
            Destroy();
            return false;
        }
#else
        keel::eprintln("Failed to create SDL window & renderer: {}", SDL_GetError());
        Destroy();
        return false;
#endif
    }

    UpdateDisplayMetrics();

#ifndef __EMSCRIPTEN__
    keel::f32 winCoordScale = 1.0f;
    if (m_desc.highDpi) {
        const keel::f32 rawPd = SDL_GetWindowPixelDensity(m_window);
        const keel::f32 pd = (rawPd > 0.01f) ? rawPd : 1.0f;
        winCoordScale = keel::max(1.0f, m_dpiScale / pd);
        if (winCoordScale > 1.01f) {
            SDL_SetWindowSize(
                m_window,
                keel::max(1, keel::round_i32(keel::as<keel::f32>(m_desc.width) * winCoordScale)),
                keel::max(1, keel::round_i32(keel::as<keel::f32>(m_desc.height) * winCoordScale))
            );
        }
    }
    if (m_desc.minWidth > 0 && m_desc.minHeight > 0) {
        SDL_SetWindowMinimumSize(
            m_window,
            keel::max(1, keel::round_i32(keel::as<keel::f32>(m_desc.minWidth) * winCoordScale)),
            keel::max(1, keel::round_i32(keel::as<keel::f32>(m_desc.minHeight) * winCoordScale))
        );
    }
    SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
#else
    if (m_desc.minWidth > 0 && m_desc.minHeight > 0) {
        SDL_SetWindowMinimumSize(m_window, m_desc.minWidth, m_desc.minHeight);
    }
    SDL_SetWindowSize(m_window, m_width, m_height);
#endif
    SDL_SetRenderVSync(m_renderer, m_vsyncEnabled ? 1 : 0);
    m_textInputActive = false;
    m_lastImeCaretPx = {};
    m_pendingDropFiles.clear();

    keel::i32 actualW = m_width;
    keel::i32 actualH = m_height;
    if (SDL_GetWindowSizeInPixels(m_window, &actualW, &actualH) && actualW > 0 && actualH > 0) {
        m_width = actualW;
        m_height = actualH;
    }
    UpdateDisplayMetrics();

    const bool shadersAvailable = m_gpuShaderEngine.Initialize(m_renderer);
    m_gpuShadersEnabled = m_desc.gpuShaders && shadersAvailable;

    ui.SetClipboardCallbacks(
        []() -> keel::String {
            char* clip = SDL_GetClipboardText();
            if (!clip) return keel::String{};
            keel::String res(clip);
            SDL_free(clip);
            return res;
        },
        [](keel::StringRef text) {
            const keel::SmallString<256> s(text);
            SDL_SetClipboardText(s.c_str());
        }
    );

    m_lastLiveResizeTimeNs = keel::monotonic_ns();
    m_lastEmscriptenRenderTimeNs = m_lastLiveResizeTimeNs;

    m_dialogState = new AsyncDialogState(this);
    m_focused = true;

#ifndef __EMSCRIPTEN__
    if (m_desc.customTitleBar && !m_desc.fullscreen) {
        (void)SDL_SetWindowHitTest(m_window, &SdlCustomTitleBarHitTest, this);
#if defined(_WIN32)
        ApplyWin32CustomTitleBarShadow(m_window, true);
#endif
    }
    SDL_AddEventWatch(&LiveResizeEventWatchThunk, this);
    m_eventWatchInstalled = true;
#endif

    return true;
}

void SdlPlatformWindow::Destroy() noexcept {
#ifndef __EMSCRIPTEN__
    if (m_eventWatchInstalled) {
        SDL_RemoveEventWatch(&LiveResizeEventWatchThunk, this);
        m_eventWatchInstalled = false;
    }
#endif

    m_gpuShaderEngine.Shutdown();

    if (auto* sdlCanvas = dynamic_cast<SdlCanvas*>(m_attachedCanvas); sdlCanvas != nullptr) {
        sdlCanvas->SetRenderer(nullptr);
    }
    m_attachedCanvas = nullptr;

    if (m_screenTexture != nullptr) {
        SDL_DestroyTexture(m_screenTexture);
        m_screenTexture = nullptr;
    }
    m_texWidth = 0;
    m_texHeight = 0;

    if (m_renderer != nullptr) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window != nullptr) {
#ifndef __EMSCRIPTEN__
        (void)SDL_SetWindowHitTest(m_window, nullptr, nullptr);
#endif
        if (m_textInputActive) {
            SDL_StopTextInput(m_window);
            m_textInputActive = false;
        }
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_dialogState != nullptr) {
        m_dialogState->DetachOwner();
        AsyncDialogState::Release(m_dialogState);
        m_dialogState = nullptr;
    }

    for (SDL_Cursor*& cur : m_cursors) {
        if (cur != nullptr) {
            SDL_DestroyCursor(cur);
            cur = nullptr;
        }
    }
    m_activeCursor = CursorType::Default;
    m_boundUi = nullptr;

    if (m_sdlVideoInitialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_sdlVideoInitialized = false;
    }
}

keel::UniquePtr<ICanvas> SdlPlatformWindow::CreateDefaultCanvas() {
    if (auto vec = CreateVectorCanvas(true)) {
        return vec;
    }
    return keel::make_unique<SdlCanvas>(m_renderer);
}

void SdlPlatformWindow::AttachCanvas(ICanvas& canvas) noexcept {
    m_attachedCanvas = &canvas;
    if (auto* sdlCanvas = dynamic_cast<SdlCanvas*>(&canvas); sdlCanvas != nullptr) {
        sdlCanvas->SetRenderer(m_renderer);
    }
    canvas.SetGpuShadersEnabled(m_gpuShadersEnabled);
}

bool SdlPlatformWindow::UsesMainLoopCallback() const noexcept {
#ifdef __EMSCRIPTEN__
    return true;
#else
    return false;
#endif
}

void SdlPlatformWindow::RunMainLoopCallback(keel::Function<bool()> stepFn) {
#ifdef __EMSCRIPTEN__
    m_emscriptenStepFn = keel::move(stepFn);
    emscripten_set_main_loop_arg(
        [](void* arg) {
            auto* self = static_cast<SdlPlatformWindow*>(arg);
            if (!self->m_emscriptenStepFn || !self->m_emscriptenStepFn()) {
                emscripten_cancel_main_loop();
            }
        },
        this,
        0,
        true
    );
#else
    (void)stepFn;
#endif
}

bool SdlPlatformWindow::PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) {
    m_skipRenderThisStep = false;
    if (m_shouldExit) {
        return false;
    }

    if (AtomicExchangeU32(m_wakePending, 0u) != 0u) {
        ui.RequestRepaint(1);
    }
    FlushCompletedDialogs(ui);

#ifdef __EMSCRIPTEN__
    keel::i32 targetW = m_width;
    keel::i32 targetH = m_height;
    if (GetEmscriptenCanvasPhysicalSize(targetW, targetH, m_desc.highDpi)) {
        if (targetW != m_width || targetH != m_height) {
            SDL_SetWindowSize(m_window, targetW, targetH);
        }
    }
    UpdateDisplayMetrics();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ProcessEvent(event, ui);
    }

    if (m_shouldExit) {
        return false;
    }

    if (reactiveMode && !ui.NeedsRepaint()) {
        if (sleepTimeoutMs <= 0) {
            m_skipRenderThisStep = true;
            return true;
        }
        const keel::u64 nowNs = keel::monotonic_ns();
        const keel::f64 elapsedMs = keel::as<keel::f64>(nowNs - m_lastEmscriptenRenderTimeNs) * 1e-6;
        if (elapsedMs < keel::as<keel::f64>(sleepTimeoutMs)) {
            m_skipRenderThisStep = true;
            return true;
        }
    }
    m_lastEmscriptenRenderTimeNs = keel::monotonic_ns();
    return !m_shouldExit;
#else
    if ((reactiveMode && !ui.NeedsRepaint() && AtomicLoadAcquireU32(m_wakePending) == 0u) || m_minimized) {
        keel::i32 waitMs = m_minimized ? 250 : sleepTimeoutMs;
        if (!m_minimized && m_gpuShaderEngine.HasIdleCachedResources() && (waitMs <= 0 || waitMs > 500)) {
            waitMs = 500;
        }
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, waitMs)) {
            ProcessEvent(event, ui);
            while (SDL_PollEvent(&event)) {
                ProcessEvent(event, ui);
            }
        }
    } else {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ProcessEvent(event, ui);
        }
    }
    if (AtomicExchangeU32(m_wakePending, 0u) != 0u) {
        ui.RequestRepaint(1);
    }
    FlushCompletedDialogs(ui);
    return !m_shouldExit;
#endif
}

void SdlPlatformWindow::WakeUp() noexcept {
    if (!m_sdlVideoInitialized || m_shouldExit) {
        return;
    }
    if (AtomicExchangeU32(m_wakePending, 1u) == 0u) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_USER;
        SDL_PushEvent(&ev);
    }
}

bool SdlPlatformWindow::LiveResizeEventWatch(void* userdata, SDL_Event* event) {
    auto* self = static_cast<SdlPlatformWindow*>(userdata);
    if (!self || self->m_shouldExit || !self->m_boundUi || !event || !self->m_boundUi->IsLiveResizeEnabled()) {
        return true;
    }
    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        if (event->window.data1 > 0 && event->window.data2 > 0) {
            self->m_width = event->window.data1;
            self->m_height = event->window.data2;
            self->UpdateDisplayMetrics();
            self->m_boundUi->InvalidateAll();
            self->m_boundUi->RequestRepaint(3);
            if (self->m_callbacks.onResize) {
                self->m_callbacks.onResize(self->m_width, self->m_height);
            }
        }
        return true;
    }
    if (event->type == SDL_EVENT_WINDOW_EXPOSED &&
        (keel::as<keel::u32>(self->m_width) != self->m_texWidth ||
         keel::as<keel::u32>(self->m_height) != self->m_texHeight)) {
        const keel::u64 nowNs = keel::monotonic_ns();
        const keel::f64 elapsedMs = keel::as<keel::f64>(nowNs - self->m_lastLiveResizeTimeNs) * 1e-6;
        if (elapsedMs >= 16.0 && self->m_callbacks.onLiveResizeRender) {
            self->m_lastLiveResizeTimeNs = nowNs;
            self->m_isLiveResizeFrame = true;
            self->m_callbacks.onLiveResizeRender();
            self->m_isLiveResizeFrame = false;
        }
    }
    return true;
}

void SdlPlatformWindow::ProcessEvent(const SDL_Event& event, Context& ui) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (!m_callbacks.onCloseRequested || m_callbacks.onCloseRequested()) {
                m_shouldExit = true;
            }
            break;

        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_HIDDEN:
            m_minimized = true;
            break;

        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_MAXIMIZED:
            m_minimized = false;
            UpdateDisplayMetrics();
            ui.InvalidateAll();
            ui.RequestRepaint(2);
            break;

        case SDL_EVENT_WINDOW_EXPOSED:
            m_minimized = false;
            ui.RequestRepaint(1);
            break;

        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            UpdateDisplayMetrics();
            ui.InvalidateAll();
            ui.RequestRepaint(2);
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            m_focused = true;
            ui.InvalidateAll();
            ui.RequestRepaint(2);
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            m_focused = false;
            ui.OnTextCompositionEnd();
            ui.SetKeyModifiers({false, false, false});
            if (m_pressedMouseMask != 0) {
                for (keel::i32 btn = 1; btn <= 8; ++btn) {
                    if ((m_pressedMouseMask & (1u << btn)) != 0) {
                        ui.OnMouseUp(btn);
                    }
                }
                m_pressedMouseMask = 0;
                SDL_CaptureMouse(false);
            }
            ui.InvalidateAll();
            ui.RequestRepaint(2);
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (event.window.data1 > 0 && event.window.data2 > 0) {
                m_width = event.window.data1;
                m_height = event.window.data2;
                UpdateDisplayMetrics();
                ui.InvalidateAll();
                ui.RequestRepaint(3);
                if (m_callbacks.onResize) {
                    m_callbacks.onResize(m_width, m_height);
                }
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            ui.OnMouseMove(ToPhysicalMousePos(event.motion.x, event.motion.y));
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            ui.OnMouseMove(ToPhysicalMousePos(event.button.x, event.button.y));
            const SDL_Keymod mod = SDL_GetModState();
            ui.SetKeyModifiers({
                .ctrl  = (mod & SDL_KMOD_CTRL) != 0,
                .shift = (mod & SDL_KMOD_SHIFT) != 0,
                .alt   = (mod & SDL_KMOD_ALT) != 0
            });
            if (event.button.button >= 1 && event.button.button <= 8) {
                m_pressedMouseMask |= (1u << event.button.button);
                SDL_CaptureMouse(true);
            }
            ui.OnMouseDown(event.button.button);
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
            ui.OnMouseMove(ToPhysicalMousePos(event.button.x, event.button.y));
            if (event.button.button >= 1 && event.button.button <= 8) {
                m_pressedMouseMask &= ~(1u << event.button.button);
                if (m_pressedMouseMask == 0) {
                    SDL_CaptureMouse(false);
                }
            }
            ui.OnMouseUp(event.button.button);
            break;

        case SDL_EVENT_MOUSE_WHEEL: {
            ui.OnMouseMove(ToPhysicalMousePos(event.wheel.mouse_x, event.wheel.mouse_y));
            const SDL_Keymod mod = SDL_GetModState();
            ui.SetKeyModifiers({
                .ctrl  = (mod & SDL_KMOD_CTRL) != 0,
                .shift = (mod & SDL_KMOD_SHIFT) != 0,
                .alt   = (mod & SDL_KMOD_ALT) != 0
            });
            ui.OnMouseWheel(event.wheel.x, event.wheel.y);
            break;
        }

        case SDL_EVENT_TEXT_INPUT: {
            const SDL_Keymod mod = SDL_GetModState();
            if ((mod & SDL_KMOD_CTRL) && !(mod & SDL_KMOD_ALT)) {
                break;
            }
            ui.OnTextInput(event.text.text);
            break;
        }

        case SDL_EVENT_TEXT_EDITING: {
            const char* preedit = event.edit.text ? event.edit.text : "";
            ui.OnTextComposition(
                keel::StringRef(preedit),
                keel::as<keel::i32>(event.edit.start),
                keel::as<keel::i32>(event.edit.length)
            );
            break;
        }

        case SDL_EVENT_DROP_BEGIN:
            m_pendingDropFiles.clear();
            m_pendingDropPos = ToPhysicalMousePos(event.drop.x, event.drop.y);
            break;

        case SDL_EVENT_DROP_POSITION:
            m_pendingDropPos = ToPhysicalMousePos(event.drop.x, event.drop.y);
            break;

        case SDL_EVENT_DROP_FILE:
            if (event.drop.data && event.drop.data[0] != '\0') {
                m_pendingDropPos = ToPhysicalMousePos(event.drop.x, event.drop.y);
                m_pendingDropFiles.push_back(keel::String(event.drop.data));
            }
            break;

        case SDL_EVENT_DROP_COMPLETE:
            if (!m_pendingDropFiles.empty()) {
                keel::Array<keel::StringRef> refs;
                refs.reserve(m_pendingDropFiles.size());
                for (const keel::String& s : m_pendingDropFiles) {
                    refs.push_back(s.as_ref());
                }
                ui.OnFilesDropped(refs.as_ref(), m_pendingDropPos);
                m_pendingDropFiles.clear();
            }
            break;

        case SDL_EVENT_KEY_UP: {
            const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
            const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            const bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;
            ui.SetKeyModifiers({ctrl, shift, alt});
            break;
        }

        case SDL_EVENT_KEY_DOWN: {
            const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
            const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            const bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;
            const KeyModifiers mods{ctrl, shift, alt};
            ui.SetKeyModifiers(mods);

            if (m_desc.debugShortcuts) {
                if ((ctrl && shift && event.key.key == SDLK_C) || (ctrl && event.key.key == SDLK_I)) {
                    ui.SetInspectMode(!ui.IsInspectMode());
                    break;
                }
                if (ctrl && event.key.key == SDLK_B) {
                    ui.SetDebugBounds(!ui.IsDebugBounds());
                    break;
                }
            }

            Key key = Key::None;
            switch (event.key.key) {
                case SDLK_BACKSPACE: key = Key::Backspace; break;
                case SDLK_DELETE:    key = Key::Delete; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:  key = Key::Enter; break;
                case SDLK_ESCAPE:    key = Key::Escape; break;
                case SDLK_TAB:       key = Key::Tab; break;
                case SDLK_SPACE:     key = Key::Space; break;
                case SDLK_LEFT:      key = Key::Left; break;
                case SDLK_RIGHT:     key = Key::Right; break;
                case SDLK_UP:        key = Key::Up; break;
                case SDLK_DOWN:      key = Key::Down; break;
                case SDLK_HOME:      key = Key::Home; break;
                case SDLK_END:       key = Key::End; break;
                case SDLK_PAGEUP:    key = Key::PageUp; break;
                case SDLK_PAGEDOWN:  key = Key::PageDown; break;
                case SDLK_A:         if (ctrl) key = Key::A; break;
                case SDLK_C:         if (ctrl) key = Key::C; break;
                case SDLK_D:         if (ctrl) key = Key::D; break;
                case SDLK_F:         if (ctrl) key = Key::F; break;
                case SDLK_G:         if (ctrl) key = Key::G; break;
                case SDLK_H:         if (ctrl) key = Key::H; break;
                case SDLK_K:         if (ctrl) key = Key::K; break;
                case SDLK_V:         if (ctrl) key = Key::V; break;
                case SDLK_X:         if (ctrl) key = Key::X; break;
                case SDLK_Y:         if (ctrl) key = Key::Y; break;
                case SDLK_Z:         if (ctrl) key = Key::Z; break;
                case SDLK_SLASH:
                case SDLK_KP_DIVIDE: if (ctrl) key = Key::Slash; break;
                case SDLK_EQUALS:
                case SDLK_PLUS:
                case SDLK_KP_PLUS:   if (ctrl) key = Key::Plus; break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS:  if (ctrl) key = Key::Minus; break;
                case SDLK_0:
                case SDLK_KP_0:      if (ctrl) key = Key::Zero; break;
                case SDLK_F3:        key = Key::F3; break;
                case SDLK_F5:        key = Key::F5; break;
                case SDLK_F9:        key = Key::F9; break;
                case SDLK_F10:       key = Key::F10; break;
                case SDLK_F11:       key = Key::F11; break;
                default: break;
            }
            if (key != Key::None) {
                ui.OnKeyDown(key, mods);
            }
            break;
        }

        default:
            break;
    }
}

void SdlPlatformWindow::BeginFrameTarget(ICanvas& canvas, Context& ui) {
    m_gpuShaderEngine.TickFrame(canvas);
    if (!canvas.UsesCpuFramebuffer() && m_renderer && ui.HasDirtyDamage()) {
        SDL_RenderClear(m_renderer);
    }
}

void SdlPlatformWindow::PresentCpuFramebuffer(
    const keel::u32* pixels,
    keel::u32 width,
    keel::u32 height,
    keel::u32 stride,
    SurfacePixelFormat format,
    keel::ArrayRef<Rect> dirtyRects,
    bool fullRepaint,
    ICanvas& canvas
) noexcept {
    if (!m_renderer || !pixels || width == 0u || height == 0u) {
        return;
    }

    if (!m_screenTexture || m_texWidth != width || m_texHeight != height || m_texFormat != format) {
        if (m_screenTexture) {
            SDL_DestroyTexture(m_screenTexture);
        }
        m_screenTexture = SDL_CreateTexture(
            m_renderer,
            ResolveSdlPixelFormat(format),
            SDL_TEXTUREACCESS_STREAMING,
            keel::as<keel::i32>(width),
            keel::as<keel::i32>(height)
        );
        if (!m_screenTexture) {
            return;
        }
        SDL_SetTextureScaleMode(m_screenTexture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(m_screenTexture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        m_texWidth = width;
        m_texHeight = height;
        m_texFormat = format;
        fullRepaint = true;
    }

    const keel::i32 pitch = keel::as<keel::i32>(stride * sizeof(keel::u32));
    if (fullRepaint || dirtyRects.empty()) {
        SDL_UpdateTexture(m_screenTexture, nullptr, pixels, pitch);
    } else {
        const keel::f32 pxPerDp = m_boundUi ? m_boundUi->GetPixelsPerDp() : m_dpiScale;
        const keel::i32 iw = keel::as<keel::i32>(width);
        const keel::i32 ih = keel::as<keel::i32>(height);
        for (const Rect& dr : dirtyRects) {
            const keel::i32 x1 = keel::clamp(keel::floor_i32(dr.x * pxPerDp), 0, iw);
            const keel::i32 y1 = keel::clamp(keel::floor_i32(dr.y * pxPerDp), 0, ih);
            const keel::i32 x2 = keel::clamp(keel::ceil_i32(dr.Right() * pxPerDp), 0, iw);
            const keel::i32 y2 = keel::clamp(keel::ceil_i32(dr.Bottom() * pxPerDp), 0, ih);
            if (x2 > x1 && y2 > y1) {
                const SDL_Rect sdlR = {x1, y1, x2 - x1, y2 - y1};
                const keel::u32* src = pixels + keel::as<keel::usize>(sdlR.y) * stride + sdlR.x;
                SDL_UpdateTexture(m_screenTexture, &sdlR, src, pitch);
            }
        }
    }

    SDL_RenderClear(m_renderer);
    if (m_gpuShadersEnabled && canvas.HasActiveGpuPasses()) {
        m_gpuShaderEngine.RenderUnderlays(canvas, width, height);
    }
    SDL_RenderTexture(m_renderer, m_screenTexture, nullptr, nullptr);
    SDL_RenderPresent(m_renderer);
}

void SdlPlatformWindow::PresentGpuFrame(ICanvas& canvas) noexcept {
    if (!m_renderer) {
        return;
    }
    if (m_gpuShadersEnabled && canvas.HasActiveGpuPasses()) {
        m_gpuShaderEngine.RenderUnderlays(canvas, keel::as<keel::u32>(m_width), keel::as<keel::u32>(m_height));
    }
    SDL_RenderPresent(m_renderer);
}

void SdlPlatformWindow::SetUnderlayShadersEnabled(bool enabled) noexcept {
    m_gpuShadersEnabled = enabled && m_gpuShaderEngine.IsAvailable();
    if (!m_gpuShadersEnabled) {
        m_gpuShaderEngine.ReleaseCachedTextures();
    }
}

void SdlPlatformWindow::SetVsyncEnabled(bool enabled) noexcept {
    m_vsyncEnabled = enabled;
    if (m_renderer != nullptr) {
        SDL_SetRenderVSync(m_renderer, m_vsyncEnabled ? 1 : 0);
    }
}

void SdlPlatformWindow::SetCursor(CursorType cursor) noexcept {
    if (cursor == m_activeCursor) {
        return;
    }
    m_activeCursor = cursor;

    const keel::usize idx = keel::min<keel::usize>(keel::as<keel::usize>(cursor), m_cursors.size() - 1);
    if (m_cursors[idx] == nullptr) {
        SDL_SystemCursor sysId = SDL_SYSTEM_CURSOR_DEFAULT;
        switch (cursor) {
            case CursorType::Default:    sysId = SDL_SYSTEM_CURSOR_DEFAULT; break;
            case CursorType::Pointer:    sysId = SDL_SYSTEM_CURSOR_POINTER; break;
            case CursorType::Text:       sysId = SDL_SYSTEM_CURSOR_TEXT; break;
            case CursorType::ResizeEW:   sysId = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case CursorType::ResizeNS:   sysId = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            case CursorType::ResizeNWSE: sysId = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
            case CursorType::ResizeNESW: sysId = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
            case CursorType::Move:       sysId = SDL_SYSTEM_CURSOR_MOVE; break;
            case CursorType::NotAllowed: sysId = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
        }
        m_cursors[idx] = SDL_CreateSystemCursor(sysId);
    }
    if (m_cursors[idx] != nullptr) {
        SDL_SetCursor(m_cursors[idx]);
    }
}

void SdlPlatformWindow::SetTextInputActive(bool active, Rect caretRectPx) noexcept {
    if (!m_window) {
        return;
    }
    if (active != m_textInputActive) {
        m_textInputActive = active;
        if (active) {
            SDL_StartTextInput(m_window);
        } else {
            SDL_StopTextInput(m_window);
            m_lastImeCaretPx = {};
        }
    }
    if (active &&
        (keel::abs(caretRectPx.x - m_lastImeCaretPx.x) > 0.5f ||
         keel::abs(caretRectPx.y - m_lastImeCaretPx.y) > 0.5f ||
         keel::abs(caretRectPx.w - m_lastImeCaretPx.w) > 0.5f ||
         keel::abs(caretRectPx.h - m_lastImeCaretPx.h) > 0.5f)) {
        m_lastImeCaretPx = caretRectPx;
        const keel::f32 pd = (m_windowPixelDensity > 0.01f) ? m_windowPixelDensity : 1.0f;
        const SDL_Rect area{
            keel::round_i32(caretRectPx.x / pd),
            keel::round_i32(caretRectPx.y / pd),
            keel::max(1, keel::round_i32(caretRectPx.w / pd)),
            keel::max(1, keel::round_i32(caretRectPx.h / pd))
        };
        SDL_SetTextInputArea(m_window, &area, 0);
    }
}

void SdlPlatformWindow::OpenUrl(keel::StringRef url) const noexcept {
    const keel::SmallString<256> urlStr(url);
    SDL_OpenURL(urlStr.c_str());
}

WindowHitRegion SdlPlatformWindow::DispatchHitTest(Vec2 logicalPos) const noexcept {
    if (m_callbacks.onHitTest) {
        return m_callbacks.onHitTest(logicalPos);
    }
    if (m_boundUi) {
        return m_boundUi->ResolveWindowHitRegion(logicalPos);
    }
    return WindowHitRegion::Client;
}

void SdlPlatformWindow::SetTitle(keel::StringRef title) noexcept {
    if (!m_window) return;
    const keel::String titleStr = keel::String::from(title);
    SDL_SetWindowTitle(m_window, titleStr.c_str());
}

void SdlPlatformWindow::Minimize() noexcept {
    if (m_window) {
        SDL_MinimizeWindow(m_window);
    }
}

void SdlPlatformWindow::Maximize() noexcept {
    if (m_window) {
        SDL_MaximizeWindow(m_window);
    }
}

void SdlPlatformWindow::Restore() noexcept {
    if (m_window) {
        SDL_RestoreWindow(m_window);
    }
}

void SdlPlatformWindow::RequestClose() noexcept {
    if (!m_callbacks.onCloseRequested || m_callbacks.onCloseRequested()) {
        m_shouldExit = true;
    }
}

bool SdlPlatformWindow::IsMaximized() const noexcept {
    if (!m_window) return false;
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool SdlPlatformWindow::IsFocused() const noexcept {
    if (!m_window) return m_focused;
    return m_focused && ((SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) != 0);
}

bool SdlPlatformWindow::SupportsCustomTitleBar() const noexcept {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return true;
#endif
}

bool SdlPlatformWindow::IsCustomTitleBar() const noexcept {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return m_desc.customTitleBar && !m_desc.fullscreen;
#endif
}

bool SdlPlatformWindow::SetCustomTitleBar(bool enabled) noexcept {
#ifdef __EMSCRIPTEN__
    (void)enabled;
    return false;
#else
    if (!m_window || m_desc.fullscreen) {
        return false;
    }
    if (m_desc.customTitleBar == enabled) {
        return true;
    }
    m_desc.customTitleBar = enabled;
    (void)SDL_SetWindowBordered(m_window, !enabled);
    (void)SDL_SetWindowHitTest(
        m_window,
        enabled ? &SdlCustomTitleBarHitTest : nullptr,
        enabled ? this : nullptr
    );
#if defined(_WIN32)
    ApplyWin32CustomTitleBarShadow(m_window, enabled);
#endif
    keel::i32 actualW = m_width;
    keel::i32 actualH = m_height;
    if (SDL_GetWindowSizeInPixels(m_window, &actualW, &actualH) && actualW > 0 && actualH > 0) {
        if (actualW != m_width || actualH != m_height) {
            m_width = actualW;
            m_height = actualH;
            if (m_callbacks.onResize) {
                m_callbacks.onResize(m_width, m_height);
            }
        }
    }
    return true;
#endif
}

void SdlPlatformWindow::SetWindowCornerStyle(WindowCornerStyle style) noexcept {
#if defined(_WIN32)
    DWORD pref = 0u;
    switch (style) {
        case WindowCornerStyle::Square:     pref = 1u; break; // DWMWCP_DONOTROUND
        case WindowCornerStyle::Round:      pref = 2u; break; // DWMWCP_ROUND
        case WindowCornerStyle::RoundSmall: pref = 3u; break; // DWMWCP_ROUNDSMALL
        case WindowCornerStyle::Auto:
        default:                            pref = 0u; break; // DWMWCP_DEFAULT
    }
    ApplyWin32DwmAttr(m_window, 33u, pref);
#else
    (void)style;
#endif
}

void SdlPlatformWindow::SetWindowBorderColor(keel::Optional<Color> color) noexcept {
#if defined(_WIN32)
    DWORD ref = 0xFFFFFFFFu;
    if (color.has_value() && color->a > 0u) {
        ref = keel::as<DWORD>(color->r) |
              (keel::as<DWORD>(color->g) << 8u) |
              (keel::as<DWORD>(color->b) << 16u);
    }
    ApplyWin32DwmAttr(m_window, 34u, ref);
#else
    (void)color;
#endif
}

bool SdlPlatformWindow::SupportsFileDialogs() const noexcept {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return m_window != nullptr;
#endif
}

void SdlPlatformWindow::FlushCompletedDialogs(Context& ui) {
    if (!m_dialogState || !m_dialogState->mutex) {
        return;
    }
    keel::Array<AsyncDialogState::CompletedEntry> ready;
    SDL_LockMutex(m_dialogState->mutex);
    if (!m_dialogState->completed.empty()) {
        ready = keel::move(m_dialogState->completed);
        m_dialogState->completed.clear();
    }
    SDL_UnlockMutex(m_dialogState->mutex);

    if (!ready.empty()) {
        ui.ResetMouseButtonsAfterModalDialog();
        for (auto& item : ready) {
            if (item.onDone) {
                item.onDone(keel::move(item.result));
            }
        }
        ui.InvalidateAll();
        ui.RequestRepaint(2);
    }
}

namespace {

struct SdlDialogRequest {
    keel::Function<void(FileDialogResult)> onDone{};
    keel::Array<keel::String> filterNames;
    keel::Array<keel::String> filterPatterns;
    keel::Array<SDL_DialogFileFilter> sdlFilters;
    keel::String defaultLoc;
    bool isSync{false};
    SDL_AtomicInt syncDone{0};
    FileDialogResult* syncOut{nullptr};
    void* retainedDialogState{nullptr};
    void (*enqueueAndRelease)(void* state, FileDialogResult res, keel::Function<void(FileDialogResult)> onDone){nullptr};
};

void SDLCALL OnSdlFileDialogFinished(void* userdata, const char* const* filelist, int /*filter*/) { // keel-lint: ignore
    auto* req = static_cast<SdlDialogRequest*>(userdata);
    if (!req) return;

    FileDialogResult res{};
    res.supported = true;
    if (filelist != nullptr) {
        for (keel::usize i = 0u; filelist[i] != nullptr; ++i) {
            if (filelist[i][0] != '\0') {
                res.paths.push_back(keel::String(filelist[i]));
            }
        }
        res.accepted = !res.paths.empty();
    }

    if (req->isSync) {
        if (req->syncOut) {
            *(req->syncOut) = keel::move(res);
        }
        SDL_SetAtomicInt(&req->syncDone, 1);
        return; // Sync caller deletes `req` after observing syncDone == 1
    }

    if (req->retainedDialogState && req->enqueueAndRelease) {
        req->enqueueAndRelease(req->retainedDialogState, keel::move(res), keel::move(req->onDone));
    }
    delete req;
}

void LaunchSdlFileDialog(
    SDL_Window* window,
    const FileDialogOptions& opts,
    SdlDialogRequest* req
) {
    if (!opts.defaultPath.empty()) {
        req->defaultLoc = keel::String::from(opts.defaultPath);
        if (opts.kind == FileDialogKind::SaveFile && !opts.defaultName.empty()) {
            if (req->defaultLoc.back() != '/' && req->defaultLoc.back() != '\\') {
                req->defaultLoc.push_back('/');
            }
            req->defaultLoc.append(opts.defaultName);
        }
    } else if (opts.kind == FileDialogKind::SaveFile && !opts.defaultName.empty()) {
        req->defaultLoc = keel::String::from(opts.defaultName);
    }

    if (opts.kind != FileDialogKind::SelectFolder && !opts.filters.empty()) {
        req->filterNames.reserve(opts.filters.size());
        req->filterPatterns.reserve(opts.filters.size());
        req->sdlFilters.reserve(opts.filters.size());
        for (const FileDialogFilter& f : opts.filters) {
            if (f.pattern.empty()) continue;
            req->filterNames.push_back(keel::String::from(f.name.empty() ? f.pattern : f.name));
            req->filterPatterns.push_back(NormalizeSdlFilterPattern(f.pattern));
            req->sdlFilters.push_back(SDL_DialogFileFilter{
                .name = req->filterNames.back().c_str(),
                .pattern = req->filterPatterns.back().c_str()
            });
        }
    }

    const char* defaultLocPtr = req->defaultLoc.empty() ? nullptr : req->defaultLoc.c_str();
    const SDL_DialogFileFilter* filtersPtr = req->sdlFilters.empty() ? nullptr : req->sdlFilters.data();
    const keel::i32 filterCount = keel::as<keel::i32>(req->sdlFilters.size());

    switch (opts.kind) {
        case FileDialogKind::OpenFile:
            SDL_ShowOpenFileDialog(&OnSdlFileDialogFinished, req, window, filtersPtr, filterCount, defaultLocPtr, false);
            break;
        case FileDialogKind::OpenFiles:
            SDL_ShowOpenFileDialog(&OnSdlFileDialogFinished, req, window, filtersPtr, filterCount, defaultLocPtr, true);
            break;
        case FileDialogKind::SaveFile:
            SDL_ShowSaveFileDialog(&OnSdlFileDialogFinished, req, window, filtersPtr, filterCount, defaultLocPtr);
            break;
        case FileDialogKind::SelectFolder:
            SDL_ShowOpenFolderDialog(&OnSdlFileDialogFinished, req, window, defaultLocPtr, false);
            break;
    }
}

} // namespace

void SdlPlatformWindow::ShowFileDialogAsync(
    const FileDialogOptions& opts,
    keel::Function<void(FileDialogResult)> onDone
) {
#ifdef __EMSCRIPTEN__
    (void)opts;
    if (onDone) {
        onDone(FileDialogResult{.accepted = false, .supported = false, .paths = {}});
    }
#else
    if (!m_window || !m_dialogState) {
        if (onDone) {
            onDone(FileDialogResult{.accepted = false, .supported = false, .paths = {}});
        }
        return;
    }
    auto* req = new SdlDialogRequest{};
    m_dialogState->Retain();
    req->retainedDialogState = m_dialogState;
    req->enqueueAndRelease = [](void* rawState, FileDialogResult res, keel::Function<void(FileDialogResult)> cb) {
        auto* st = static_cast<AsyncDialogState*>(rawState);
        if (!st) return;
        st->EnqueueCompleted(keel::move(res), keel::move(cb));
        AsyncDialogState::Release(st);
    };
    req->onDone = keel::move(onDone);
    req->isSync = false;
    LaunchSdlFileDialog(m_window, opts, req);
#endif
}

FileDialogResult SdlPlatformWindow::ShowFileDialog(const FileDialogOptions& opts) {
#ifdef __EMSCRIPTEN__
    (void)opts;
    return FileDialogResult{.accepted = false, .supported = false, .paths = {}};
#else
    if (!m_window) {
        return FileDialogResult{.accepted = false, .supported = false, .paths = {}};
    }
    FileDialogResult out{};
    out.supported = true;
    auto* req = new SdlDialogRequest{};
    req->isSync = true;
    req->syncOut = &out;
    SDL_SetAtomicInt(&req->syncDone, 0);

    LaunchSdlFileDialog(m_window, opts, req);

    // Wait without dispatching into Context (avoids re-entrancy inside BeginFrame/EndFrame)
    while (SDL_GetAtomicInt(&req->syncDone) == 0) {
        SDL_PumpEvents();
        SDL_Delay(8u);
    }
    delete req;
    if (m_pressedMouseMask != 0u) {
        m_pressedMouseMask = 0u;
        SDL_CaptureMouse(false);
    }
    return out;
#endif
}

MessageBoxChoice SdlPlatformWindow::ShowMessageBox(const MessageBoxOptions& opts) {
    SDL_MessageBoxFlags flags = 0u;
    switch (opts.icon) {
        case MessageBoxIcon::Error:    flags |= SDL_MESSAGEBOX_ERROR; break;
        case MessageBoxIcon::Warning:  flags |= SDL_MESSAGEBOX_WARNING; break;
        case MessageBoxIcon::Info:
        case MessageBoxIcon::Question:
        default:                       flags |= SDL_MESSAGEBOX_INFORMATION; break;
    }

    SDL_MessageBoxButtonData buttons[3]{};
    keel::i32 numButtons = 0;
    auto pushBtn = [&](SDL_MessageBoxButtonFlags bf, MessageBoxChoice choice, const char* text) {
        buttons[numButtons++] = SDL_MessageBoxButtonData{
            .flags = bf,
            .buttonID = keel::as<keel::i32>(choice),
            .text = text
        };
    };

    switch (opts.buttons) {
        case MessageBoxButtons::Ok:
            pushBtn(SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, MessageBoxChoice::Ok, "OK");
            break;
        case MessageBoxButtons::OkCancel:
            pushBtn(SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, MessageBoxChoice::Ok, "OK");
            pushBtn(SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, MessageBoxChoice::Cancel, "Cancel");
            break;
        case MessageBoxButtons::YesNo:
            pushBtn(SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, MessageBoxChoice::Yes, "Yes");
            pushBtn(SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, MessageBoxChoice::No, "No");
            break;
        case MessageBoxButtons::YesNoCancel:
            pushBtn(SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, MessageBoxChoice::Yes, "Yes");
            pushBtn(0u, MessageBoxChoice::No, "No");
            pushBtn(SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, MessageBoxChoice::Cancel, "Cancel");
            break;
    }

    const keel::String titleStr = keel::String::from(opts.title.empty() ? keel::StringRef("Message") : opts.title);
    const keel::String msgStr = keel::String::from(opts.message);
    const SDL_MessageBoxData data{
        .flags = flags,
        .window = m_window,
        .title = titleStr.c_str(),
        .message = msgStr.c_str(),
        .numbuttons = numButtons,
        .buttons = buttons,
        .colorScheme = nullptr
    };

    keel::i32 buttonId = keel::as<keel::i32>(MessageBoxChoice::Cancel);
    if (!SDL_ShowMessageBox(&data, &buttonId)) {
        return MessageBoxChoice::Cancel;
    }
    if (m_pressedMouseMask != 0u) {
        m_pressedMouseMask = 0u;
        SDL_CaptureMouse(false);
    }
    return keel::from_underlying<MessageBoxChoice>(keel::as<keel::u8>(buttonId));
}

keel::Optional<ThemeMode> SdlPlatformWindow::QuerySystemTheme() const noexcept {
    const SDL_SystemTheme sysTheme = SDL_GetSystemTheme();
    if (sysTheme == SDL_SYSTEM_THEME_DARK) return ThemeMode::Dark;
    if (sysTheme == SDL_SYSTEM_THEME_LIGHT) return ThemeMode::Light;
    return QueryOsSystemTheme();
}

namespace {
struct SdlPlatformModuleRegistrar {
    SdlPlatformModuleRegistrar() noexcept { RegisterSdlPlatformWindow(); }
} const g_sdlPlatformModuleRegistrar{};
} // namespace

} // namespace guikit

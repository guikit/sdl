#pragma once

#include "guikit/backend/i_window_backend.hpp"
#include "guikit/backend/sdl_gl_shader_engine.hpp"
#include "guikit/backend/vector_backend.hpp"
#include "keel/core.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Cursor;
union SDL_Event;

namespace guikit {

/// Queries the physical pixel density of the display hosting `window` in pixels per millimeter (px/mm).
/// - Windows (`_WIN32`): Queries per-monitor hardware EDID physical dimensions via `MonitorFromWindow` + GDI `GetDeviceCaps`.
/// - WebAssembly (`__EMSCRIPTEN__`): Uses `emscripten_get_device_pixel_ratio()` relative to standard 96 CSS DPI.
/// - Linux / macOS / Fallback: Uses `SDL_GetWindowDisplayScale(window)` relative to 96 DPI.
[[nodiscard]] keel::f32 QueryDisplayPixelsPerMm(SDL_Window* window = nullptr) noexcept;

/// Converts an SDL window-coordinate mouse position to physical framebuffer pixels, the space
/// `Context::OnMouseMove` expects (it divides by the DPI scale itself to reach logical dp).
/// `pixelDensity` is `SDL_GetWindowPixelDensity()`: 1 on Windows and Emscripten (window coords are
/// already pixels), 2 on macOS Retina (window coords are points). Non-positive values are treated as 1.
[[nodiscard]] inline Vec2 SdlWindowToPhysicalPixels(Vec2 windowPos, keel::f32 pixelDensity) noexcept {
    const keel::f32 pd = (pixelDensity > 0.01f) ? pixelDensity : 1.0f;
    return {windowPos.x * pd, windowPos.y * pd};
}

/// SDL3 + OpenGL ES3 / WebGL2 implementation of `guikit::IWindowBackend` with interchangeable `ICanvas`.
class SdlWindowBackend final : public IWindowBackend {
public:
    explicit SdlWindowBackend(keel::UniquePtr<ICanvas> canvas = nullptr);
    ~SdlWindowBackend() override;

    SdlWindowBackend(const SdlWindowBackend&) = delete;
    SdlWindowBackend& operator=(const SdlWindowBackend&) = delete;

    bool Initialize(const MainWindowConfig& config, Context& ui, WindowHostCallbacks callbacks) override;
    void Shutdown() noexcept override;

    bool PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) override;
    void WakeUp() noexcept override;
    [[nodiscard]] bool UsesMainLoopCallback() const noexcept override;
    void RunMainLoopCallback(keel::Function<bool()> stepFn) override;

    [[nodiscard]] ICanvas& GetCanvas() noexcept override { return *m_canvas; }
    [[nodiscard]] const ICanvas& GetCanvas() const noexcept override { return *m_canvas; }

    void PrepareFrameTarget(Context& ui) override;
    void PresentFrame(const Context& ui, FrameMetrics& metrics, bool forcePresent) override;
    void PaceFrame(keel::f64 elapsedMs, bool presented) noexcept override;

    [[nodiscard]] keel::i32 GetWidth() const noexcept override { return m_width; }
    [[nodiscard]] keel::i32 GetHeight() const noexcept override { return m_height; }
    [[nodiscard]] keel::f32 GetDpiScale() const noexcept override { return m_dpiScale; }
    [[nodiscard]] keel::f32 GetDisplayPixelsPerMm() const noexcept override { return m_displayPxPerMm; }
    [[nodiscard]] bool IsMinimized() const noexcept override { return m_minimized; }
    [[nodiscard]] bool ShouldRenderFrame() const noexcept override { return !m_minimized && !m_skipRenderThisStep; }
    [[nodiscard]] bool IsLiveResizeFrame() const noexcept override { return m_isLiveResizeFrame; }

    [[nodiscard]] bool IsVsyncEnabled() const noexcept override { return m_vsyncEnabled; }
    void SetVsyncEnabled(bool enabled) noexcept override;

    [[nodiscard]] bool IsGpuShadersEnabled() const noexcept override { return m_gpuShadersEnabled; }
    [[nodiscard]] bool IsGpuShadersAvailable() const noexcept override { return m_gpuShaderEngine.IsAvailable(); }
    void SetGpuShadersEnabled(bool enabled) noexcept override;

    void UpdateSystemCursor(CursorType cursor) noexcept override;
    void OpenUrl(keel::StringRef url) const noexcept override;

    [[nodiscard]] SDL_Window* GetSdlWindow() const noexcept { return m_window; }
    [[nodiscard]] SDL_Renderer* GetSdlRenderer() const noexcept { return m_renderer; }
    [[nodiscard]] SdlGlShaderEngine& GetShaderEngine() noexcept { return m_gpuShaderEngine; }
    [[nodiscard]] const SdlGlShaderEngine& GetShaderEngine() const noexcept { return m_gpuShaderEngine; }
    static bool LiveResizeEventWatch(void* userdata, SDL_Event* event);

private:
    void UpdateDisplayMetrics() noexcept;
    [[nodiscard]] Vec2 ToPhysicalMousePos(keel::f32 x, keel::f32 y) const noexcept {
        return SdlWindowToPhysicalPixels({x, y}, m_windowPixelDensity);
    }
    void ProcessEvent(const SDL_Event& event, Context& ui);

    MainWindowConfig m_config;
    WindowHostCallbacks m_callbacks;
    Context* m_boundUi = nullptr;

    bool m_sdlVideoInitialized = false;
    bool m_eventWatchInstalled = false;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_screenTexture = nullptr;

    keel::i32 m_width = 1380;
    keel::i32 m_height = 800;
    keel::i32 m_bufWidth = 0;
    keel::i32 m_bufHeight = 0;
    keel::Array<keel::u32> m_pixelBuffer;

    keel::UniquePtr<ICanvas> m_canvas;
    bool m_hasExplicitCanvas = false;
    bool m_usingFallbackCanvas = false;
    SdlGlShaderEngine m_gpuShaderEngine;

    bool m_shouldExit = false;
    bool m_minimized = false;
    bool m_skipRenderThisStep = false;
    keel::u32 m_wakePending{0};
    bool m_vsyncEnabled = true;
    bool m_gpuShadersEnabled = false;
    bool m_isLiveResizeFrame = false;
    keel::u32 m_pressedMouseMask = 0;
    keel::f32 m_dpiScale = 1.0f;
    keel::f32 m_windowPixelDensity = 1.0f;
    keel::f32 m_displayPxPerMm = 96.0f / 25.4f;

    keel::u64 m_lastLiveResizeTimeNs{0};
    keel::u64 m_lastEmscriptenRenderTimeNs{0};
    keel::Function<bool()> m_emscriptenStepFn;

    keel::StaticArray<SDL_Cursor*, 9> m_cursors{};
    CursorType m_activeCursor = CursorType::Default;
};

/// Creates a new `SdlWindowBackend` instance using the default registered vector canvas.
[[nodiscard]] keel::UniquePtr<IWindowBackend> CreateSdlWindowBackend();

/// Creates a new `SdlWindowBackend` instance with an explicitly injected `ICanvas` backend.
[[nodiscard]] keel::UniquePtr<IWindowBackend> CreateSdlWindowBackend(keel::UniquePtr<ICanvas> canvas);

/// Registers `CreateSdlWindowBackend` as the default `IWindowBackend` factory for `guikit::MainWindow`.
void RegisterSdlWindowBackend() noexcept;

} // namespace guikit

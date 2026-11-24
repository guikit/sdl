#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "guikit/backend/sdl_gl_shader_engine.hpp"
#include "guikit/backend/vector_backend.hpp"
#include "keel/array.h"
#include "keel/core.h"
#include "keel/string.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Cursor;
union SDL_Event;

namespace guikit {

/// Queries the physical pixel density of the display hosting `window` in pixels per millimeter (px/mm).
[[nodiscard]] keel::f32 QueryDisplayPixelsPerMm(SDL_Window* window = nullptr) noexcept;

/// Converts an SDL window-coordinate mouse position to physical framebuffer pixels.
[[nodiscard]] inline Vec2 SdlWindowToPhysicalPixels(Vec2 windowPos, keel::f32 pixelDensity) noexcept {
    const keel::f32 pd = (pixelDensity > 0.01f) ? pixelDensity : 1.0f;
    return {windowPos.x * pd, windowPos.y * pd};
}

/// SDL3 + OpenGL ES3 / WebGL2 implementation of `guikit::IPlatformWindow` with interchangeable `ICanvas`.
class SdlPlatformWindow final : public IPlatformWindow {
public:
    SdlPlatformWindow() noexcept = default;
    ~SdlPlatformWindow() override;

    SdlPlatformWindow(const SdlPlatformWindow&) = delete;
    SdlPlatformWindow& operator=(const SdlPlatformWindow&) = delete;

    bool Create(const PlatformWindowDesc& desc, Context& ui, WindowHostCallbacks callbacks) override;
    void Destroy() noexcept override;

    bool PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) override;
    void WakeUp() noexcept override;
    [[nodiscard]] bool UsesMainLoopCallback() const noexcept override;
    void RunMainLoopCallback(keel::Function<bool()> stepFn) override;

    [[nodiscard]] keel::UniquePtr<ICanvas> CreateDefaultCanvas() override;
    void AttachCanvas(ICanvas& canvas) noexcept override;
    void BeginFrameTarget(ICanvas& canvas, Context& ui) override;
    void PresentCpuFramebuffer(
        const keel::u32* pixels,
        keel::u32 width,
        keel::u32 height,
        keel::u32 stride,
        SurfacePixelFormat format,
        keel::ArrayRef<Rect> dirtyRects,
        bool fullRepaint,
        ICanvas& canvas
    ) noexcept override;
    void PresentGpuFrame(ICanvas& canvas) noexcept override;

    [[nodiscard]] bool SupportsUnderlayShaders() const noexcept override { return m_gpuShaderEngine.IsAvailable(); }
    void SetUnderlayShadersEnabled(bool enabled) noexcept override;

    [[nodiscard]] keel::i32 GetFramebufferWidth() const noexcept override { return m_width; }
    [[nodiscard]] keel::i32 GetFramebufferHeight() const noexcept override { return m_height; }
    [[nodiscard]] keel::f32 GetDpiScale() const noexcept override { return m_dpiScale; }
    [[nodiscard]] keel::f32 GetDisplayPixelsPerMm() const noexcept override { return m_displayPxPerMm; }
    [[nodiscard]] bool IsMinimized() const noexcept override { return m_minimized; }
    [[nodiscard]] bool ShouldRenderFrame() const noexcept override { return !m_minimized && !m_skipRenderThisStep; }
    [[nodiscard]] bool IsLiveResizeFrame() const noexcept override { return m_isLiveResizeFrame; }

    [[nodiscard]] bool IsVsyncEnabled() const noexcept override { return m_vsyncEnabled; }
    void SetVsyncEnabled(bool enabled) noexcept override;

    void SetCursor(CursorType cursor) noexcept override;
    void SetTextInputActive(bool active, Rect caretRectPx) noexcept override;
    void OpenUrl(keel::StringRef url) const noexcept override;
    [[nodiscard]] keel::Optional<ThemeMode> QuerySystemTheme() const noexcept override;
    [[nodiscard]] keel::StringRef GetPlatformName() const noexcept override { return "SDL3"; }

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

    PlatformWindowDesc m_desc{};
    WindowHostCallbacks m_callbacks{};
    Context* m_boundUi = nullptr;
    ICanvas* m_attachedCanvas = nullptr;

    bool m_sdlVideoInitialized = false;
    bool m_eventWatchInstalled = false;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_screenTexture = nullptr;

    keel::i32 m_width = 1380;
    keel::i32 m_height = 800;
    keel::u32 m_texWidth = 0;
    keel::u32 m_texHeight = 0;
    SurfacePixelFormat m_texFormat = SurfacePixelFormat::ABGR8888_Premul;

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

    bool m_textInputActive = false;
    Rect m_lastImeCaretPx{};
    keel::Array<keel::String> m_pendingDropFiles;
    Vec2 m_pendingDropPos{};
};

/// Creates a new `SdlPlatformWindow` instance.
[[nodiscard]] keel::UniquePtr<IPlatformWindow> CreateSdlPlatformWindow();

/// Registers `CreateSdlPlatformWindow` as the default `guikit::IPlatformWindow` provider.
void RegisterSdlPlatformWindow() noexcept;

} // namespace guikit

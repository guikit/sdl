#pragma once

// ==============================================================================
// GuiKit SDL3 + ThorVG Backend Module (Umbrella Header)
// ==============================================================================

#include "guikit/core.hpp"
#if defined(GUIKIT_HAS_BACKEND_THORVG) && GUIKIT_HAS_BACKEND_THORVG
#include "guikit/backend/thor_canvas.hpp"
#endif
#if defined(GUIKIT_HAS_BACKEND_SDL3) && GUIKIT_HAS_BACKEND_SDL3
#include "guikit/backend/sdl_canvas.hpp"
#include "guikit/backend/sdl_gl_shader_engine.hpp"
#include "guikit/backend/sdl_platform_window.hpp"
#endif

namespace guikit {

#if defined(GUIKIT_HAS_BACKEND_THORVG) && GUIKIT_HAS_BACKEND_THORVG
void RegisterThorVectorBackend() noexcept;
#endif
#if defined(GUIKIT_HAS_BACKEND_SDL3) && GUIKIT_HAS_BACKEND_SDL3
void RegisterSdlPlatformWindow() noexcept;
#endif

#if (defined(GUIKIT_HAS_BACKEND_THORVG) && GUIKIT_HAS_BACKEND_THORVG) || \
    (defined(GUIKIT_HAS_BACKEND_SDL3) && GUIKIT_HAS_BACKEND_SDL3)
namespace detail {
struct SdlBackendsAutoRegistrar {
    SdlBackendsAutoRegistrar() noexcept {
#if defined(GUIKIT_HAS_BACKEND_THORVG) && GUIKIT_HAS_BACKEND_THORVG
        RegisterThorVectorBackend();
#endif
#if defined(GUIKIT_HAS_BACKEND_SDL3) && GUIKIT_HAS_BACKEND_SDL3
        RegisterSdlPlatformWindow();
#endif
    }
};
inline const SdlBackendsAutoRegistrar g_sdlBackendsAutoRegistrar{};
} // namespace detail
#endif

} // namespace guikit

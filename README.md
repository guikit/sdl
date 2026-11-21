# GuiKit SDL3 + ThorVG Backend (`guikit::sdl`)

Cross-platform SDL3 windowing, OpenGL ES2/3 / WebGL2 shader underlays, and ThorVG CPU vector rasterizer backend for **GuiKit**.

## CMake Targets

- `guikit::backend_thorvg` — `ThorCanvas` vector software rasterizer + image decoder (PNG/JPG/SVG).
- `guikit::backend_sdl3` — `SdlWindowBackend`, `SdlCanvas`, `SdlGlShaderEngine`.
- `guikit::sdl` — umbrella target aggregating enabled backends with automatic registration via `#include "guikit/sdl.hpp"`.

## Usage

```cpp
#include "guikit/sdl.hpp"

int main() {
    guikit::MainWindow app({.title = "GuiKit SDL", .width = 800, .height = 600});
    return app.Run([](guikit::Context& ui) {
        ui.Heading({.label = "Hello GuiKit"});
    });
}
```

## License

GuiKit SDL adapter code is licensed under **0BSD** (`LICENSE`). Bundled `third_party/SDL` (zlib) and `third_party/thorvg` (MIT) remain under their respective upstream licenses.

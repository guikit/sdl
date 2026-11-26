# GuiKit Optional SDL3 + ThorVG Backend (`guikit::sdl`)

**Optional** cross-platform **SDL3** windowing, OpenGL ES2/3 / WebGL2 shader underlays, and **ThorVG** CPU vector rasterizer backend adapter for **GuiKit**.

> For GuiKit's own zero-dependency native platform & GPU backend (zero SDL3, zero ThorVG), see [`guikit/native`](https://github.com/guikit/native).

## CMake Targets

- `guikit::backend_thorvg` — optional `ThorCanvas` vector software rasterizer + image decoder (PNG/JPG/SVG).
- `guikit::backend_sdl3` — optional `SdlWindowBackend`, `SdlCanvas`, `SdlGlShaderEngine`.
- `guikit::sdl` — umbrella target aggregating enabled optional backends with automatic registration via `#include "guikit/sdl.hpp"`.

## Usage

```cpp
#include "guikit/sdl.hpp"

int main() {
    guikit::MainWindow app({.title = "GuiKit SDL", .width = 800, .height = 600});
    return app.Run([](guikit::Context& ui) {
        guikit::Heading(ui, {.label = "Hello GuiKit"});
    });
}
```

## License

GuiKit SDL adapter code is licensed under **0BSD** (`LICENSE`). Bundled `third_party/SDL` (zlib) and `third_party/thorvg` (MIT) remain under their respective upstream licenses.

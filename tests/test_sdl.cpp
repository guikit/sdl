#include "guikit/sdl.hpp"

#include <cstring>
#include "guikit/core/assets.hpp"
#include "keel/keel.h"
#include "keel/math.h"

#if defined(SDL_h_) || defined(SDL_INIT_VIDEO)
#error "guikit/sdl.hpp must not leak <SDL3/SDL.h>"
#endif
#if defined(THORVG_H) || defined(_THORVG_H_)
#error "guikit/sdl.hpp must not leak thorvg.h"
#endif

static keel::i32 g_failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            keel::eprintln("[FAIL] {} ({}:{})", #cond, __FILE__, __LINE__);          \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        if (keel::abs((a) - (b)) > (eps)) {                                           \
            keel::eprintln("[FAIL] {} ({}) != {} ({}) [{}:{}]",                      \
                           #a, (a), #b, (b), __FILE__, __LINE__);                    \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

#if defined(GUIKIT_HAS_BACKEND_THORVG) && GUIKIT_HAS_BACKEND_THORVG

void TestThorCanvasTextAndAssets() {
    using namespace guikit;
    keel::println("  -> TestThorCanvasTextAndAssets...");

    ThorCanvas canvas;
    CHECK(canvas.GetBackendName() == "ThorVG (Vector Software)");
    CHECK(canvas.UsesCpuFramebuffer());
    CHECK(canvas.GetPixelFormat() == SurfacePixelFormat::ABGR8888_Premul);

    const keel::u32 width = 200;
    const keel::u32 height = 100;
    keel::Array<keel::u32> pixels(width * height, 0u);
    canvas.SetTarget(pixels.data(), width, height, width);

    CHECK(canvas.LoadFontFile("assets/fonts/Roboto-Regular.ttf"));
    const Vec2 iconSize = canvas.GetImageSize("assets/guikit-icon-inter850-1024.png");
    CHECK(iconSize.x > 0.0f && iconSize.y > 0.0f);

    static constexpr keel::StringRef kInlineSvg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='200' height='100'>"
        "<rect x='0' y='0' width='200' height='100' fill='#3b82f6'/></svg>";
    const Vec2 svgNat = canvas.GetImageSize(kInlineSvg);
    CHECK_NEAR(svgNat.x, 200.0f, 0.5f);
    CHECK_NEAR(svgNat.y, 100.0f, 0.5f);

    canvas.Begin(width, height);
    canvas.DrawTextBox("Overview", Rect(10.0f, 10.0f, 120.0f, 36.0f), 14.0f, Color(255, 255, 255), 0.5f, 0.5f);
    canvas.End();

    keel::usize lit = 0;
    for (keel::u32 px : pixels) {
        if (px != 0u) ++lit;
    }
    CHECK(lit > 20u);

    // Leading spaces preserved at alignX=0
    keel::Array<keel::u32> bufNoSpace(200 * 40, 0u);
    keel::Array<keel::u32> bufWithSpace(200 * 40, 0u);
    canvas.SetTarget(bufNoSpace.data(), 200, 40, 200);
    canvas.Begin(200, 40, 1.0f);
    canvas.DrawTextBoxStyled("ABC", Rect(10.0f, 4.0f, 180.0f, 32.0f), 16.0f, Color(255, 255, 255), FontStyle::Regular, 0.0f, 0.5f);
    canvas.End();

    canvas.SetTarget(bufWithSpace.data(), 200, 40, 200);
    canvas.Begin(200, 40, 1.0f);
    canvas.DrawTextBoxStyled("   ABC", Rect(10.0f, 4.0f, 180.0f, 32.0f), 16.0f, Color(255, 255, 255), FontStyle::Regular, 0.0f, 0.5f);
    canvas.End();

    auto findFirstInkX = [](keel::ArrayRef<keel::u32> px) {
        keel::i32 minX = 200;
        for (keel::i32 y = 0; y < 40; ++y) {
            for (keel::i32 x = 0; x < 200; ++x) {
                if (px[keel::as<keel::usize>(y) * 200u + keel::as<keel::usize>(x)] != 0u && x < minX) {
                    minX = x;
                }
            }
        }
        return minX;
    };
    CHECK(findFirstInkX(bufWithSpace) > findFirstInkX(bufNoSpace) + 6);
}

void TestThorCanvasFontSizeAndStyles() {
    using namespace guikit;
    keel::println("  -> TestThorCanvasFontSizeAndStyles...");

    const keel::Optional<AssetView> font = Assets::Find("assets/fonts/Roboto-Regular.ttf");
    CHECK(font.has_value() && !font->Empty());
    if (!font.has_value() || font->Empty()) return;

    const keel::u8* d = font->data;
    auto be16 = [&](keel::usize off) { return keel::as<keel::u16>((d[off] << 8) | d[off + 1]); };
    auto be32 = [&](keel::usize off) {
        return (keel::as<keel::u32>(d[off]) << 24) | (keel::as<keel::u32>(d[off + 1]) << 16) |
               (keel::as<keel::u32>(d[off + 2]) << 8) | keel::as<keel::u32>(d[off + 3]);
    };
    keel::usize headOff = 0, hheaOff = 0;
    const keel::u16 numTables = be16(4);
    for (keel::u16 i = 0; i < numTables; ++i) {
        const keel::usize rec = 12 + keel::as<keel::usize>(i) * 16;
        if (keel::mem_eq(d + rec, "head", 4)) headOff = be32(rec + 8);
        if (keel::mem_eq(d + rec, "hhea", 4)) hheaOff = be32(rec + 8);
    }
    CHECK(headOff != 0 && hheaOff != 0);
    const keel::f32 unitsPerEm = keel::as<keel::f32>(be16(headOff + 18));
    const keel::f32 ascender = keel::as<keel::f32>(keel::as<keel::i16>(be16(hheaOff + 4)));
    const keel::f32 descender = keel::as<keel::f32>(keel::as<keel::i16>(be16(hheaOff + 6)));
    const keel::f32 emBoxRatio = (ascender - descender) / unitsPerEm;

    ThorCanvas canvas(false);
    CHECK(canvas.LoadFontMemory("RobotoTest", font->Bytes(), FontStyle::Regular));
    CHECK(canvas.LoadFontFile("assets/fonts/JetBrainsMono-Regular.ttf", FontStyle::Monospace));
    const Vec2 box = canvas.MeasureText("Hg", 100.0f);
    CHECK_NEAR(box.y, 100.0f * emBoxRatio, 2.0f);

    const Vec2 propNarrow = canvas.MeasureTextStyled("iiii", 16.0f, FontStyle::Regular);
    const Vec2 propWide = canvas.MeasureTextStyled("WWWW", 16.0f, FontStyle::Regular);
    CHECK(propWide.x > propNarrow.x * 1.5f);

    const Vec2 monoNarrow = canvas.MeasureTextStyled("iiii", 16.0f, FontStyle::Monospace);
    const Vec2 monoWide = canvas.MeasureTextStyled("WWWW", 16.0f, FontStyle::Monospace);
    CHECK(monoNarrow.x > 10.0f);
    CHECK_NEAR(monoWide.x, monoNarrow.x, 0.05f);
}

void TestThorCanvasTransformAndSubpixel() {
    using namespace guikit;
    keel::println("  -> TestThorCanvasTransformAndSubpixel...");

    constexpr keel::u32 kW = 260;
    constexpr keel::u32 kH = 180;
    constexpr keel::f32 kFrameScale = 1.5f;
    const Vec2 origin{10.0f, 10.0f};
    constexpr keel::f32 s = 2.0f;
    auto map = [&](Vec2 v) { return Vec2{origin.x + (v.x - origin.x) * s, origin.y + (v.y - origin.y) * s}; };
    auto mapRect = [&](Rect r) {
        const Vec2 o = map({r.x, r.y});
        return Rect(o.x, o.y, r.w * s, r.h * s);
    };

    auto draw = [&](ThorCanvas& c, bool useTransform) {
        auto R = [&](Rect r) { return useTransform ? r : mapRect(r); };
        auto P = [&](Vec2 v) { return useTransform ? v : map(v); };
        auto L = [&](keel::f32 v) { return useTransform ? v : v * s; };
        c.Begin(kW, kH, kFrameScale);
        c.Clear(Color(10, 12, 16));
        if (useTransform) {
            c.PushTransform(Transform2D::ScaleAbout(origin, s));
            CHECK_NEAR(c.GetScale(), kFrameScale * s, 1e-6f);
        }
        c.DrawRect(R(Rect(12.0f, 12.0f, 20.0f, 9.0f)), Color(220, 40, 40));
        c.DrawRoundRect(R(Rect(14.0f, 24.0f, 30.0f, 12.0f)), L(3.0f), Color(40, 90, 220), Color(255, 255, 255), L(1.0f));
        c.DrawLine(P({12.0f, 42.0f}), P({60.0f, 50.0f}), Color(255, 255, 255), L(1.5f));
        c.DrawText("Zone", P({40.0f, 30.0f}), L(11.0f), Color(255, 255, 255));
        if (useTransform) {
            c.PopTransform();
            CHECK_NEAR(c.GetScale(), kFrameScale, 1e-6f);
        }
        c.End();
    };

    keel::Array<keel::u32> a(kW * kH, 0u);
    keel::Array<keel::u32> b(kW * kH, 0u);
    ThorCanvas ca, cb;
    ca.SetTarget(a.data(), kW, kH, kW);
    cb.SetTarget(b.data(), kW, kH, kW);
    draw(ca, true);
    draw(cb, false);

    keel::usize differing = 0;
    keel::i32 maxDelta = 0;
    for (keel::usize i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) continue;
        ++differing;
        for (keel::i32 shift = 0; shift < 32; shift += 8) {
            const keel::i32 ca8 = keel::as<keel::i32>((a[i] >> shift) & 0xFFu);
            const keel::i32 cb8 = keel::as<keel::i32>((b[i] >> shift) & 0xFFu);
            maxDelta = keel::max(maxDelta, keel::abs(ca8 - cb8));
        }
    }
    CHECK(maxDelta <= 2);
    CHECK(differing < a.size() / 500);

    // Grayscale vs SubpixelRGB vs SubpixelBGR
    ThorCanvas subCanvas;
#if defined(GUIKIT_HAS_FREETYPE) && GUIKIT_HAS_FREETYPE
    CHECK(subCanvas.GetTextEngineName() == "FreeType");
    CHECK(subCanvas.SupportsFontHinting());
#elif defined(GUIKIT_HAS_TEXT_ENGINE) && GUIKIT_HAS_TEXT_ENGINE
    CHECK(subCanvas.GetTextEngineName() == "keel::font");
    CHECK(subCanvas.SupportsFontHinting());
#else
    CHECK(subCanvas.GetTextEngineName() == "ThorVG");
    CHECK(!subCanvas.SupportsFontHinting());
#endif
    constexpr keel::u32 W = 180;
    constexpr keel::u32 H = 48;
    keel::Array<keel::u32> fbGray(W * H, 0u), fbRgb(W * H, 0u), fbBgr(W * H, 0u);
    const Color darkBg(0x20, 0x20, 0x20, 0xFF);
    const Color white(255, 255, 255, 255);
    auto renderWord = [&](keel::Array<keel::u32>& fb, FontSmoothing mode) {
        subCanvas.SetTarget(fb.data(), W, H, W);
        subCanvas.SetTextRenderSettings(TextRenderSettings{.smoothing = mode, .gammaCorrectBlending = true});
        subCanvas.Begin(W, H, 1.0f);
        subCanvas.Clear(darkBg);
        subCanvas.DrawText("Hello 123", {8.0f, 10.0f}, 14.0f, white);
        subCanvas.End();
    };
    renderWord(fbGray, FontSmoothing::Grayscale);
    renderWord(fbRgb, FontSmoothing::SubpixelRGB);
    renderWord(fbBgr, FontSmoothing::SubpixelBGR);

    keel::usize grayLit = 0, grayChromatic = 0, rgbChromatic = 0, rgbVsBgrDiff = 0;
    for (keel::usize i = 0; i < W * H; ++i) {
        const keel::u32 gPx = fbGray[i];
        const keel::u32 gr = gPx & 0xFFu, gg = (gPx >> 8) & 0xFFu, gb = (gPx >> 16) & 0xFFu;
        if (gr > 0x20u) ++grayLit;
        if (gr != gg || gg != gb) ++grayChromatic;
        const keel::u32 rPx = fbRgb[i];
        if ((rPx & 0xFFu) != ((rPx >> 8) & 0xFFu) || ((rPx >> 8) & 0xFFu) != ((rPx >> 16) & 0xFFu)) ++rgbChromatic;
        if (rPx != fbBgr[i]) ++rgbVsBgrDiff;
    }
    CHECK(grayLit > 50u);
    CHECK(grayChromatic == 0u);
    CHECK(rgbChromatic > 20u);
    CHECK(rgbVsBgrDiff > 20u);
}

void TestThorCanvasStampLruAndOverlayZOrder() {
    using namespace guikit;
    keel::println("  -> TestThorCanvasStampLruAndOverlayZOrder...");

    // StampLruCache churn
    {
        ThorCanvas canvas;
        keel::Array<keel::u32> fb(400u * 120u, 0u);
        canvas.SetTarget(fb.data(), 400, 120, 400);
        canvas.Begin(400, 120, 1.0f);
        canvas.Clear(Color(16, 20, 28, 255));
        for (keel::i32 i = 0; i < 600; ++i) {
            char buf[64]{};
            const keel::StringRef label = keel::format_buf(buf, "Telemetry Sample #%04d", i);
            canvas.DrawTextBoxStyled(label, Rect(8.0f, 8.0f, 360.0f, 28.0f), 14.0f, Color(235, 240, 248, 255), FontStyle::Regular, 0.0f, 0.5f);
            canvas.DrawRoundRect(Rect(8.0f, 48.0f, 24.0f + keel::as<keel::f32>(i % 140), 24.0f), 6.0f, Color(56, 189, 248, 220));
        }
        canvas.End();
        keel::usize nonBg = 0;
        for (keel::u32 px : fb) {
            if (px != fb[0]) ++nonBg;
        }
        CHECK(nonBg > 100u);
    }

    // OverlayLayer pixel Z-order (Notification draws above Popup even when registered first)
    {
        constexpr keel::u32 W = 400, H = 300;
        keel::Array<keel::u32> pixels(W * H, 0u);
        ThorCanvas canvas;
        canvas.SetTarget(pixels.data(), W, H, W);
        Context ui;
        ui.SetThemeMode(ThemeMode::Light);
        ui.BeginFrame({keel::as<keel::f32>(W), keel::as<keel::f32>(H)});
        ui.RegisterOverlay([](ICanvas& c) {
            c.DrawRect(Rect{100.0f, 100.0f, 120.0f, 40.0f}, Color{20, 200, 60, 255});
        }, OverlayLayer::Notification);
        ui.RegisterOverlay([](ICanvas& c) {
            c.DrawRect(Rect{100.0f, 100.0f, 120.0f, 40.0f}, Color{220, 30, 30, 255});
        }, OverlayLayer::Popup);
        ui.EndFrame(canvas);

        const keel::u32 px = pixels[120u * W + 160u];
        CHECK((px & 0xFFu) < 40u && ((px >> 8) & 0xFFu) > 180u && ((px >> 16) & 0xFFu) < 80u);
    }

    // PushRoundedClip: rectangular fill inside rounded clip must leave outer corner pixels untouched
    {
        constexpr keel::u32 W = 80, H = 80;
        keel::Array<keel::u32> pixels(W * H, 0u);
        ThorCanvas canvas;
        CHECK(canvas.SupportsRoundedClip());
        canvas.SetTarget(pixels.data(), W, H, W);
        canvas.Begin(W, H, 1.0f);
        const Color bg(10, 20, 30, 255);
        const Color fg(240, 60, 40, 255);
        canvas.Clear(bg);
        canvas.PushRoundedClip(Rect(10.0f, 10.0f, 60.0f, 60.0f), 16.0f);
        canvas.DrawRect(Rect(10.0f, 10.0f, 60.0f, 60.0f), fg);
        canvas.PopClip();
        canvas.End();

        const keel::u32 bgRaw = bg.ToUint32();
        const keel::u32 centerPx = pixels[40u * W + 40u];
        CHECK((centerPx & 0xFFu) > 220u && ((centerPx >> 8) & 0xFFu) < 80u);
        CHECK(pixels[10u * W + 10u] == bgRaw);
        CHECK(pixels[10u * W + 69u] == bgRaw);
        CHECK(pixels[69u * W + 10u] == bgRaw);
        CHECK(pixels[69u * W + 69u] == bgRaw);
    }
}

#endif // GUIKIT_HAS_BACKEND_THORVG

#if defined(GUIKIT_HAS_BACKEND_SDL3) && GUIKIT_HAS_BACKEND_SDL3

void TestSdlBackendAndDpiMapping() {
    using namespace guikit;
    keel::println("  -> TestSdlBackendAndDpiMapping...");

    // 1. SdlCanvas stub & ICanvas polymorphism
    SdlCanvas canvas(nullptr);
    CHECK(canvas.GetRenderer() == nullptr);
    CHECK(!canvas.UsesCpuFramebuffer());
    CHECK(canvas.GetBackendName() == "SDL3 (Direct GPU)");
    canvas.Begin(800, 600, 1.25f);
    CHECK_NEAR(canvas.GetScale(), 1.25f, 0.01f);
    const Vec2 dims = canvas.MeasureText("Hello GuiKit", 16.0f);
    CHECK(dims.x > 0.0f && dims.y > 0.0f);
    canvas.End();

    // 2. SdlWindowToPhysicalPixels HiDPI mapping
    struct Case {
        keel::f32 pixelDensity;
        keel::f32 dpiScale;
        Vec2 windowPos;
        Vec2 expectedLogical;
    };
    static constexpr Case kCases[] = {
        {1.0f, 1.0f,  {300.0f, 120.0f}, {300.0f, 120.0f}},
        {1.0f, 1.5f,  {300.0f, 120.0f}, {200.0f, 80.0f}},
        {2.0f, 2.0f,  {300.0f, 120.0f}, {300.0f, 120.0f}},
        {0.0f, 1.5f,  {300.0f, 120.0f}, {200.0f, 80.0f}},
    };
    for (const Case& c : kCases) {
        Context ui;
        ui.BeginFrame({1600.0f, 1000.0f}, c.dpiScale);
        ui.OnMouseMove(SdlWindowToPhysicalPixels(c.windowPos, c.pixelDensity));
        CHECK_NEAR(ui.GetMousePos().x, c.expectedLogical.x, 0.01f);
        CHECK_NEAR(ui.GetMousePos().y, c.expectedLogical.y, 0.01f);
    }

    // 3. SdlPlatformWindow default canvas selection & fallback
    SdlPlatformWindow sdlWin;
    CHECK(sdlWin.GetPlatformName() == "SDL3");

    RegisterDefaultVectorCanvasFactory(nullptr);
    CHECK(!HasDefaultVectorCanvasFactory());
    {
        auto fallbackCanvas = sdlWin.CreateDefaultCanvas();
        CHECK(fallbackCanvas != nullptr);
        CHECK(!fallbackCanvas->UsesCpuFramebuffer());
        CHECK(fallbackCanvas->GetBackendName() == "SDL3 (Direct GPU)");
    }
#if defined(GUIKIT_HAS_BACKEND_THORVG) && GUIKIT_HAS_BACKEND_THORVG
    RegisterThorVectorBackend();
    CHECK(HasDefaultVectorCanvasFactory());
    {
        auto vecCanvas = sdlWin.CreateDefaultCanvas();
        CHECK(vecCanvas != nullptr);
        CHECK(vecCanvas->UsesCpuFramebuffer());
    }
#endif
}

#if defined(GUIKIT_HAS_BACKEND_THORVG) && GUIKIT_HAS_BACKEND_THORVG
void TestThorCanvasDrawListPixelParity() {
    keel::println("  -> TestThorCanvasDrawListPixelParity...");
    using namespace guikit;

    constexpr keel::u32 kW = 480;
    constexpr keel::u32 kH = 320;
    keel::Array<keel::u32> directPixels(kW * kH, 0u);
    keel::Array<keel::u32> drawListPixels(kW * kH, 0u);

    auto renderScene = [&](bool useDrawList, keel::Array<keel::u32>& outPixels, keel::usize& outCmdCount) {
        ThorCanvas canvas(true);
        CHECK(canvas.QueryCpuSurface() != nullptr);
        CHECK(canvas.QueryGpuPassRenderer() != nullptr);
        canvas.SetTarget(outPixels.data(), kW, kH, kW);

        Context ui;
        ui.SetDrawListEnabled(useDrawList);
        ui.SetTheme(UiTheme::Dark());
        ui.SetClearColor(Color(15, 23, 42, 255));
        ui.InvalidateAll();

        bool toggleOn = true;
        keel::f32 sliderVal = 0.65f;
        ui.BeginFrame({keel::as<keel::f32>(kW), keel::as<keel::f32>(kH)}, 1.0f);
        if (auto col = guikit::Column(ui, {.gap = 10.0f, .padding = Padding::All(16.0f)})) {
            if (auto card = guikit::Card(ui, {.gap = 8.0f, .padding = Padding::All(14.0f)})) {
                guikit::Heading(ui, {.label = "DrawList Regression", .level = HeadingLevel::H4});
                if (auto row = guikit::Row(ui, {.gap = 8.0f})) {
                    (void)guikit::Button(ui, {.text = "Primary", .variant = UiVariant::Primary});
                    (void)guikit::Switch(ui, toggleOn);
                    guikit::Tag(ui, "OK", {.variant = UiVariant::Success});
                }
                (void)guikit::Slider(ui, sliderVal, 0.0f, 1.0f);
                guikit::ProgressBar(ui, 0.72f);
            }
        }
        ui.EndFrame(canvas);
        outCmdCount = ui.GetLastDrawList().CommandCount();
    };

    keel::usize directCmds = 0u;
    keel::usize listCmds = 0u;
    renderScene(false, directPixels, directCmds);
    renderScene(true, drawListPixels, listCmds);

    CHECK(directCmds == 0u);
    CHECK(listCmds > 15u);

    keel::usize nonBgCount = 0u;
    const keel::u32 bgPx = drawListPixels[0];
    for (keel::usize i = 0u; i < directPixels.size(); ++i) {
        if (drawListPixels[i] != bgPx) {
            ++nonBgCount;
        }
    }
    CHECK(nonBgCount > 5000u);
    CHECK(keel::mem_cmp(directPixels.data(), drawListPixels.data(), directPixels.size() * sizeof(keel::u32)) == 0);
}
#endif

#endif // GUIKIT_HAS_BACKEND_SDL3

int main() {
    keel::println("[RUN] Testing GuiKit SDL3 + ThorVG module...");
#if defined(GUIKIT_HAS_BACKEND_THORVG) && GUIKIT_HAS_BACKEND_THORVG
    TestThorCanvasTextAndAssets();
    TestThorCanvasFontSizeAndStyles();
    TestThorCanvasTransformAndSubpixel();
    TestThorCanvasStampLruAndOverlayZOrder();
    TestThorCanvasDrawListPixelParity();
#endif
#if defined(GUIKIT_HAS_BACKEND_SDL3) && GUIKIT_HAS_BACKEND_SDL3
    TestSdlBackendAndDpiMapping();
#endif
    if (g_failures == 0) {
        keel::println("[PASS] All GuiKit SDL3 + ThorVG tests passed successfully!");
        return 0;
    }
    keel::eprintln("[FAIL] Total failures: {}", g_failures);
    return 1;
}

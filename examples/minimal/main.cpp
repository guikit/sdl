#include "guikit/sdl.hpp"

int main(int /*argc*/, char* /*argv*/[]) {
    bool autopilot = true;
    keel::f32 thrustPct = 72.5f;
    keel::i32 pulseCount = 0;

    guikit::MainWindow app({
        .title = "GuiKit Minimal Quickstart",
        .width = 720,
        .height = 480
    });

    return app.Run([&](guikit::Context& ui) {
        if (auto root = guikit::Column(ui, {
            .padding = guikit::Padding::All(24.0f),
            .align = guikit::Alignment::Center,
            .justify = guikit::Justify::Center,
            .width = guikit::SizeMode::Stretch,
            .height = guikit::SizeMode::Stretch
        })) {
            if (auto card = guikit::Card(ui, {
                .minWidth = 420.0f,
                .width = guikit::SizeMode::Fit,
                .gap = 12.0f,
                .padding = guikit::Padding::All(20.0f)
            })) {
                guikit::Heading(ui, {
                    .label = "Mission Control",
                    .description = "GuiKit C++20 Immediate-Mode Vector UI",
                    .icon = guikit::Icon::Play(),
                    .separator = guikit::HeadingSeparator::Bottom
                });

                if (auto el = guikit::Element(ui, {
                    .label = "Autopilot Loop",
                    .description = "Closed-loop attitude stabilization"
                })) {
                    guikit::Switch(ui, autopilot);
                }

                if (auto el = guikit::Element(ui, {.label = "Main Thruster Output"})) {
                    guikit::Slider(ui, thrustPct, 0.0f, 100.0f, {.valueFormat = "%.1f %%"});
                }

                if (auto row = guikit::Row(ui, {.gap = 8.0f, .align = guikit::Alignment::Center})) {
                    if (guikit::Button(ui, {
                        .text = "Trigger Telemetry Pulse",
                        .icon = guikit::Icon::Flash(),
                        .color = guikit::StateColor::Primary()
                    })) {
                        ++pulseCount;
                    }
                    guikit::Spacer(ui);
                    guikit::Text(ui, ui.Format("Pulses: {}", pulseCount), {
                        .role = guikit::TextRole::CodeMedium,
                        .variant = guikit::UiVariant::Info
                    });
                }
            }
        }
    });
}

#include "sdk/include/mod.h"
#include "sdk/include/ui.h"
#include "sdk/include/utils.h"

#include <array>
#include <cstdio>
#include <memory>

namespace nyx {
namespace mods {
namespace demo {

namespace {

constexpr const char* kModName = "demo";
constexpr const char* kWindowTitle = "NyxCore Demo";

sdk::Info ModInfo() {
    sdk::Info info;
    info.name = kModName;
    info.feature = nullptr;
    info.enabled_by_default = true;
    return info;
}

} // namespace

class DemoMod final : public sdk::IMod {
public:
    ~DemoMod() override = default;

    void OnInit() override {
        initialized_ = true;
        NYX_LOGI("%s initialized", kModName);
    }

    void OnUpdate() override {
        if (initialized_) {
            ++frame_count_;
        }
    }

    void OnDraw() override {
        if (!show_window_) {
            return;
        }

        sdk::ui::SetSize(360.0f, 220.0f);
        if (!sdk::ui::Begin(kWindowTitle, &show_window_)) {
            return;
        }

        std::array<char, 96> line{};
        std::snprintf(line.data(), line.size(), "Status: %s", initialized_ ? "initialized" : "waiting");
        sdk::ui::Text(line.data());

        std::snprintf(line.data(), line.size(), "Frames: %llu", static_cast<unsigned long long>(frame_count_));
        sdk::ui::Text(line.data());

        sdk::ui::Line();
        sdk::ui::Checkbox("Sample toggle", &sample_enabled_);
        sdk::ui::Slider("Sample value", &sample_value_, 0.0f, 1.0f);

        if (sdk::ui::Button("Reset")) {
            frame_count_ = 0;
            sample_enabled_ = true;
            sample_value_ = 0.5f;
        }

        sdk::ui::End();
    }

private:
    bool initialized_ = false;
    bool show_window_ = true;
    bool sample_enabled_ = true;
    float sample_value_ = 0.5f;
    unsigned long long frame_count_ = 0;
};

std::unique_ptr<sdk::IMod> CreateDemoMod() {
    return std::make_unique<DemoMod>();
}

} // namespace demo
} // namespace mods
} // namespace nyx

extern "C" NYX_EXPORT void ModEntry() {
    nyx::sdk::Register(nyx::mods::demo::ModInfo(), nyx::mods::demo::CreateDemoMod);
}

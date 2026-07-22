#include "src/overlay/ui/font_registry.h"

#include "sdk/include/utils.h"
#include "src/overlay/ui/resources/font.h"
#include "vendor/imgui/imgui.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nyx {
namespace overlay {
namespace ui {

namespace {

// 字体来源
enum class FontSource {
    // 内存字体
    Memory,
    // 文件字体
    File,
};

// 字体注册规格
struct FontSpec {
    // 字体来源
    FontSource source = FontSource::Memory;
    // 内存字体数据
    const void* data = nullptr;
    // 内存字体大小
    std::size_t size = 0;
    // 文件字体路径
    std::string path;
    // 字体像素大小
    float size_pixels = 0.0f;
    // TTC 字体索引
    int font_index = 0;
};

// 保护字体注册表的锁
std::mutex font_mutex;
// 待加载字体规格
std::unordered_map<std::string, FontSpec> font_specs;
// 已加载字体
std::unordered_map<std::string, ImFont*> loaded_fonts;
// 加载失败字体
std::unordered_set<std::string> failed_fonts;
// 默认字体是否已准备
bool default_ready = false;
// 当前线程压入字体深度
thread_local int font_depth = 0;

// 空指针文本转为空字符串
const char* TextOrEmpty(const char* text) {
    return text != nullptr ? text : "";
}

// 当前是否存在 ImGui 上下文
bool HasContext() {
    return ImGui::GetCurrentContext() != nullptr;
}

// 生成字体 ID
std::string KeyFrom(const char* id) {
    return id != nullptr && id[0] != '\0' ? std::string(id) : std::string();
}

// 写入 ImGui 字体配置名称
void NameFont(ImFontConfig& config, const std::string& id) {
    std::snprintf(config.Name, sizeof(config.Name), "%s", id.c_str());
}

// 归一化字体大小
float CleanSize(float size_pixels) {
    return size_pixels > 0.0f ? size_pixels : 20.0f;
}

// 按规格加载字体
ImFont* LoadFont(const std::string& id, const FontSpec& spec, ImFontAtlas* fonts) {
    if (fonts == nullptr) {
        return nullptr;
    }

    ImFontConfig config;
    config.FontNo = std::max(0, spec.font_index);
    config.Flags |= ImFontFlags_NoLoadError;
    NameFont(config, id);

    const ImWchar* ranges = fonts->GetGlyphRangesChineseFull();
    if (spec.source == FontSource::File) {
        return fonts->AddFontFromFileTTF(
            spec.path.c_str(),
            CleanSize(spec.size_pixels),
            &config,
            ranges
        );
    }

    config.FontDataOwnedByAtlas = false;
    return fonts->AddFontFromMemoryTTF(
        const_cast<void*>(spec.data),
        static_cast<int>(spec.size),
        CleanSize(spec.size_pixels),
        &config,
        ranges
    );
}

// 加载内置或系统默认字体
ImFont* LoadDefaultFont(ImFontAtlas* fonts) {
    if (fonts == nullptr) {
        return nullptr;
    }

    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    const ImWchar* ranges = fonts->GetGlyphRangesChineseFull();
    ImFont* main = fonts->AddFontFromMemoryTTF(
        const_cast<unsigned int*>(OPPOSans_H),
        static_cast<int>(OPPOSans_H_size),
        25.0f,
        &config,
        ranges
    );
    if (main != nullptr) {
        fonts->AddFontFromMemoryTTF(icons_binary, static_cast<int>(sizeof(icons_binary)), 20.0f, &config);
        fonts->AddFontFromMemoryTTF(font_bold_binary, static_cast<int>(sizeof(font_bold_binary)), 20.0f, &config);
        fonts->AddFontFromMemoryTTF(font_binary, static_cast<int>(sizeof(font_binary)), 20.0f, &config);
        NYX_LOGI("imgui bundled demo fonts loaded");
        return main;
    }

    ImFontConfig fallback_config;
    fallback_config.Flags |= ImFontFlags_NoLoadError;
    constexpr const char* kSystemFonts[] = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/NotoSansCJK.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/Roboto-Regular.ttf",
    };

    for (const char* path : kSystemFonts) {
        ImFont* font = fonts->AddFontFromFileTTF(path, 25.0f, &fallback_config, ranges);
        if (font != nullptr) {
            NYX_LOGI("imgui font loaded: %s", path);
            return font;
        }
    }

    NYX_LOGW("imgui system font unavailable, using default font");
    return fonts->AddFontDefault();
}

} // namespace

// 注册内存字体
bool RegisterFont(
    const char* id,
    const void* data,
    std::size_t size,
    float size_pixels
) {
    const std::string key = KeyFrom(id);
    if (key.empty() || data == nullptr || size == 0 || size > static_cast<std::size_t>(INT_MAX)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(font_mutex);
    if (loaded_fonts.find(key) != loaded_fonts.end()) {
        return true;
    }

    FontSpec spec;
    spec.source = FontSource::Memory;
    spec.data = data;
    spec.size = size;
    spec.size_pixels = CleanSize(size_pixels);
    font_specs[key] = std::move(spec);
    failed_fonts.erase(key);
    return true;
}

// 注册文件字体
bool RegisterFontFile(
    const char* id,
    const char* path,
    float size_pixels,
    int font_index
) {
    const std::string key = KeyFrom(id);
    if (key.empty() || path == nullptr || path[0] == '\0') {
        return false;
    }

    std::lock_guard<std::mutex> lock(font_mutex);
    if (loaded_fonts.find(key) != loaded_fonts.end()) {
        return true;
    }

    FontSpec spec;
    spec.source = FontSource::File;
    spec.path = path;
    spec.size_pixels = CleanSize(size_pixels);
    spec.font_index = font_index;
    font_specs[key] = std::move(spec);
    failed_fonts.erase(key);
    return true;
}

// 压入已加载字体
bool PushFont(const char* id, float size_pixels) {
    if (!HasContext()) {
        return false;
    }

    ImFont* font = nullptr;
    {
        std::lock_guard<std::mutex> lock(font_mutex);
        const auto it = loaded_fonts.find(TextOrEmpty(id));
        if (it != loaded_fonts.end()) {
            font = it->second;
        }
    }

    if (font == nullptr) {
        return false;
    }

    const float next_size = size_pixels > 0.0f ? size_pixels : font->LegacySize;
    ImGui::PushFont(font, next_size);
    ++font_depth;
    return true;
}

// 弹出当前字体
void PopFont() {
    if (HasContext() && font_depth > 0) {
        --font_depth;
        ImGui::PopFont();
    }
}

// 应用待加载字体到 ImGui 图集
void ApplyFonts(float /* density */) {
    if (!HasContext()) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* fonts = io.Fonts;
    if (fonts == nullptr || fonts->Locked) {
        return;
    }

    if (!default_ready) {
        if (fonts->Fonts.Size == 0) {
            LoadDefaultFont(fonts);
        }
        default_ready = true;
    }

    // 只加载尚未成功或失败过的字体，避免每帧重复尝试
    std::vector<std::pair<std::string, FontSpec>> pending;
    {
        std::lock_guard<std::mutex> lock(font_mutex);
        for (const auto& item : font_specs) {
            if (loaded_fonts.find(item.first) == loaded_fonts.end() &&
                failed_fonts.find(item.first) == failed_fonts.end()) {
                pending.push_back(item);
            }
        }
    }

    for (const auto& item : pending) {
        ImFont* font = LoadFont(item.first, item.second, fonts);
        std::lock_guard<std::mutex> lock(font_mutex);
        if (font != nullptr) {
            loaded_fonts[item.first] = font;
            NYX_LOGI("imgui font registered: %s", item.first.c_str());
        } else {
            failed_fonts.insert(item.first);
            NYX_LOGW("imgui font registration failed: %s", item.first.c_str());
        }
    }
}

// 重置字体上下文缓存
void ResetFontContext() {
    std::lock_guard<std::mutex> lock(font_mutex);
    loaded_fonts.clear();
    failed_fonts.clear();
    default_ready = false;
    font_depth = 0;
}

} // namespace ui
} // namespace overlay
} // namespace nyx

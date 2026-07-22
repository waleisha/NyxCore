#include "sdk/include/ui.h"

#include "src/overlay/ui/font_registry.h"
#include "src/overlay/ui/gif_player.h"
#include "vendor/imgui/imgui.h"

namespace nyx {
namespace sdk {
namespace ui {

namespace {

// 当前是否存在 ImGui 上下文
bool HasContext() {
    return ImGui::GetCurrentContext() != nullptr;
}

// 当前 Begin/End 面板嵌套深度
thread_local int panel_depth = 0;
// 当前文字颜色栈深度
thread_local int text_color_depth = 0;

// 空指针文本转为空字符串
const char* TextOrEmpty(const char* text) {
    return text != nullptr ? text : "";
}

} // namespace

// 开始绘制面板
bool Begin(const char* title, bool* open) {
    if (!HasContext()) {
        return false;
    }

    const bool visible = ImGui::Begin(TextOrEmpty(title), open);
    if (!visible) {
        ImGui::End();
        return false;
    }

    ++panel_depth;
    return visible;
}

// 结束绘制面板
void End() {
    if (HasContext() && panel_depth > 0) {
        --panel_depth;
        ImGui::End();
    }
}

// 绘制文本
void Text(const char* text) {
    if (HasContext()) {
        ImGui::TextUnformatted(TextOrEmpty(text));
    }
}

// 绘制按钮
bool Button(const char* label) {
    return HasContext() && ImGui::Button(TextOrEmpty(label));
}

// 绘制复选框
bool Checkbox(const char* label, bool* value) {
    return HasContext() && value != nullptr && ImGui::Checkbox(TextOrEmpty(label), value);
}

// 绘制浮点滑条
bool Slider(const char* label, float* value, float min, float max) {
    return HasContext() && value != nullptr && ImGui::SliderFloat(TextOrEmpty(label), value, min, max);
}

// 绘制文本输入框
bool InputText(const char* label, char* buffer, std::size_t size) {
    return HasContext() && buffer != nullptr && size > 0 && ImGui::InputText(TextOrEmpty(label), buffer, size);
}

// 绘制 GIF
bool Gif(const char* id, const GifFrames& frames, float width, float height) {
    return nyx::overlay::ui::DrawGif(id, frames, width, height);
}

// 释放 GIF 纹理
void ReleaseGif(const char* id) {
    nyx::overlay::ui::ReleaseGif(id);
}

// 注册内存字体
bool LoadFont(
    const char* id,
    const void* data,
    std::size_t size,
    float size_pixels
) {
    return nyx::overlay::ui::RegisterFont(id, data, size, size_pixels);
}

// 注册文件字体
bool LoadFontFile(
    const char* id,
    const char* path,
    float size_pixels,
    int font_index
) {
    return nyx::overlay::ui::RegisterFontFile(id, path, size_pixels, font_index);
}

// 压入字体
bool PushFont(const char* id, float size_pixels) {
    return nyx::overlay::ui::PushFont(id, size_pixels);
}

// 弹出字体
void PopFont() {
    nyx::overlay::ui::PopFont();
}

// 绘制分割线
void Line() {
    if (HasContext()) {
        ImGui::Separator();
    }
}

// 切到同行绘制
void SameLine() {
    if (HasContext()) {
        ImGui::SameLine();
    }
}

// 设置下一次窗口尺寸
void SetSize(float width, float height) {
    if (HasContext()) {
        ImGui::SetNextWindowPos(ImVec2(48.0f, 48.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    }
}

// 压入文本颜色
void PushColor(float r, float g, float b, float a) {
    if (HasContext()) {
        ++text_color_depth;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(r, g, b, a));
    }
}

// 弹出文本颜色
void PopColor() {
    if (HasContext() && text_color_depth > 0) {
        --text_color_depth;
        ImGui::PopStyleColor();
    }
}

} // namespace ui
} // namespace sdk
} // namespace nyx

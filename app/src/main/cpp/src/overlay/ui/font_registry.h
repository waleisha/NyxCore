#pragma once

#include "sdk/include/ui.h"

#include <cstddef>

namespace nyx {
namespace overlay {
namespace ui {

// 注册内存字体
bool RegisterFont(
    const char* id,
    const void* data,
    std::size_t size,
    float size_pixels
);
// 注册文件字体
bool RegisterFontFile(
    const char* id,
    const char* path,
    float size_pixels,
    int font_index
);
// 压入已加载字体
bool PushFont(const char* id, float size_pixels);
// 弹出当前字体
void PopFont();
// 应用待加载字体到 ImGui 图集
void ApplyFonts(float density);
// 重置字体上下文缓存
void ResetFontContext();

} // namespace ui
} // namespace overlay
} // namespace nyx

#pragma once

#include "sdk/include/utils.h"

#include <cstddef>
#include <cstdint>

namespace nyx {
namespace sdk {
namespace ui {

// GIF 帧数据：RGBA 连续存储，delays_ms 与帧数一一对应
struct GifFrames {
    // 帧宽度
    int width = 0;
    // 帧高度
    int height = 0;
    // 帧数量
    int count = 0;
    // RGBA 像素数据
    const std::uint8_t* rgba = nullptr;
    // 每帧延迟（毫秒）
    const std::uint16_t* delays_ms = nullptr;
};

// 开始一个 UI 窗口
NYX_EXPORT bool Begin(const char* title, bool* open = nullptr);
// 结束当前 UI 窗口
NYX_EXPORT void End();

// 绘制文本
NYX_EXPORT void Text(const char* text);
// 绘制按钮，点击时返回 true
NYX_EXPORT bool Button(const char* label);
// 绘制复选框，状态变化时返回 true
NYX_EXPORT bool Checkbox(const char* label, bool* value);
// 绘制浮点滑块，数值变化时返回 true
NYX_EXPORT bool Slider(const char* label, float* value, float min, float max);
// 绘制文本输入框，内容变化时返回 true
NYX_EXPORT bool InputText(const char* label, char* buffer, std::size_t size);
// 绘制 GIF 动画
NYX_EXPORT bool Gif(const char* id, const GifFrames& frames, float width, float height);
// 释放指定 GIF 缓存，id 为空时释放全部
NYX_EXPORT void ReleaseGif(const char* id = nullptr);

// 从内存加载字体
NYX_EXPORT bool LoadFont(
    const char* id,
    const void* data,
    std::size_t size,
    float size_pixels
);
// 从文件加载字体
NYX_EXPORT bool LoadFontFile(
    const char* id,
    const char* path,
    float size_pixels,
    int font_index = 0
);
// 压入已加载字体
NYX_EXPORT bool PushFont(const char* id, float size_pixels = 0.0f);
// 弹出当前字体
NYX_EXPORT void PopFont();

// 绘制分隔线
NYX_EXPORT void Line();
// 后续控件与上一控件同行
NYX_EXPORT void SameLine();
// 设置下一个窗口大小
NYX_EXPORT void SetSize(float width, float height);
// 压入当前绘制颜色
NYX_EXPORT void PushColor(float r, float g, float b, float a);
// 弹出当前绘制颜色
NYX_EXPORT void PopColor();

} // namespace ui
} // namespace sdk
} // namespace nyx

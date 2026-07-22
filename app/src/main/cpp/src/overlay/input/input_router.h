#pragma once

#include <cstddef>
#include <vector>

namespace nyx {
namespace overlay {
namespace input {

// 触摸捕获矩形
struct CaptureRect {
    // 左边界
    float left = 0.0f;
    // 上边界
    float top = 0.0f;
    // 右边界
    float right = 0.0f;
    // 下边界
    float bottom = 0.0f;
};

// 推入触摸事件
bool PushTouch(float x, float y, int action);
// 设置当前需要捕获输入的矩形
void SetCaptureRects(const CaptureRect* rects, std::size_t count);
// 获取当前捕获矩形
std::vector<CaptureRect> CaptureRects();
// 推入文本输入
void PushText(const char* text);
// 推入退格键
void PushBackspace();
// 将排队事件写入 ImGui
void DrainToImGui();
// 清空输入状态
void Clear();

// 设置是否需要文本输入
void SetWantTextInput(bool want);
// 查询是否需要文本输入
bool WantsTextInput();
// 获取待处理触摸事件数量
std::size_t Pending();

} // namespace input
} // namespace overlay
} // namespace nyx

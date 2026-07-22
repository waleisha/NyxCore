#include "src/overlay/input/input_router.h"

#include <deque>
#include <mutex>
#include <vector>

#include "vendor/imgui/imgui.h"

namespace nyx {
namespace overlay {
namespace input {

namespace {

// Android ACTION_DOWN
constexpr int kActionDown = 0;
// Android ACTION_UP
constexpr int kActionUp = 1;
// Android ACTION_MOVE
constexpr int kActionMove = 2;
// 最多缓存的触摸事件数量
constexpr std::size_t kMaxQueuedEvents = 256;

// 待转发给 ImGui 的触摸事件
struct TouchEvent {
    // 触摸 X 坐标
    float x = 0.0f;
    // 触摸 Y 坐标
    float y = 0.0f;
    // Android 触摸动作
    int action = kActionUp;
};

// 保护输入队列和捕获状态的锁
std::mutex queue_mutex;
// 触摸事件队列
std::deque<TouchEvent> queue;
// 当前 ImGui 窗口捕获矩形
std::vector<CaptureRect> capture_rects;
// 是否请求系统文本输入
bool want_text_input = false;

// 取出并清空触摸事件队列
std::deque<TouchEvent> TakeEvents() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    std::deque<TouchEvent> events;
    events.swap(queue);
    return events;
}

// 判断动作是否为支持的触摸事件
bool IsTouchAction(int action) {
    return action == kActionDown
        || action == kActionUp
        || action == kActionMove;
}

// 将触摸事件写入 ImGui IO
void Apply(ImGuiIO& io, const TouchEvent& event) {
    switch (event.action) {
    case kActionDown:
        io.AddMousePosEvent(event.x, event.y);
        io.AddMouseButtonEvent(0, true);
        break;
    case kActionUp:
        io.AddMouseButtonEvent(0, false);
        io.AddMousePosEvent(-1.0f, -1.0f);
        break;
    case kActionMove:
        io.AddMousePosEvent(event.x, event.y);
        break;
    }
}

} // namespace

// 推入触摸事件
bool PushTouch(float x, float y, int action) {
    if (!IsTouchAction(action)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(queue_mutex);
    if (queue.size() >= kMaxQueuedEvents) {
        return false;
    }

    queue.push_back(TouchEvent{x, y, action});
    return true;
}

// 设置当前需要捕获输入的矩形
void SetCaptureRects(const CaptureRect* rects, std::size_t count) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    capture_rects.clear();
    if (rects == nullptr || count == 0) {
        return;
    }

    capture_rects.assign(rects, rects + count);
}

// 获取当前捕获矩形
std::vector<CaptureRect> CaptureRects() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return capture_rects;
}

// 推入文本输入
void PushText(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    ImGui::GetIO().AddInputCharactersUTF8(text);
}

// 推入退格键
void PushBackspace() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_Backspace, true);
    io.AddKeyEvent(ImGuiKey_Backspace, false);
}

// 将排队事件写入 ImGui
void DrainToImGui() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    auto events = TakeEvents();
    if (events.empty()) {
        return;
    }

    auto& io = ImGui::GetIO();
    for (const auto& event : events) {
        Apply(io, event);
    }
}

// 清空输入状态
void Clear() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    queue.clear();
    capture_rects.clear();
    want_text_input = false;
}

// 设置是否需要文本输入
void SetWantTextInput(bool want) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    want_text_input = want;
}

// 查询是否需要文本输入
bool WantsTextInput() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return want_text_input;
}

// 获取待处理触摸事件数量
std::size_t Pending() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return queue.size();
}

} // namespace input
} // namespace overlay
} // namespace nyx

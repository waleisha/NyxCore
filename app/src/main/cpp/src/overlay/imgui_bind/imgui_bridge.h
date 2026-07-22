#pragma once

struct ANativeWindow;

namespace nyx {
namespace overlay {
namespace imgui_bind {

// 初始化 ImGui 后端
bool Init(ANativeWindow* window);
// 更新显示尺寸和密度
void Resize(int width, int height, float density);
// 渲染一帧 ImGui
void Frame();
// 关闭 ImGui 后端
void Shutdown(const char* reason = nullptr);

// 判断 ImGui 后端是否可渲染
bool IsReady();

} // namespace imgui_bind
} // namespace overlay
} // namespace nyx

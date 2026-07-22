#include "src/overlay/imgui_bind/imgui_bridge.h"

#include "sdk/include/utils.h"
#include "src/core/context.h"
#include "src/core/manager/module_controller.h"
#include "src/overlay/glsurface/glsurface_host.h"
#include "src/overlay/input/input_router.h"
#include "src/overlay/ui/font_registry.h"
#include "vendor/imgui/backends/imgui_impl_android.h"
#include "vendor/imgui/backends/imgui_impl_opengl3.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_internal.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstring>
#include <time.h>
#include <vector>

namespace nyx {
namespace overlay {
namespace imgui_bind {

namespace {

// ImGui 桥接状态
struct BridgeState {
    // ImGui 上下文
    ImGuiContext* context = nullptr;
    // 当前 native window
    ANativeWindow* window = nullptr;
    // 显示宽度
    int width = 0;
    // 显示高度
    int height = 0;
    // 屏幕密度
    float density = 1.0f;
    // 上一帧时间
    double frame_time = 0.0;
    // Android 后端是否就绪
    bool android_ready = false;
    // OpenGL 后端是否就绪
    bool opengl_ready = false;
};

// 全局桥接状态
BridgeState state;

// UI 尺寸缩放
constexpr float kUiStyleScale = 3.0f;
// UI 默认字体大小
constexpr float kUiFontSize = 25.0f;
// UI 默认字体缩放
constexpr float kUiFontScale = 1.2f;

// 归一化屏幕密度
float CleanDensity(float density) {
    return density > 0.0f ? density : 1.0f;
}

// 应用 ImGui 全局样式缩放
void ApplyStyleScale() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(kUiStyleScale);
    style.FontSizeBase = kUiFontSize;
    style.FontScaleMain = kUiFontScale;
    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill = true;
    style.CurveTessellationTol = 1.25f;
    style.CircleTessellationMaxError = 0.30f;
}

// 切换到桥接持有的 ImGui 上下文
void UseContext() {
    if (state.context != nullptr) {
        ImGui::SetCurrentContext(state.context);
    }
}

// 应用显示尺寸到 ImGui IO
void ApplyDisplay() {
    if (state.context == nullptr || state.width <= 0 || state.height <= 0) {
        return;
    }

    UseContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(state.width), static_cast<float>(state.height));
}

// 重置桥接状态
void ResetState() {
    state.context = nullptr;
    state.window = nullptr;
    state.width = 0;
    state.height = 0;
    state.density = 1.0f;
    state.frame_time = 0.0;
    state.android_ready = false;
    state.opengl_ready = false;
}

// 获取单调时钟秒数
double NowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1000000000.0;
}

// 准备 Android 帧输入状态
void BeginAndroidFrame() {
    UseContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(state.width), static_cast<float>(state.height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    const double now = NowSeconds();
    io.DeltaTime = state.frame_time > 0.0
        ? static_cast<float>(now - state.frame_time)
        : 1.0f / 60.0f;
    if (io.DeltaTime <= 0.0f) {
        io.DeltaTime = 1.0f / 60.0f;
    }
    state.frame_time = now;
}

// 清空 ImGui 输入状态
void ClearInputState() {
    if (state.context == nullptr) {
        return;
    }

    UseContext();
    auto& io = ImGui::GetIO();
    io.ClearInputKeys();
    io.ClearInputMouse();
    io.AddMousePosEvent(-1.0f, -1.0f);
}

// 确保字体图集已加载
void EnsureFonts() {
    if (state.context == nullptr) {
        return;
    }

    UseContext();
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->IsBuilt()) {
        nyx::overlay::ui::ApplyFonts(state.density);
    }
}

// 刷新需要拦截触摸的 ImGui 窗口矩形
void RefreshCaptureRects() {
    if (state.context == nullptr) {
        input::SetCaptureRects(nullptr, 0);
        return;
    }

    UseContext();
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    std::vector<input::CaptureRect> rects;
    rects.reserve(static_cast<std::size_t>(context.Windows.Size));

    for (const ImGuiWindow* window : context.Windows) {
        if (window == nullptr ||
            (!window->Active && !window->WasActive) ||
            window->Hidden ||
            window->Size.x <= 0.0f ||
            window->Size.y <= 0.0f ||
            (window->Flags & ImGuiWindowFlags_NoInputs) != 0 ||
            (window->Flags & ImGuiWindowFlags_ChildWindow) != 0 ||
            std::strstr(window->Name, "Tooltip") != nullptr ||
            std::strstr(window->Name, "Debug##Default") != nullptr) {
            continue;
        }

        rects.push_back(input::CaptureRect{
            window->Pos.x,
            window->Pos.y,
            window->Pos.x + window->Size.x,
            window->Pos.y + window->Size.y,
        });
    }

    input::SetCaptureRects(rects.data(), rects.size());
}

// 当前作用域标记为渲染线程
class RenderThreadScope {
public:
    // 进入渲染线程作用域
    RenderThreadScope() {
        core::Context::instance().set_render_thread(true);
    }

    // 离开渲染线程作用域
    ~RenderThreadScope() {
        core::Context::instance().set_render_thread(false);
    }
};

} // namespace

// 初始化 ImGui 后端
bool Init(ANativeWindow* window) {
    if (window == nullptr) {
        NYX_LOGW("imgui init skipped: null window");
        return false;
    }

    if (state.context != nullptr) {
        Shutdown("surface recreate before init");
    }

    IMGUI_CHECKVERSION();
    state.context = ImGui::CreateContext();
    if (state.context == nullptr) {
        ResetState();
        NYX_LOGE("imgui context creation failed");
        return false;
    }

    UseContext();
    state.window = window;

    ImGui::StyleColorsDark();
    ApplyStyleScale();
    ClearInputState();

    state.android_ready = ImGui_ImplAndroid_Init(window);
    if (!state.android_ready) {
        NYX_LOGE("imgui android backend init failed");
        Shutdown();
        return false;
    }

    state.opengl_ready = ImGui_ImplOpenGL3_Init("#version 300 es");
    if (!state.opengl_ready) {
        NYX_LOGE("imgui opengl backend init failed");
        Shutdown();
        return false;
    }

    nyx::overlay::ui::ApplyFonts(state.density);
    ApplyDisplay();
    nyx::overlay::input::Clear();
    NYX_LOGI("imgui bridge initialized");
    return true;
}

// 更新显示尺寸和密度
void Resize(int width, int height, float density) {
    state.width = width;
    state.height = height;
    state.density = CleanDensity(density);
    ApplyDisplay();
}

// 渲染一帧 ImGui
void Frame() {
    if (!IsReady()) {
        return;
    }

    // 每帧轻量校验 surface flag、缓存状态和 EGL Context
    if (!nyx::overlay::glsurface::ValidateSurfaceFast("imgui_bind::Frame")) {
        nyx::overlay::glsurface::MaybeRequestRecreate("imgui_bind::Frame surface fast-check failed");
        return;
    }

    // EGL Context 绑定和变化检测
    if (!nyx::overlay::glsurface::CheckAndUpdateEGLContext()) {
        if (!nyx::overlay::glsurface::IsStaleRenderThread()) {
            nyx::overlay::glsurface::MaybeRequestRecreate("imgui_bind::Frame EGL check failed");
        }
        return;
    }

    nyx::overlay::glsurface::ResetFailureState("imgui_bind::Frame valid");

    RenderThreadScope render_thread;
    UseContext();

    input::DrainToImGui();
    EnsureFonts();
    ImGui_ImplOpenGL3_NewFrame();
    BeginAndroidFrame();
    ImGui::NewFrame();

    sdk::utils::RunTasks();

    auto& controller = core::ModuleController::Instance();
    controller.UpdateAll();
    controller.RenderAll();
    RefreshCaptureRects();
    input::SetWantTextInput(ImGui::GetIO().WantTextInput);

    ImGui::Render();
    glViewport(0, 0, state.width, state.height);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// 关闭 ImGui 后端
void Shutdown(const char* reason) {
    if (state.context == nullptr) {
        ResetState();
        return;
    }

    UseContext();
    ClearInputState();
    input::SetWantTextInput(false);

    if (state.opengl_ready) {
        ImGui_ImplOpenGL3_Shutdown();
    }

    if (state.android_ready) {
        ImGui_ImplAndroid_Shutdown();
    }

    ImGui::DestroyContext(state.context);
    nyx::overlay::ui::ResetFontContext();
    ResetState();
    NYX_LOGI("imgui bridge shutdown: reason=%s", reason ? reason : "unknown");
}

// 判断 ImGui 后端是否可渲染
bool IsReady() {
    return state.context != nullptr
        && state.window != nullptr
        && state.width > 0
        && state.height > 0
        && state.android_ready
        && state.opengl_ready;
}

} // namespace imgui_bind
} // namespace overlay
} // namespace nyx

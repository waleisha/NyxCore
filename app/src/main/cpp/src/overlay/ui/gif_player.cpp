#include "src/overlay/ui/gif_player.h"

#include "vendor/imgui/imgui.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nyx {
namespace overlay {
namespace ui {

namespace {

// 单个 GIF 播放状态
struct GifState {
    // OpenGL 纹理
    GLuint texture = 0;
    // 帧宽度
    int width = 0;
    // 帧高度
    int height = 0;
    // 总帧数
    int count = 0;
    // 当前帧索引
    int frame = 0;
    // 上次推进时的 ImGui 帧号
    int last_imgui_frame = -1;
    // 当前帧累计播放时间
    float elapsed_ms = 0.0f;
    // RGBA 帧数据
    const std::uint8_t* rgba = nullptr;
    // 每帧延迟毫秒
    const std::uint16_t* delays_ms = nullptr;
};

// GIF 状态表
std::unordered_map<std::string, GifState> gifs;

// 当前是否存在 ImGui 上下文
bool HasContext() {
    return ImGui::GetCurrentContext() != nullptr;
}

// 当前是否存在 GL 上下文
bool HasGlContext() {
    return eglGetCurrentContext() != EGL_NO_CONTEXT;
}

// 判断 GIF 帧数据是否有效
bool IsValid(const sdk::ui::GifFrames& frames) {
    return frames.width > 0 &&
        frames.height > 0 &&
        frames.count > 0 &&
        frames.rgba != nullptr;
}

// 单帧 RGBA 数据大小
std::size_t FrameSize(const GifState& gif) {
    return static_cast<std::size_t>(gif.width) *
        static_cast<std::size_t>(gif.height) *
        4;
}

// 获取指定帧像素指针
const std::uint8_t* FramePixels(const GifState& gif, int frame) {
    return gif.rgba + FrameSize(gif) * static_cast<std::size_t>(frame);
}

// 获取当前帧延迟
int DelayMs(const GifState& gif) {
    if (gif.delays_ms == nullptr) {
        return 100;
    }

    const int delay = static_cast<int>(gif.delays_ms[gif.frame]);
    return delay > 0 ? delay : 100;
}

// 判断已缓存状态是否匹配输入帧数据
bool Matches(const GifState& state, const sdk::ui::GifFrames& frames) {
    return state.width == frames.width &&
        state.height == frames.height &&
        state.count == frames.count &&
        state.rgba == frames.rgba &&
        state.delays_ms == frames.delays_ms;
}

// 删除 GIF 纹理
void DeleteTexture(GifState& gif, bool can_delete) {
    if (gif.texture != 0 && can_delete) {
        glDeleteTextures(1, &gif.texture);
    }
    gif.texture = 0;
}

// 重置播放进度
void Reset(GifState& gif) {
    gif.frame = 0;
    gif.last_imgui_frame = -1;
    gif.elapsed_ms = 0.0f;
}

// 接收新的 GIF 帧数据
void Adopt(GifState& gif, const sdk::ui::GifFrames& frames) {
    gif.width = frames.width;
    gif.height = frames.height;
    gif.count = frames.count;
    gif.rgba = frames.rgba;
    gif.delays_ms = frames.delays_ms;
    Reset(gif);
}

// 上传当前帧到已有纹理
void UploadFrame(GifState& gif) {
    GLint last_texture = 0;
    GLint last_unpack_alignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &last_unpack_alignment);

    glBindTexture(GL_TEXTURE_2D, gif.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        gif.width,
        gif.height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        FramePixels(gif, gif.frame)
    );

    glPixelStorei(GL_UNPACK_ALIGNMENT, last_unpack_alignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(last_texture));
}

// 确保 GIF 纹理已创建
bool EnsureTexture(GifState& gif) {
    if (gif.texture != 0) {
        return true;
    }

    GLint last_texture = 0;
    GLint last_unpack_alignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &last_unpack_alignment);

    glGenTextures(1, &gif.texture);
    if (gif.texture == 0) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, last_unpack_alignment);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(last_texture));
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, gif.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        gif.width,
        gif.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        FramePixels(gif, 0)
    );

    glPixelStorei(GL_UNPACK_ALIGNMENT, last_unpack_alignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(last_texture));
    Reset(gif);
    return true;
}

// 按 ImGui DeltaTime 推进 GIF 帧
void Step(GifState& gif) {
    const int imgui_frame = ImGui::GetFrameCount();
    if (gif.last_imgui_frame == imgui_frame) {
        return;
    }
    gif.last_imgui_frame = imgui_frame;

    const float delta_ms = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 1.0f / 30.0f) * 1000.0f;
    gif.elapsed_ms += delta_ms;

    bool changed = false;
    while (gif.elapsed_ms >= static_cast<float>(DelayMs(gif))) {
        gif.elapsed_ms -= static_cast<float>(DelayMs(gif));
        gif.frame = (gif.frame + 1) % gif.count;
        changed = true;
    }

    if (changed) {
        UploadFrame(gif);
    }
}

// 生成 GIF 状态表 key
std::string KeyFrom(const char* id) {
    return id != nullptr && id[0] != '\0' ? std::string(id) : std::string("##nyx-gif");
}

} // namespace

// 绘制 GIF 帧序列
bool DrawGif(const char* id, const sdk::ui::GifFrames& frames, float width, float height) {
    if (!HasContext() || !HasGlContext() || !IsValid(frames)) {
        return false;
    }

    GifState& gif = gifs[KeyFrom(id)];
    if (!Matches(gif, frames)) {
        DeleteTexture(gif, true);
        Adopt(gif, frames);
    }

    if (!EnsureTexture(gif)) {
        return false;
    }

    Step(gif);
    const float draw_width = width > 0.0f ? width : static_cast<float>(gif.width);
    const float draw_height = height > 0.0f ? height : static_cast<float>(gif.height);
    ImGui::Image(
        static_cast<ImTextureID>(static_cast<intptr_t>(gif.texture)),
        ImVec2(draw_width, draw_height)
    );
    return true;
}

// 释放指定 GIF 纹理
void ReleaseGif(const char* id) {
    if (id == nullptr || id[0] == '\0') {
        ResetGifTextures(HasGlContext());
        return;
    }

    auto it = gifs.find(id);
    if (it == gifs.end()) {
        return;
    }

    DeleteTexture(it->second, HasGlContext());
    gifs.erase(it);
}

// 重置全部 GIF 纹理
void ResetGifTextures(bool can_delete) {
    for (auto& item : gifs) {
        DeleteTexture(item.second, can_delete);
    }
    gifs.clear();
}

} // namespace ui
} // namespace overlay
} // namespace nyx

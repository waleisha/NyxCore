#pragma once

#include "sdk/include/ui.h"

namespace nyx {
namespace overlay {
namespace ui {

// 绘制 GIF 帧序列
bool DrawGif(const char* id, const sdk::ui::GifFrames& frames, float width, float height);
// 释放指定 GIF 纹理
void ReleaseGif(const char* id);
// 重置全部 GIF 纹理
void ResetGifTextures(bool can_delete);

} // namespace ui
} // namespace overlay
} // namespace nyx

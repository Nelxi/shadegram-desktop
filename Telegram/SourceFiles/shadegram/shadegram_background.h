/*
This file is part of Shadegram, a patch set for AyuGram Desktop / Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPainter;
class QWidget;

namespace Shadegram {

// Live shader background instead of the chat wallpaper. The shader is a
// Shadertoy-style GLSL file (mainImage, iTime, iResolution, iMouse) rendered
// on the GPU; Telegram then paints messages, text and media on top of it as
// usual, so nothing but the background is ever touched.
//
// Settings: %APPDATA%\shadegram\desktop.json
//   { "enabled": true, "shader": "", "fps": 30, "scale": 1.0 }
// "shader" is a file name in %APPDATA%\shadegram\shaders\ (empty = built-in).
// Both files are re-read when they change.

[[nodiscard]] bool Enabled();

// Paints the current shader frame over `fill` (clipped to `clip`).
// Returns false if the shader is off or unavailable (no OpenGL, compile
// error) - then the caller paints the usual wallpaper.
bool PaintBackground(QPainter &p, QSize fill, QRect clip);

// The widget is repainted on every shader frame while it is visible.
void Track(not_null<QWidget*> widget);

} // namespace Shadegram

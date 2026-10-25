// RTS Engine — render/camera.h
// 相机：世界坐标（像素）↔ 屏幕坐标。渲染层可用浮点（逻辑层仍禁浮点）。
// 设计：相机只做变换，不持有 GL 状态 → 纯数学，可单元测试。
#pragma once

#include <cstdint>
#include <algorithm>

namespace rts {

struct Camera {
	float centerX = 0.0f;  // 视口中心的世界像素 X
	float centerY = 0.0f;  // 视口中心的世界像素 Y
	float zoom = 1.0f;     // 缩放（>1 放大）
	int viewW = 1280;
	int viewH = 720;

	// 世界 → 屏幕
	void WorldToScreen(float wx, float wy, float& sx, float& sy) const {
		sx = (wx - centerX) * zoom + viewW * 0.5f;
		sy = (wy - centerY) * zoom + viewH * 0.5f;
	}

	// 屏幕 → 世界
	void ScreenToWorld(float sx, float sy, float& wx, float& wy) const {
		wx = (sx - viewW * 0.5f) / zoom + centerX;
		wy = (sy - viewH * 0.5f) / zoom + centerY;
	}

	// 平移（屏幕像素增量）
	void PanScreen(float dxPixels, float dyPixels) {
		centerX -= dxPixels / zoom;
		centerY -= dyPixels / zoom;
	}

	// 以屏幕点 (ax, ay) 为锚点缩放
	void ZoomAt(float factor, float ax, float ay) {
		float wx = 0.0f, wy = 0.0f;
		ScreenToWorld(ax, ay, wx, wy);
		zoom = std::clamp(zoom * factor, 0.1f, 8.0f);
		// 保持锚点世界坐标不变
		float sx2 = 0.0f, sy2 = 0.0f;
		WorldToScreen(wx, wy, sx2, sy2);
		centerX += (sx2 - ax) / zoom;
		centerY += (sy2 - ay) / zoom;
	}

	// OpenRA Viewport.ClampToBounds：把相机中心限制在世界矩形 [min,max] 内
	void ClampToBounds(float mapMinX, float mapMinY, float mapMaxX, float mapMaxY) {
		const float halfW = viewW * 0.5f / ((zoom > 0.001f) ? zoom : 0.001f);
		const float halfH = viewH * 0.5f / ((zoom > 0.001f) ? zoom : 0.001f);
		const float loX = mapMinX + halfW;
		const float hiX = mapMaxX - halfW;
		const float loY = mapMinY + halfH;
		const float hiY = mapMaxY - halfH;
		centerX = (loX < hiX) ? std::clamp(centerX, loX, hiX) : (loX + hiX) * 0.5f;
		centerY = (loY < hiY) ? std::clamp(centerY, loY, hiY) : (loY + hiY) * 0.5f;
	}
};

} // namespace rts

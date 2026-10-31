// RTS Engine — app/ui_layout.h
// 屏幕 UI 布局 + 点击命中判定（纯函数，可在 headless 测试里验证）。
//
// 抽出来的原因：侧栏改版时踩过一个真实的 bug——按钮从底部栏移到右侧后，
// 点击检测还留在"顶部栏之外"的分支里，而按钮的 y 落在游戏区范围内，
// 于是每次点按钮都走进了框选分支，按钮完全没反应。
// 这类"坐标落错区域"的问题必须能用测试覆盖，不能只靠手点。
#pragma once

#include <cstdint>
#include "render/ui.h"

namespace rts {
namespace uilayout {

// 与 game_main 保持一致的布局常量
inline constexpr float kTopBarH = 36.0f;
inline constexpr float kBottomH = 90.0f;
inline constexpr float kSidebarW = 210.0f;
inline constexpr float kBtnH = 54.0f;
inline constexpr float kBtnGap = 8.0f;
inline constexpr float kMinimapSize = 150.0f;

struct Layout {
	float screenW = 1280.0f;
	float screenH = 720.0f;

	ui::Button btnBuild;
	ui::Button btnInfantry;
	ui::Button btnTank;
	ui::Button btnTurret;

	float sideX = 0.0f;
	float sideW = 0.0f;
	float sideBottom = 0.0f;

	float mmX = 0.0f;
	float mmY = 0.0f;
	float mmSize = kMinimapSize;

	// 游戏区（可框选/放置的区域）
	float playTop() const { return kTopBarH; }
	float playBottom() const { return screenH - kBottomH; }
	float playRight() const { return sideX - 12.0f; }

	bool InSidebar(float mx, float my) const {
		return mx >= sideX - 12.0f && my >= playTop() && my <= playBottom();
	}
	bool InMinimap(float mx, float my) const {
		return mx >= mmX && mx <= mmX + mmSize && my >= mmY && my <= mmY + mmSize;
	}
	bool InPlayArea(float mx, float my) const {
		return mx < playRight() && my >= playTop() && my <= playBottom();
	}

	// 点击语义：一个点同时可能落在多个区域内，优先级必须明确。
	enum class HitTarget { None, Minimap, BuildButton, InfantryButton, TankButton, TurretButton, PlayArea };

	// 只有侧栏内的按钮才算按钮；侧栏外的同坐标不能触发（防止误点）
	HitTarget Classify(float mx, float my, bool placingBuilding) const {
		if (!placingBuilding && InMinimap(mx, my)) return HitTarget::Minimap;
		if (InSidebar(mx, my)) {
			if (btnBuild.Hit(mx, my)) return HitTarget::BuildButton;
			if (btnTurret.Hit(mx, my)) return HitTarget::TurretButton;
			if (btnTank.Hit(mx, my)) return HitTarget::TankButton;
			if (btnInfantry.Hit(mx, my)) return HitTarget::InfantryButton;
		}
		if (InPlayArea(mx, my)) return HitTarget::PlayArea;
		return HitTarget::None;
	}
};

// 框选矩形的归一化：起点/终点顺序无关，并夹到游戏区内。
// 抽成纯函数是为了能测——之前 dragging 只记了起点、从来没画框，
// 属于"状态有、表现没有"的漏实现，用测试能钉住"有拖拽就必有框"。
struct SelectionRect {
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
	// 太小的框没有意义（点击而非拖拽），调用方据此决定是否绘制
	bool Visible() const { return w > 0.5f && h > 0.5f; }
};

inline SelectionRect ComputeSelectionRect(float ax, float ay, float bx, float by,
	float screenW, float screenH) {
	SelectionRect r;
	float x0 = (ax < bx) ? ax : bx;
	float x1 = (ax < bx) ? bx : ax;
	float y0 = (ay < by) ? ay : by;
	float y1 = (ay < by) ? by : ay;

	// 夹到游戏区（顶部栏之下、底部栏之上、侧栏之左）
	const float left = 0.0f;
	const float right = screenW - kSidebarW;
	const float top = kTopBarH;
	const float bottom = screenH - kBottomH;
	if (x0 < left) x0 = left;
	if (x1 > right) x1 = right;
	if (y0 < top) y0 = top;
	if (y1 > bottom) y1 = bottom;
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;

	r.x = x0;
	r.y = y0;
	r.w = x1 - x0;
	r.h = y1 - y0;
	return r;
}
// 按当前屏幕尺寸算出布局（与 game_main 的布局代码逐项对应）
inline Layout Compute(float screenW, float screenH) {
	Layout L;
	L.screenW = screenW;
	L.screenH = screenH;
	L.sideX = screenW - kSidebarW + 12.0f;
	L.sideW = kSidebarW - 24.0f;
	L.sideBottom = screenH - kBottomH - 12.0f;

	L.btnBuild.rect = ui::Rect{L.sideX, L.sideBottom - kBtnH, L.sideW, kBtnH};
	L.btnInfantry.rect = ui::Rect{L.sideX, L.sideBottom - 2.0f * (kBtnH + kBtnGap), L.sideW, kBtnH};
	L.btnTank.rect = ui::Rect{L.sideX, L.sideBottom - 3.0f * (kBtnH + kBtnGap), L.sideW, kBtnH};
	L.btnTurret.rect = ui::Rect{L.sideX, L.sideBottom - 4.0f * (kBtnH + kBtnGap), L.sideW, kBtnH};

	L.mmX = 10.0f;
	L.mmY = screenH - kBottomH - kMinimapSize - 10.0f;
	L.mmSize = kMinimapSize;
	return L;
}

} // namespace uilayout
} // namespace rts

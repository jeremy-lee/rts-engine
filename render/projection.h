// RTS Engine — render/projection.h
// 正交（rectangular）投影：格子/子格 ↔ 屏幕像素。
//
// 对齐 OpenRA 的 RA 网格语义（不是等距！）：
//   mods/ra/mod.yaml:   MapGrid: TileSize: 24,24 / Type: Rectangular
//   MapGrid.cs:         TileScale = Type == RectangularIsometric ? 1448 : 1024  → RA 用 1024
//   Map.CenterOfCell:   Rectangular ⇒ WPos(1024*x + 512, 1024*y + 512, 0)   （格中心）
//   WorldRenderer.ScreenPosition: (TileSize.W * pos.X / TileScale, TileSize.H * (pos.Y - pos.Z) / TileScale)
//                       ⇒ 屏幕 (24x + 12, 24y + 12)  ⇒ 格中心 = 格坐标 × 24 + 半格
//
// 我们的 TileSize 与 OpenRA 完全一致，取 24（1:1，不放大 tile）：
//   格中心 = (x * 24 + 12, y * 24 + 12 - height * kHeightStep)
// 高度步长取一格（OpenRA: screenY -= TileSize.H * Z / TileScale，Z = 1024*height ⇒ 每级恰好一格）。
//
// 设计文档 L1：世界↔屏幕转换只写这一份，全引擎复用；只依赖 core 数据类型，不依赖图形库。
#pragma once

#include "core/core.h"

namespace rts {

// ---- 投影常量（像素）----
inline constexpr int32_t kTileW = 24;         // tile 画布宽（= OpenRA TileSize.Width）
inline constexpr int32_t kTileH = 24;         // tile 画布高
inline constexpr int32_t kStepX = 12;         // 等距格间距 X（= kTileW/2）
inline constexpr int32_t kStepY = 6;          // 等距格间距 Y（= kTileH/4，菱形 2:1）
inline constexpr int32_t kHeightStep = 12;    // 每级高度抬升像素

// 屏幕坐标（世界像素，可为负；相机在 M3 处理）
struct ScreenPos {
	int32_t x = 0, y = 0;
	constexpr bool operator==(const ScreenPos& o) const { return x == o.x && y == o.y; }
	constexpr bool operator!=(const ScreenPos& o) const { return !(*this == o); }
};

// 向下取整除法（处理负数）
constexpr int32_t FloorDiv(int32_t a, int32_t b) {
	return (a >= 0) ? (a / b) : -(((-a) + b - 1) / b);
}

// 四舍五入除法（处理负数）
constexpr int32_t RoundDiv(int32_t a, int32_t b) {
	return (a >= 0) ? ((a + b / 2) / b) : -((b / 2 - a) / b);
}

// ---- 正投影（等距）：格中心 → 屏幕像素 ----
inline ScreenPos ProjectCell(CellCoord c, uint8_t height = 0) {
	// 格中心 = 子格 (x+0.5, y+0.5) 的位置，与 ProjectSub 保持自洽：
	//   sub (x*1024+512, y*1024+512) -> ((x-y)*kStepX, (x+y+1)*kStepY)
	return ScreenPos{
		(c.x - c.y) * kStepX,
		(c.x + c.y + 1) * kStepY - static_cast<int32_t>(height) * kHeightStep
	};
}

// ---- 正投影（子格精度）：SubCoord → 屏幕像素 ----
// SubCoord 的 0 表示格左上角（sub = cell*1024 + 512 即格中心），
// 因此这里不加半格偏移，与 ProjectCell 在 sub 取格中心时自洽。
inline ScreenPos ProjectSub(SubCoord pos, uint8_t height = 0) {
	return ScreenPos{
		(pos.x - pos.y) * kStepX / kSubPerCell,
		(pos.x + pos.y) * kStepY / kSubPerCell - static_cast<int32_t>(height) * kHeightStep
	};
}

// ---- 正投影（子格精度）→ Q16.16 屏幕像素 ----
inline Fixed ProjectCellXFixed(SubCoord s) {
	return Fixed::FromRaw(static_cast<int32_t>(
		static_cast<int64_t>(s.x - s.y) * kStepX * Fixed::kOne / kSubPerCell));
}
inline Fixed ProjectCellYFixed(SubCoord s, uint8_t height = 0) {
	const int64_t v = static_cast<int64_t>(s.x + s.y) * kStepY * Fixed::kOne / kSubPerCell;
	const int64_t h = static_cast<int64_t>(height) * kHeightStep * Fixed::kOne;
	return Fixed::FromRaw(static_cast<int32_t>(v - h));
}

// ---- 逆投影：屏幕像素 → 格子（高度 0 层）----
// 格 (x,y) 覆盖屏幕 [x*32, x*32+32) × [y*32, y*32+32)。
inline CellCoord ScreenToCell(ScreenPos p) {
	// 等距逆变换：a = x - y, b = x + y + 1（ProjectCell 把格中心放在 (x+y+1)*kStepY）
	const int32_t a = RoundDiv(p.x, kStepX);
	const int32_t b = RoundDiv(p.y, kStepY) - 1;
	return CellCoord((a + b) / 2, (b - a) / 2);
}

// ---- 逆投影（子格精度）----
inline SubCoord ScreenToSubFixed(Fixed sx, Fixed sy) {
	const Fixed a = (sx * Fixed::FromInt(kSubPerCell)) / Fixed::FromInt(2 * kStepX);
	const Fixed b = (sy * Fixed::FromInt(kSubPerCell)) / Fixed::FromInt(2 * kStepY);
	const Fixed x = a + b;
	const Fixed y = b - a;
	return SubCoord(x.Raw() / Fixed::kOne, y.Raw() / Fixed::kOne);
}

} // namespace rts

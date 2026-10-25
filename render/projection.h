// RTS Engine — render/projection.h
// 等距（isometric）投影：格子/子格 ↔ 屏幕。
// 设计文档 L1 规定：世界↔屏幕转换函数只写这一份，全引擎复用。
// 本文件只依赖 core 数据类型，不依赖任何图形库——因此可在测试中精确验证往返。
#pragma once

#include "core/core.h"

namespace rts {

// ---- 投影常量（像素）----
inline constexpr int32_t kTileW = 64;          // 一格横向像素
inline constexpr int32_t kTileH = 32;          // 一格纵向像素（2:1 等距）
inline constexpr int32_t kHeightStep = 8;      // 每高度单位（0..255）抬升像素

// 屏幕坐标（世界像素，可为负；相机在 M3 处理）
struct ScreenPos {
	int32_t x = 0, y = 0;
	constexpr bool operator==(const ScreenPos& o) const { return x == o.x && y == o.y; }
	constexpr bool operator!=(const ScreenPos& o) const { return !(*this == o); }
};

// 四舍五入除法（处理负数）
constexpr int32_t RoundDiv(int32_t a, int32_t b) {
	return (a >= 0) ? ((a + b / 2) / b) : -((b / 2 - a) / b);
}

// ---- 正投影：格子中心 → 屏幕像素 ----
inline ScreenPos ProjectCell(CellCoord c, uint8_t height = 0) {
	return ScreenPos{
		(c.x - c.y) * (kTileW / 2),
		(c.x + c.y) * (kTileH / 2) - static_cast<int32_t>(height) * kHeightStep
	};
}

// ---- 正投影（子格精度）：SubCoord → 屏幕像素（平滑移动必须用它，ToCell 取整会导致跳格）----
inline ScreenPos ProjectSub(SubCoord pos, uint8_t height = 0) {
	return ScreenPos{
		(pos.x - pos.y) * (kTileW / 2) / kSubPerCell,
		(pos.x + pos.y) * (kTileH / 2) / kSubPerCell - static_cast<int32_t>(height) * kHeightStep
	};
}

// ---- 正投影（子格精度）→ Q16.16 屏幕像素 ----
inline Fixed ProjectCellXFixed(SubCoord s) {
	// sx = (subX - subY) * (kTileW/2) / kSubPerCell
	return Fixed::FromRaw(
		static_cast<int32_t>((static_cast<int64_t>(s.x - s.y) * (kTileW / 2) * Fixed::kOne) / kSubPerCell));
}
inline Fixed ProjectCellYFixed(SubCoord s, uint8_t height = 0) {
	const int64_t v = static_cast<int64_t>(s.x + s.y) * (kTileH / 2) * Fixed::kOne / kSubPerCell;
	const int64_t h = static_cast<int64_t>(height) * kHeightStep * Fixed::kOne;
	return Fixed::FromRaw(static_cast<int32_t>(v - h));
}

// ---- 逆投影：屏幕像素 → 最近格子（高度 0 层）----
// 推导：a = x - y, b = x + y（格子整数）；a = sx/(kTileW/2), b = sy/(kTileH/2)
// 对格子中心点严格精确（a+b 为偶数）。
inline CellCoord ScreenToCell(ScreenPos p) {
	const int32_t a = RoundDiv(p.x, kTileW / 2);
	const int32_t b = RoundDiv(p.y, kTileH / 2);
	return CellCoord((a + b) / 2, (b - a) / 2);
}

// ---- 逆投影（子格精度）----
inline SubCoord ScreenToSubFixed(Fixed sx, Fixed sy) {
	// subX = (sx/kTileW*2 + sy/kTileH*2) * kSubPerCell / 2
	const Fixed a = (sx * Fixed::FromInt(2)) / Fixed::FromInt(kTileW); // (sx)/32
	const Fixed b = (sy * Fixed::FromInt(2)) / Fixed::FromInt(kTileH); // (sy)/16
	const Fixed x = (a + b) * Fixed::FromInt(kSubPerCell) / Fixed::FromInt(2);
	const Fixed y = (b - a) * Fixed::FromInt(kSubPerCell) / Fixed::FromInt(2);
	return SubCoord(x.Raw() / Fixed::kOne, y.Raw() / Fixed::kOne);
}

} // namespace rts

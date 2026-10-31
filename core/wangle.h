// RTS Engine — core/wangle.h
// 移植自 OpenRA 的 WAngle（OpenRA.Game/WAngle.cs）：
//   1D 角度，1024 单位 = 360°（8 朝向 = 128 间隔，32 朝向 = 32 间隔）。
// 确定性：Sin/Cos 用编译期常量表（core/cos_table.inc），不依赖浮点三角函数。
#pragma once

#include <cstdint>

namespace rts {

// 1D angle: 1024 units = 360 degrees（与 OpenRA WAngle 完全一致）
struct WAngle {
	int32_t angle = 0; // 始终归一化到 [0, 1023]

	WAngle() = default;
	explicit constexpr WAngle(int32_t a) : angle(a % 1024) {
		if (angle < 0) angle += 1024;
	}

	static constexpr WAngle FromFacing(int facing) { return WAngle(facing * 4); }
	static constexpr WAngle FromDegrees(int degrees) { return WAngle(degrees * 1024 / 360); }

	int Facing() const { return angle / 4; } // 0-255（32 朝向细分）

	// OpenRA: Sin() = Cos(angle - 256)
	int Sin() const { return WAngle(angle - 256).Cos(); }

	int Cos() const {
		if (angle <= 256) return kCosTable[angle];
		if (angle <= 512) return -kCosTable[512 - angle];
		return -WAngle(angle - 512).Cos();
	}

	// 角度插值（处理 1024 环绕），mul/div 为比例
	static WAngle Lerp(WAngle a, WAngle b, int32_t mul, int32_t div) {
		int32_t aa = a.angle;
		int32_t bb = b.angle;
		if (aa > bb && aa - bb > 512) aa -= 1024;
		if (bb > aa && bb - aa > 512) bb -= 1024;
		const int32_t mid = (aa * (div - mul) + bb * mul) / div;
		return WAngle(mid);
	}

	WAngle operator+(WAngle o) const { return WAngle(angle + o.angle); }
	WAngle operator-(WAngle o) const { return WAngle(angle - o.angle); }
	WAngle operator-() const { return WAngle(-angle); }
	bool operator==(const WAngle& o) const { return angle == o.angle; }
	bool operator!=(const WAngle& o) const { return angle != o.angle; }

	// 对齐 OpenRA 的 Util.TickFacing：每 tick 最多转 turnSpeed，逼近 desiredFacing。
	// 返回新的朝向；差值为 0 时返回 desiredFacing。
	static WAngle TickFacing(WAngle facing, WAngle desiredFacing, int32_t turnSpeed) {
		int32_t d = desiredFacing.angle - facing.angle;
		if (d > 512) d -= 1024;
		if (d < -512) d += 1024;
		if (d >= -turnSpeed && d <= turnSpeed) return desiredFacing;
		return WAngle(facing.angle + (d > 0 ? turnSpeed : -turnSpeed));
	}

	// 到目标朝向的最短角差（有符号，范围 (-512, 512]）
	int32_t ShortestDelta(WAngle other) const {
		int32_t d = other.angle - angle;
		if (d > 512) d -= 1024;
		if (d <= -512) d += 1024;
		return d;
	}

private:
	// C++17：类内 constexpr 静态数组隐式 inline，无需单独定义
	static constexpr int32_t kCosTable[257] = {
#include "cos_table.inc"
	};
};

} // namespace rts

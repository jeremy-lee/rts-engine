// RTS Engine — tests/test_wangle.cpp
// WAngle：OpenRA 角度类型（1024 单位 = 360°）的移植测试。
#include "core/wangle.h"
#include "test_framework.h"

using namespace rts;

TEST(wangle_normalization) {
	CHECK_EQ(WAngle(0).angle, 0);
	CHECK_EQ(WAngle(1024).angle, 0);
	CHECK_EQ(WAngle(1025).angle, 1);
	CHECK_EQ(WAngle(-1).angle, 1023);
	CHECK_EQ(WAngle(-1025).angle, 1023);
	CHECK_EQ(WAngle(5000).angle, 5000 % 1024);
}

TEST(wangle_facing_roundtrip) {
	// OpenRA: FromFacing(f) = f*4, Facing() = angle/4
	for (int f = 0; f < 256; ++f) {
		CHECK_EQ(WAngle::FromFacing(f).Facing(), f);
	}
	// 8 方向 ↔ 32 细分
	CHECK_EQ(WAngle::FromFacing(0).angle, 0);
	CHECK_EQ(WAngle::FromFacing(1).angle, 4);
	CHECK_EQ(WAngle::FromFacing(8).angle, 32);
	CHECK_EQ(WAngle::FromFacing(255).angle, 1020);
}

TEST(wangle_degrees) {
	// 0°→0, 90°→256, 180°→512, 270°→768
	CHECK_EQ(WAngle::FromDegrees(0).angle, 0);
	CHECK_EQ(WAngle::FromDegrees(90).angle, 256);
	CHECK_EQ(WAngle::FromDegrees(180).angle, 512);
	CHECK_EQ(WAngle::FromDegrees(270).angle, 768);
}

TEST(wangle_sin_cos) {
	// Cos(0) = 1024, Cos(256) = 0, Cos(512) = -1024
	CHECK_EQ(WAngle(0).Cos(), 1024);
	CHECK_EQ(WAngle(256).Cos(), 0);
	CHECK_EQ(WAngle(512).Cos(), -1024);
	CHECK_EQ(WAngle(768).Cos(), 0);
	// Sin(0) = 0, Sin(256) = 1024（Sin(a) = Cos(a-256)）
	CHECK_EQ(WAngle(0).Sin(), 0);
	CHECK_EQ(WAngle(256).Sin(), 1024);
	// 单位圆：sin² + cos² ≈ 1024²
	for (int a = 0; a < 1024; a += 37) {
		const int64_t s = WAngle(a).Sin();
		const int64_t c = WAngle(a).Cos();
		const int64_t len = s * s + c * c;
		// 查表精度：误差 < 1%
		CHECK(len > 1048576 * 98 / 100 && len < 1048576 * 102 / 100);
	}
}

TEST(wangle_lerp_wraps) {
	// 从 1020 到 4（跨 0）插值应在 512 附近（短路径）
	const WAngle a(1020);
	const WAngle b(4);
	const WAngle mid = WAngle::Lerp(a, b, 1, 2);
	// 1020→1032（=8），中点 1026 → 2
	CHECK(mid.angle == 2 || mid.angle == 0 || mid.angle == 4);
}

TEST(wangle_deterministic) {
	// 相同输入 → 相同输出（表驱动，无浮点）
	const WAngle x(777);
	CHECK_EQ(x.Cos(), x.Cos());
	CHECK_EQ(x.Sin(), x.Sin());
}

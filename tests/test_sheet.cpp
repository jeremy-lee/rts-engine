// RTS Engine — tests/test_sheet.cpp
// Sheet / SpriteRect：OpenRA Sprite 语义（bounds + inset UV）的移植测试。
#include "render/sheet.h"
#include "test_framework.h"

using namespace rts;

TEST(sheet_valid) {
	Sheet s;
	CHECK(!s.Valid());
	s.texture = 7;
	s.width = 5120;
	s.height = 2176;
	CHECK(s.Valid());
}

TEST(sheet_sprite_uv_inset) {
	Sheet s;
	s.texture = 1;
	s.width = 1000;
	s.height = 500;
	// 位于 (0,0) 的 100x50 sprite
	const SpriteRect r = MakeSpriteRect(s, 0, 0, 100, 50);
	// u0 = (0 + 1/128)/1000, u1 = (100 - 1/128)/1000
	CHECK(r.u0 >= 0.0f && r.u0 < 0.001f);
	CHECK(r.u1 > 0.0999f && r.u1 <= 0.1f);
	CHECK(r.v0 >= 0.0f && r.v0 < 0.001f);
	CHECK(r.v1 > 0.0999f && r.v1 <= 0.1f);
}

TEST(sheet_sprite_center) {
	Sheet s;
	s.texture = 1;
	s.width = 1000;
	s.height = 1000;
	// 位于 (500, 500) 的 100x100 sprite → 中心 UV 0.55
	const SpriteRect r = MakeSpriteRect(s, 500, 500, 100, 100);
	CHECK(r.u0 >= 0.5f && r.u0 <= 0.51f);
	CHECK(r.u1 >= 0.599f && r.u1 <= 0.601f);
}

TEST(sheet_inset_positive) {
	// inset 让 UV 严格位于 [bounds] 内（非 0 非 1 边界）
	Sheet s;
	s.texture = 1;
	s.width = 1280;
	s.height = 720;
	const SpriteRect r = MakeSpriteRect(s, 0, 0, 1280, 720);
	CHECK(r.u0 > 0.0f);
	CHECK(r.v0 > 0.0f);
	CHECK(r.u1 < 1.0f);
	CHECK(r.v1 < 1.0f);
}

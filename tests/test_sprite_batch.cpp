// RTS Engine — tests/test_sprite_batch.cpp
#include "test_framework.h"
#include "render/sprite_batch.h"

using namespace rts;

TEST(sprite_batch_build_quad_vertices) {
	SpriteVertex v[6];
	const int n = BuildQuad(10.0f, 20.0f, 30.0f, 40.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, v);
	CHECK_EQ(n, 6);
	// 三角形 1：左上(10,20), 右上(40,20), 右下(40,60)
	CHECK_EQ(v[0].x, 10.0f); CHECK_EQ(v[0].y, 20.0f);
	CHECK_EQ(v[1].x, 40.0f); CHECK_EQ(v[1].y, 20.0f);
	CHECK_EQ(v[2].x, 40.0f); CHECK_EQ(v[2].y, 60.0f);
	// 三角形 2：左上, 右下, 左下
	CHECK_EQ(v[3].x, 10.0f); CHECK_EQ(v[3].y, 20.0f);
	CHECK_EQ(v[4].x, 40.0f); CHECK_EQ(v[4].y, 60.0f);
	CHECK_EQ(v[5].x, 10.0f); CHECK_EQ(v[5].y, 60.0f);
	// 纹理坐标
	CHECK_EQ(v[0].u, 0.0f); CHECK_EQ(v[0].v, 0.0f);
	CHECK_EQ(v[2].u, 1.0f); CHECK_EQ(v[2].v, 1.0f);
	// 颜色
	CHECK_EQ(v[0].color, 0xFFFFFFFFu);
}

TEST(sprite_batch_collects) {
	SpriteBatch batch;
	batch.Begin();
	CHECK_EQ(batch.SpriteCount(), static_cast<size_t>(0));
	batch.AddSolid(0, 0, 16, 16, 0xFF00FF00);
	batch.AddSolid(100, 100, 8, 8, 0xFFFF0000);
	batch.End();
	CHECK_EQ(batch.SpriteCount(), static_cast<size_t>(2));
	CHECK_EQ(batch.Vertices().size(), static_cast<size_t>(12));
	CHECK_EQ(batch.Vertices()[0].color, 0xFF00FF00u);
}

TEST(sprite_batch_clear) {
	SpriteBatch batch;
	batch.Begin();
	batch.AddSolid(0, 0, 16, 16, 0xFFFFFFFF);
	batch.Clear();
	CHECK_EQ(batch.SpriteCount(), static_cast<size_t>(0));
}

TEST(sprite_batch_1000_sprites) {
	SpriteBatch batch;
	batch.Begin();
	for (int i = 0; i < 1000; ++i) {
		batch.AddSolid(static_cast<float>(i), 0.0f, 16.0f, 16.0f, 0xFF00FF00);
	}
	batch.End();
	CHECK_EQ(batch.SpriteCount(), static_cast<size_t>(1000));
	CHECK_EQ(batch.Vertices().size(), static_cast<size_t>(6000));
}

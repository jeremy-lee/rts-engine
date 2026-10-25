// RTS Engine — render/sprite_batch.h
// 精灵批处理：收集矩形精灵 → 生成顶点数组 → 一次提交。
// 顶点生成是纯 CPU 逻辑（可单元测试），GL 提交在 gl_renderer 中。
#pragma once

#include <cstdint>
#include <vector>

namespace rts {

// 顶点：位置(屏幕) + 纹理坐标 + 颜色(ARGB)
struct SpriteVertex {
	float x = 0.0f;
	float y = 0.0f;
	float u = 0.0f;
	float v = 0.0f;
	uint32_t color = 0xFFFFFFFF;
};

// 纯函数：生成一个轴对齐矩形的 6 个顶点（两个三角形，顺时针）
// 返回写入的顶点数（固定 6）
inline int BuildQuad(float x, float y, float w, float h,
	float u0, float v0, float u1, float v1,
	uint32_t color, SpriteVertex* out /*[6]*/) {
	const float x1 = x + w;
	const float y1 = y + h;
	// 三角形 1：左上, 右上, 右下
	out[0] = SpriteVertex{x, y, u0, v0, color};
	out[1] = SpriteVertex{x1, y, u1, v0, color};
	out[2] = SpriteVertex{x1, y1, u1, v1, color};
	// 三角形 2：左上, 右下, 左下
	out[3] = SpriteVertex{x, y, u0, v0, color};
	out[4] = SpriteVertex{x1, y1, u1, v1, color};
	out[5] = SpriteVertex{x, y1, u0, v1, color};
	return 6;
}

class SpriteBatch {
public:
	void Begin() { m_vertices.clear(); }

	void AddSprite(float x, float y, float w, float h,
		float u0, float v0, float u1, float v1, uint32_t color) {
		SpriteVertex v[6];
		BuildQuad(x, y, w, h, u0, v0, u1, v1, color, v);
		m_vertices.insert(m_vertices.end(), v, v + 6);
	}

	void AddSolid(float x, float y, float w, float h, uint32_t color) {
		AddSprite(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
	}

	void End() {}

	const std::vector<SpriteVertex>& Vertices() const { return m_vertices; }
	size_t SpriteCount() const { return m_vertices.size() / 6; }
	void Clear() { m_vertices.clear(); }

private:
	std::vector<SpriteVertex> m_vertices;
};

} // namespace rts

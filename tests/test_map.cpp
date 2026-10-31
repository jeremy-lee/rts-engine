// RTS Engine — tests/test_map.cpp
#include "test_framework.h"
#include "sim/map.h"
#include <cstdio>
#include <cstring>

using namespace rts;

TEST(map_creation_and_defaults) {
	Map m(16, 8);
	CHECK_EQ(m.Width(), 16);
	CHECK_EQ(m.Height(), 8);
	CHECK_EQ(m.CellCount(), static_cast<size_t>(128));
	CHECK_EQ(static_cast<int>(m.TerrainHeight(CellCoord(0, 0))), 0);
	CHECK(m.Walkable(CellCoord(0, 0)));
	CHECK(m.Buildable(CellCoord(0, 0)));
	CHECK_EQ(static_cast<int>(m.Fog(CellCoord(0, 0))), 0); // 默认未探索
}

TEST(map_set_get_terrain) {
	Map m(8, 8);
	m.SetTerrainHeight(CellCoord(2, 3), 7);
	CHECK_EQ(static_cast<int>(m.TerrainHeight(CellCoord(2, 3))), 7);

	m.SetWalkable(CellCoord(2, 3), false);
	CHECK(!m.Walkable(CellCoord(2, 3)));
	m.SetWalkable(CellCoord(2, 3), true);
	CHECK(m.Walkable(CellCoord(2, 3)));

	m.SetBuildable(CellCoord(1, 1), false);
	CHECK(!m.Buildable(CellCoord(1, 1)));

	m.SetFog(CellCoord(4, 4), 0);
	CHECK_EQ(static_cast<int>(m.Fog(CellCoord(4, 4))), 0);
}

TEST(map_out_of_bounds_safe) {
	Map m(4, 4);
	CHECK(!m.InBounds(CellCoord(-1, 0)));
	CHECK(!m.InBounds(CellCoord(4, 0)));
	CHECK_EQ(static_cast<int>(m.TerrainHeight(CellCoord(-1, -1))), 0);
	CHECK_EQ(static_cast<int>(m.Fog(CellCoord(99, 99))), 0);
}

TEST(map_generate_deterministic) {
	const Map a = Map::GenerateTestMap(32, 32, 12345);
	const Map b = Map::GenerateTestMap(32, 32, 12345);
	CHECK_EQ(a.Hash(), b.Hash());

	const Map c = Map::GenerateTestMap(32, 32, 67890);
	// 不同种子应生成不同地图（概率极高）
	CHECK_NE(a.Hash(), c.Hash());
}

TEST(map_text_roundtrip) {
	Map m = Map::GenerateTestMap(16, 16, 777);
	const std::string path = "test_map_roundtrip.map";
	CHECK(m.SaveToText(path));

	Map loaded;
	CHECK(loaded.LoadFromText(path));
	CHECK_EQ(loaded.Width(), 16);
	CHECK_EQ(loaded.Height(), 16);
	CHECK_EQ(loaded.Hash(), m.Hash());

	// 清理
	std::remove(path.c_str());
}

TEST(map_hash_changes_on_edit) {
	Map m(8, 8);
	const uint64_t h0 = m.Hash();
	m.SetTerrainHeight(CellCoord(0, 0), 1);
	CHECK_NE(h0, m.Hash());
}

TEST(map_generate_has_obstacles) {
	// 生成的地图应当存在障碍（不可通行）——验证生成器真的在生成内容
	const Map m = Map::GenerateTestMap(64, 64, 999);
	int blocked = 0;
	for (int y = 0; y < 64; ++y) {
		for (int x = 0; x < 64; ++x) {
			if (!m.Walkable(CellCoord(x, y))) ++blocked;
		}
	}
	CHECK(blocked > 0);
}

// ---- 二进制地图（与 OpenRA map.bin 同构）往返 ----
TEST(map_binary_roundtrip) {
	const Map src = Map::GenerateTestMap(24, 18, 12345);
	CHECK(src.SaveBinary("test_map_roundtrip.bin"));

	Map dst;
	CHECK(dst.LoadBinary("test_map_roundtrip.bin"));
	CHECK_EQ(dst.Width(), src.Width());
	CHECK_EQ(dst.Height(), src.Height());

	int tileMismatch = 0, heightMismatch = 0, resMismatch = 0;
	for (int32_t y = 0; y < src.Height(); ++y) {
		for (int32_t x = 0; x < src.Width(); ++x) {
			const CellCoord c(x, y);
			if (dst.Tile(c) != src.Tile(c)) ++tileMismatch;
			if (dst.TerrainHeight(c) != src.TerrainHeight(c)) ++heightMismatch;
			// OpenRA 语义：地图文件只存 resourceType，数量是运行时播种的（1 → 2000）
			if (dst.ResourceType(c) != src.ResourceType(c)) ++resMismatch;
		}
	}
	CHECK_EQ(tileMismatch, 0);
	CHECK_EQ(heightMismatch, 0);
	CHECK_EQ(resMismatch, 0);
	std::remove("test_map_roundtrip.bin");
}

// 二进制头必须与 OpenRA 的 BinaryDataHeader 一致：
//   u8 format=2, u16 w, u16 h, u32 tilesOffset, u32 heightsOffset, u32 resourcesOffset
TEST(map_binary_header_layout) {
	Map m(10, 6);
	m.SaveBinary("test_map_header.bin");
	std::FILE* f = std::fopen("test_map_header.bin", "rb");
	CHECK(f != nullptr);
	uint8_t buf[17] = {};
	CHECK_EQ(std::fread(buf, 1, sizeof(buf), f), sizeof(buf));
	std::fclose(f);
	CHECK_EQ(static_cast<int>(buf[0]), 2); // format
	uint16_t w = 0, h = 0;
	std::memcpy(&w, &buf[1], 2);
	std::memcpy(&h, &buf[3], 2);
	CHECK_EQ(static_cast<int>(w), 10);
	CHECK_EQ(static_cast<int>(h), 6);
	uint32_t tilesOffset = 0, heightsOffset = 0, resourcesOffset = 0;
	std::memcpy(&tilesOffset, &buf[5], 4);
	std::memcpy(&heightsOffset, &buf[9], 4);
	std::memcpy(&resourcesOffset, &buf[13], 4);
	CHECK_EQ(tilesOffset, 17u);
	CHECK_EQ(heightsOffset, 17u + 10u * 6u * 3u);
	CHECK_EQ(resourcesOffset, heightsOffset + 10u * 6u);
	std::remove("test_map_header.bin");
}
// 多格模板放置：一次覆盖 N×M，每格 index = y*sizeW + x（对齐 OpenRA TileTemplate.Size）
TEST(map_multi_cell_template_placement) {
	Map m(16, 16);
	// 手工铺一个 2×3 的模板（模拟 sh01.tem 这种多格模板）
	const uint16_t templateId = 3;
	const int sw = 2, sh = 3;
	for (int dy = 0; dy < sh; ++dy) {
		for (int dx = 0; dx < sw; ++dx) {
			m.SetTile(CellCoord(4 + dx, 5 + dy), templateId,
				static_cast<uint8_t>(dy * sw + dx));
		}
	}
	for (int dy = 0; dy < sh; ++dy) {
		for (int dx = 0; dx < sw; ++dx) {
			const TileRef t = m.Tile(CellCoord(4 + dx, 5 + dy));
			CHECK_EQ(static_cast<int>(t.templateId), static_cast<int>(templateId));
			CHECK_EQ(static_cast<int>(t.index), dy * sw + dx);
		}
	}
	// 模板覆盖范围之外的格子必须没被碰到
	CHECK_EQ(static_cast<int>(m.Tile(CellCoord(6, 5)).templateId), 255);
	CHECK_EQ(static_cast<int>(m.Tile(CellCoord(4, 8)).templateId), 255);
}
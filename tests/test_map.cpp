// RTS Engine — tests/test_map.cpp
#include "test_framework.h"
#include "sim/map.h"
#include <cstdio>

using namespace rts;

TEST(map_creation_and_defaults) {
	Map m(16, 8);
	CHECK_EQ(m.Width(), 16);
	CHECK_EQ(m.Height(), 8);
	CHECK_EQ(m.CellCount(), static_cast<size_t>(128));
	CHECK_EQ(static_cast<int>(m.TerrainHeight(CellCoord(0, 0))), 0);
	CHECK(m.Walkable(CellCoord(0, 0)));
	CHECK(m.Buildable(CellCoord(0, 0)));
	CHECK_EQ(static_cast<int>(m.Fog(CellCoord(0, 0))), 255); // 默认未探索
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
	CHECK_EQ(static_cast<int>(m.Fog(CellCoord(99, 99))), 255);
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

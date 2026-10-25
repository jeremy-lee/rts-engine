// RTS Engine — sim/map.h
// 地图：地形层（高度/可通行/可建造）+ 迷雾数据层 + 文本序列化。
// 设计：地图是纯数据（不依赖渲染），坐标一律使用 CellCoord。
#pragma once

#include <cstdint>
#include <string>
#include "core/core.h"

namespace rts {

// 地形标志位
enum TerrainFlags : uint8_t {
	kWalkable = 1 << 0,   // 地面单位可通过
	kBuildable = 1 << 1,  // 可建造
	kWater = 1 << 2,      // 水域（不可通行）
};

class Map {
public:
	Map() = default;
	Map(int32_t width, int32_t height, uint8_t fillHeight = 0,
		uint8_t fillFlags = kWalkable | kBuildable);

	// ---- 尺寸 ----
	int32_t Width() const { return m_width; }
	int32_t Height() const { return m_height; }
	bool InBounds(CellCoord c) const { return m_heightLayer.InBounds(c); }
	size_t CellCount() const { return m_heightLayer.CellCount(); }

	// ---- 地形查询 ----
	uint8_t TerrainHeight(CellCoord c) const { return m_heightLayer.Get(c); }
	uint8_t Flags(CellCoord c) const { return m_flags.Get(c, 0); }
	bool Walkable(CellCoord c) const { return (Flags(c) & kWalkable) != 0; }
	bool Buildable(CellCoord c) const { return (Flags(c) & kBuildable) != 0; }
	bool Water(CellCoord c) const { return (Flags(c) & kWater) != 0; }

	// ---- 地形修改 ----
	void SetTerrainHeight(CellCoord c, uint8_t h) { m_heightLayer.Set(c, h); }
	void SetFlags(CellCoord c, uint8_t f) { m_flags.Set(c, f); }
	void SetWalkable(CellCoord c, bool v) {
		uint8_t f = Flags(c);
		f = v ? (f | kWalkable) : (f & ~kWalkable);
		SetFlags(c, f);
	}
	void SetBuildable(CellCoord c, bool v) {
		uint8_t f = Flags(c);
		f = v ? (f | kBuildable) : (f & ~kBuildable);
		SetFlags(c, f);
	}

	// ---- 建筑占用（0 = 空闲，否则为 EntityId）----
	uint32_t Occupancy(CellCoord c) const { return m_occupancy.Get(c, 0); }
	bool Occupied(CellCoord c) const { return Occupancy(c) != 0; }
	void SetOccupancy(CellCoord c, uint32_t entityId) { m_occupancy.Set(c, entityId); }
	// 区域是否可建造：范围内全部可建造且未被占用
	bool CanBuildAt(CellCoord origin, int32_t w, int32_t h) const {
		for (int32_t y = 0; y < h; ++y) {
			for (int32_t x = 0; x < w; ++x) {
				const CellCoord c(origin.x + x, origin.y + y);
				if (!InBounds(c) || !Buildable(c) || Occupied(c)) return false;
			}
		}
		return true;
	}
	// 占用/释放一个区域（entityId=0 表示释放）
	void SetFootprint(CellCoord origin, int32_t w, int32_t h, uint32_t entityId) {
		for (int32_t y = 0; y < h; ++y) {
			for (int32_t x = 0; x < w; ++x) {
				SetOccupancy(CellCoord(origin.x + x, origin.y + y), entityId);
			}
		}
	}
	// 清除某实体占用的全部格子（O(n) 修复，建筑销毁时调用）
	void ClearFootprint(uint32_t entityId) {
		for (int32_t y = 0; y < m_height; ++y) {
			for (int32_t x = 0; x < m_width; ++x) {
				const CellCoord c(x, y);
				if (Occupancy(c) == entityId) SetOccupancy(c, 0);
			}
		}
	}

	// ---- 资源（矿点数量，0 = 无）----
	int32_t ResourceAmount(CellCoord c) const { return m_resources.Get(c, 0); }
	void SetResourceAmount(CellCoord c, int32_t v) { m_resources.Set(c, v); }
	int32_t TakeResource(CellCoord c, int32_t want) {
		int32_t have = m_resources.Get(c, 0);
		if (have <= 0) return 0;
		const int32_t taken = (want < have) ? want : have;
		m_resources.Set(c, have - taken);
		return taken;
	}

	// ---- 战争迷雾（三态）：0=未探索, 1=已探索, 2=可见 ----
	uint8_t Fog(CellCoord c) const { return m_fog.Get(c, 0); }
	void SetFog(CellCoord c, uint8_t v) { m_fog.Set(c, v); }

	// ---- 序列化：文本格式（可读、可 diff、便于测试）----
	// 格式：
	//   RTSMAP 1
	//   <width> <height>
	//   每格一行：<height> <flags> <fog>
	bool LoadFromText(const std::string& path);
	bool SaveToText(const std::string& path) const;

	// ---- 测试地图生成（确定性，给定 seed 可复现）----
	static Map GenerateTestMap(int32_t width, int32_t height, uint64_t seed);

	// ---- 工具：找最近可通行格（螺旋搜索，确定性）----
	static CellCoord FindNearestWalkable(const Map& map, CellCoord c, int32_t maxRadius = 16) {
		if (map.Walkable(c)) return c;
		for (int32_t r = 1; r <= maxRadius; ++r) {
			for (int32_t dy = -r; dy <= r; ++dy) {
				for (int32_t dx = -r; dx <= r; ++dx) {
					if ((dx > -r && dx < r) && (dy > -r && dy < r)) continue;
					const CellCoord n(c.x + dx, c.y + dy);
					if (map.Walkable(n)) return n;
				}
			}
		}
		return c;
	}

	// ---- 确定性哈希 ----
	uint64_t Hash() const;

private:
	int32_t m_width = 0;
	int32_t m_height = 0;
	CellLayer<uint8_t> m_heightLayer; // 地形高度 0..255
	CellLayer<uint8_t> m_flags;       // TerrainFlags 位集
	CellLayer<uint8_t> m_fog;         // 迷雾 0..255
	CellLayer<int32_t> m_resources;   // 矿点剩余量
	CellLayer<uint32_t> m_occupancy;  // 建筑占用（EntityId）
};

} // namespace rts

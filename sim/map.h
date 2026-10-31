// RTS Engine — sim/map.h
// 地图：地形层（高度/可通行/可建造）+ 迷雾数据层 + 文本序列化。
// 设计：地图是纯数据（不依赖渲染），坐标一律使用 CellCoord。
#pragma once

#include <cstddef>
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

// 地图格子的瓦片引用（对齐 OpenRA 的 TerrainTile：模板 + 变体索引）。
//   templateId = TileSet 的模板（对应一个 TMP 文件，如 255 → clear1.tem 草地）
//   index      = 该模板下的变体号（多格模板时是子格序号，行优先）
// 可玩区域（对应 OpenRA map.yaml 的 Bounds: x,y,w,h）。
// 战役图常把 128×128 的地图缩到 30×36 的可玩区。
struct MapBounds {
	int32_t x = 0;
	int32_t y = 0;
	int32_t w = 0;
	int32_t h = 0;
	bool Contains(CellCoord c) const {
		return c.x >= x && c.y >= y && c.x < x + w && c.y < y + h;
	}
};

struct TileRef {
	uint16_t templateId = 255; // 255 = clear1（默认草地）
	uint8_t index = 0;
	constexpr bool operator==(const TileRef& o) const {
		return templateId == o.templateId && index == o.index;
	}
	constexpr bool operator!=(const TileRef& o) const { return !(*this == o); }
};

class Map {
public:
	Map() = default;
	Map(int32_t width, int32_t height, uint8_t fillHeight = 0,
		uint8_t fillFlags = kWalkable | kBuildable);

	// ---- 元数据（对应 OpenRA 的 map.yaml：Bounds / Tileset）----
	const MapBounds& Bounds() const { return m_bounds; }
	void SetBounds(const MapBounds& b) { m_bounds = b; }
	const std::string& TilesetName() const { return m_tileset; }
	void SetTilesetName(const std::string& name) { m_tileset = name; }

	// ---- 尺寸 ----
	int32_t Width() const { return m_width; }
	int32_t Height() const { return m_height; }
	bool InBounds(CellCoord c) const { return m_heightLayer.InBounds(c); }
	size_t CellCount() const { return m_heightLayer.CellCount(); }

	// ---- 瓦片（OpenRA 同构：每格存 (templateId, index)）----
	TileRef Tile(CellCoord c) const {
		const uint32_t v = m_tiles.Get(c, 0xFF00u);
		return TileRef{static_cast<uint16_t>(v >> 8), static_cast<uint8_t>(v & 0xFFu)};
	}
	void SetTile(CellCoord c, uint16_t templateId, uint8_t index) {
		m_tiles.Set(c, (static_cast<uint32_t>(templateId) << 8) | static_cast<uint32_t>(index));
	}
	void SetTile(CellCoord c, TileRef t) { SetTile(c, t.templateId, t.index); }

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
	// 带 Footprint 的区域检查：mask 为 width*height 的字符数组（'_' 表示该格不占地）。
	// mask 为空时等价于整块矩形。
	bool CanBuildAt(CellCoord origin, int32_t w, int32_t h, const char* mask) const {
		if (mask == nullptr) return CanBuildAt(origin, w, h);
		for (int32_t y = 0; y < h; ++y) {
			for (int32_t x = 0; x < w; ++x) {
				if (mask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] == '_') continue;
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

	// ---- 资源 ----
	// OpenRA 的 map.bin 只存 resourceType（u8），具体数量由运行时 ResourceLayer 播种。
	// 我们两者都存：type 进地图文件（同构），amount 是运行时状态。
	uint8_t ResourceType(CellCoord c) const { return m_resTypes.Get(c, 0); }
	int32_t ResourceAmount(CellCoord c) const { return m_resources.Get(c, 0); }
	// 只设类型，储量留 0：对齐 OpenRA 的 map.bin 语义（文件里只有 resourceType，
	// 具体 density 由 ResourceLayer 在加载后播种，且上限是 ResourceTypeInfo.MaxDensity）。
	void SetResourceType(CellCoord c, uint8_t type) {
		m_resTypes.Set(c, type);
		m_resources.Set(c, type == 0 ? 0 : kDefaultResourceDensity);
	}
	static constexpr int32_t kDefaultResourceDensity = 10; // = OpenRA ResourceTypeInfo.MaxDensity 默认值
	void SetResourceAmount(CellCoord c, int32_t v) {
		m_resources.Set(c, v);
		m_resTypes.Set(c, v > 0 ? 1 : 0);
	}
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

	// ---- 二进制格式（与 OpenRA 的 map.bin 同构）----
	//   u8  format = 2
	//   u16 width, height
	//   u32 tilesOffset      (= 17)
	//   u32 heightsOffset    (= 17 + w*h*3)
	//   u32 resourcesOffset  (= heightsOffset + w*h)
	//   [w*h*3] u16 templateId + u8 index
	//   [w*h*1] u8 height
	//   [w*h*1] u8 resource amount
	bool SaveBinary(const std::string& path) const;
	bool LoadBinary(const std::string& path);

	// ---- 地图元数据（对应 OpenRA map.yaml；二进制 map.bin 里没有这些字段）----
	//   格式（每行 key value）：
	//     RTSMETA 1
	//     tileset TEMPERAT
	//     bounds 0 0 128 128
	bool SaveMeta(const std::string& path) const;
	bool LoadMeta(const std::string& path);

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
	MapBounds m_bounds;               // 可玩区域
	std::string m_tileset = "TEMPERAT";
	CellLayer<uint32_t> m_tiles;      // 打包的 (templateId << 8) | index
	CellLayer<uint8_t> m_heightLayer; // 地形高度 0..255
	CellLayer<uint8_t> m_flags;       // TerrainFlags 位集
	CellLayer<uint8_t> m_fog;         // 迷雾 0..255
	CellLayer<uint8_t> m_resTypes;    // 资源类型（0 = 无，1 = Ore）——进地图文件
	CellLayer<int32_t> m_resources;   // 矿点剩余量（运行时）
	CellLayer<uint32_t> m_occupancy;  // 建筑占用（EntityId）
};

} // namespace rts

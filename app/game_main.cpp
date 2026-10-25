// RTS Engine — app/game_main.cpp
// M9 可玩 Demo：玩家（P0）vs AI（P1）
// 操作：
//   左键点选/拖拽框选己方单位
//   右键：移动 / 攻击（指向敌方单位时）
//   B：在相机中心建造兵营（需 300 资源）
//   P：让所有兵营生产步兵（需 50/个）
//   数字 1/2/3：按类型全选（1=步兵 2=坦克 3=采矿车）
//   方向键/WASD 平移，滚轮缩放
//   右下角小地图
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <vector>
#include <algorithm>

#include "core/core.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"
#include "render/snapshot.h"
#include "render/sprite_batch.h"
#include "render/camera.h"
#include "render/gl_renderer.h"
#include "platform/win32_window.h"

using namespace rts;

namespace {

const Rules kRules = Rules::FromYAML(R"(
units:
  harvester:
    name: "采矿车"
    cost: 100
    hp: 150
    speed: 28
    harvester: true
    weapon:
      damage: 0
      range: 0
      cooldown: 60
  infantry:
    name: "步兵"
    cost: 50
    buildTime: 10
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 4096
      cooldown: 15
  tank:
    name: "坦克"
    cost: 150
    buildTime: 30
    hp: 300
    speed: 20
    weapon:
      damage: 25
      range: 5120
      cooldown: 30
buildings:
  barracks:
    name: "兵营"
    cost: 300
    buildTime: 20
    hp: 500
    width: 4
    height: 4
    produces:
      - infantry
      - tank
)");

Map MakeMap() {
	Map map = Map::GenerateTestMap(128, 128, 2024);
	map.SetResourceAmount(CellCoord(30, 30), 8000);
	map.SetResourceAmount(CellCoord(98, 98), 8000);
	return map;
}

// 屏幕 → 世界（米）
struct WorldPoint {
	float x = 0, y = 0;
};

} // namespace

int main() {
	Win32Window window;
	if (!window.Create("RTS Engine — M9 Playable Demo (You vs AI)", 1280, 720)) return 1;

	GLRenderer renderer;
	if (!renderer.Initialize(window.DeviceContext())) return 1;

	Simulation sim(MakeMap(), 0x2024, kRules);
	sim.AddAI(1, 99); // AI 玩家 1
	sim.GetWorld().AddCredits(0, 2000);
	sim.GetWorld().AddCredits(1, 1500);

	// 玩家初始单位：1 采矿车 + 2 步兵
	{
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = 0;
		c.x = 60 * kSubPerCell;
		c.y = 60 * kSubPerCell;
		c.targetId = 0; // harvester
		sim.Commands().Push(c);
		sim.Step();
		c.targetId = 1; // infantry
		c.x = 62 * kSubPerCell;
		sim.Commands().Push(c);
		c.x = 60 * kSubPerCell;
		c.y = 62 * kSubPerCell;
		sim.Commands().Push(c);
	}

	Camera camera;
	camera.viewW = window.Width();
	camera.viewH = window.Height();
	{
		const ScreenPos c = ProjectCell(CellCoord(60, 60), 0);
		camera.centerX = static_cast<float>(c.x);
		camera.centerY = static_cast<float>(c.y);
		camera.zoom = 1.0f;
	}

	std::vector<EntityId> selection;

	// 输入状态
	bool prevLeft = false;
	int dragStartX = 0, dragStartY = 0;
	bool dragging = false;
	bool prevB = false, prevP = false;
	int prevNum[3] = {0, 0, 0};

	auto last = std::chrono::steady_clock::now();
	double accumulator = 0.0;
	constexpr double tickDt = 1.0 / 30.0;
	double fpsTimer = 0.0;
	int fpsFrames = 0;

	while (window.PumpMessages()) {
		const auto now = std::chrono::steady_clock::now();
		double dt = std::chrono::duration<double>(now - last).count();
		last = now;
		if (dt > 0.1) dt = 0.1;

		// ---- 相机 ----
		const float panSpeed = 500.0f * static_cast<float>(dt) / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= panSpeed;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += panSpeed;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= panSpeed;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += panSpeed;

		const int wheel = window.ConsumeWheel();
		if (wheel != 0) {
			camera.ZoomAt((wheel > 0) ? 1.1f : 1.0f / 1.1f,
				static_cast<float>(window.MouseX()), static_cast<float>(window.MouseY()));
		}
		camera.viewW = window.Width();
		camera.viewH = window.Height();

		// ---- 鼠标屏幕 → 世界 ----
		WorldPoint mouseWorld;
		camera.ScreenToWorld(static_cast<float>(window.MouseX()),
			static_cast<float>(window.MouseY()), mouseWorld.x, mouseWorld.y);

		// ---- 左键：选择（点选/框选）----
		const bool left = window.IsMouseDown(0);
		if (left && !prevLeft) {
			dragStartX = window.MouseX();
			dragStartY = window.MouseY();
			dragging = true;
		}
		if (!left && prevLeft && dragging) {
			dragging = false;
			const int dx = window.MouseX() - dragStartX;
			const int dy = window.MouseY() - dragStartY;
			const int dragDist = dx * dx + dy * dy;

			if (dragDist < 36) {
				// 点选：选择最近的己方单位
				const SubCoord clickSub(static_cast<int32_t>(mouseWorld.x) * kSubPerCell / kTileW * 2,
					0); // 仅用于距离比较的粗略值
				(void)clickSub;
				EntityId best = 0;
				int64_t bestDist = INT64_MAX;
				for (const auto& a : sim.GetWorld().Entities().All()) {
					if (!a.alive || a.playerId != 0) continue;
					const ScreenPos p = ProjectCell(a.pos.ToCell(), 0);
					const int64_t ddx = static_cast<int64_t>(p.x) - static_cast<int64_t>(mouseWorld.x);
					const int64_t ddy = static_cast<int64_t>(p.y) - static_cast<int64_t>(mouseWorld.y);
					const int64_t d2 = ddx * ddx + ddy * ddy;
					if (d2 < bestDist && d2 < 32 * 32) { bestDist = d2; best = a.id; }
				}
				if (best != 0) selection.assign(1, best);
				else selection.clear();
			} else {
				// 框选
				selection.clear();
				const int x0 = std::min(dragStartX, window.MouseX());
				const int x1 = std::max(dragStartX, window.MouseX());
				const int y0 = std::min(dragStartY, window.MouseY());
				const int y1 = std::max(dragStartY, window.MouseY());
				for (const auto& a : sim.GetWorld().Entities().All()) {
					if (!a.alive || a.playerId != 0) continue;
					const ScreenPos p = ProjectCell(a.pos.ToCell(), 0);
					float sx = 0, sy = 0;
					camera.WorldToScreen(static_cast<float>(p.x), static_cast<float>(p.y), sx, sy);
					if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) {
						selection.push_back(a.id);
					}
				}
			}
		}
		prevLeft = left;

		// ---- 右键：移动/攻击 ----
		{
			static bool prevRight = false;
			const bool right = window.IsMouseDown(1);
			if (right && !prevRight && !selection.empty()) {
				// 找点击位置最近的敌方单位
				EntityId enemy = 0;
				int64_t bestDist = INT64_MAX;
				for (const auto& a : sim.GetWorld().Entities().All()) {
					if (!a.alive || a.playerId == 0) continue;
					const ScreenPos p = ProjectCell(a.pos.ToCell(), 0);
					const int64_t ddx = static_cast<int64_t>(p.x) - static_cast<int64_t>(mouseWorld.x);
					const int64_t ddy = static_cast<int64_t>(p.y) - static_cast<int64_t>(mouseWorld.y);
					const int64_t d2 = ddx * ddx + ddy * ddy;
					if (d2 < bestDist && d2 < 32 * 32) { bestDist = d2; enemy = a.id; }
				}
				for (EntityId id : selection) {
					Command cmd;
					if (enemy != 0) {
						cmd.type = CommandType::Attack;
						cmd.entityId = id;
						cmd.targetId = enemy;
					} else {
						cmd.type = CommandType::Move;
						cmd.entityId = id;
						cmd.x = static_cast<int32_t>(mouseWorld.x / kTileW * 2) * kSubPerCell / 2;
						cmd.y = static_cast<int32_t>(mouseWorld.y / kTileH * 2) * kSubPerCell / 2;
					}
					sim.Commands().Push(cmd);
				}
			}
			prevRight = right;
		}

		// ---- 建造（B）：在相机中心建兵营 ----
		{
			const bool b = window.IsKeyDown('B');
			if (b && !prevB) {
				float wx = camera.centerX, wy = camera.centerY;
				const CellCoord center = ScreenToCell(ScreenPos{static_cast<int32_t>(wx), static_cast<int32_t>(wy)});
				Command cmd;
				cmd.type = CommandType::Build;
				cmd.playerId = 0;
				cmd.x = center.x * kSubPerCell;
				cmd.y = center.y * kSubPerCell;
				cmd.targetId = 0; // barracks
				sim.Commands().Push(cmd);
			}
			prevB = b;
		}

		// ---- 生产（P）：所有空闲兵营生产步兵 ----
		{
			const bool p = window.IsKeyDown('P');
			if (p && !prevP) {
				for (const auto& a : sim.GetWorld().Entities().All()) {
					if (!a.alive || a.playerId != 0 || !a.Has(kCompBuilding)) continue;
					if (a.producer.queued > 0) continue;
					Command cmd;
					cmd.type = CommandType::Produce;
					cmd.entityId = a.id;
					cmd.targetId = 1; // infantry
					sim.Commands().Push(cmd);
				}
			}
			prevP = p;
		}

		// ---- 快速选择（1/2/3）----
		{
			const char keys[3] = {'1', '2', '3'};
			for (int i = 0; i < 3; ++i) {
				const bool down = window.IsKeyDown(keys[i]);
				if (down && !prevNum[i]) {
					selection.clear();
					for (const auto& a : sim.GetWorld().Entities().All()) {
						if (!a.alive || a.playerId != 0) continue;
						if (i == 0 && !a.Has(kCompBuilding) && !a.Has(kCompHarvester) && a.armament.damage > 0) selection.push_back(a.id);
						if (i == 1 && !a.Has(kCompBuilding) && a.armament.damage >= 20) selection.push_back(a.id);
						if (i == 2 && a.Has(kCompHarvester)) selection.push_back(a.id);
					}
				}
				prevNum[i] = down;
			}
		}

		// ---- 逻辑 tick ----
		accumulator += dt;
		while (accumulator >= tickDt) {
			sim.Step();
			accumulator -= tickDt;
		}

		// ---- 渲染 ----
		const RenderSnapshot snapshot = RenderSnapshot::FromWorld(sim.GetWorld());
		SpriteBatch batch;
		batch.Begin();
		for (const auto& s : snapshot.sprites) {
			batch.AddSolid(s.worldX, s.worldY, s.w, s.h, s.color);
		}
		// 选中高亮
		for (EntityId id : selection) {
			const Actor* a = sim.GetWorld().Entities().Get(id);
			if (a == nullptr || !a->alive) continue;
			const ScreenPos p = ProjectCell(a->pos.ToCell(), 0);
			batch.AddSolid(static_cast<float>(p.x) - 12.0f, static_cast<float>(p.y) - 12.0f,
				24.0f, 24.0f, 0x8000FF00);
		}
		// 右键目标位置标记（简单：不绘制）

		// 小地图（右下角）
		{
			const float mmSize = 200.0f;
			const float mmX = static_cast<float>(window.Width()) - mmSize - 10.0f;
			const float mmY = static_cast<float>(window.Height()) - mmSize - 10.0f;
			const float scale = mmSize / static_cast<float>(std::max(sim.GetWorld().Width(), sim.GetWorld().Height()));
			// 背景
			batch.AddSolid(mmX, mmY, mmSize, mmSize, 0xCC000000);
			// 单位点
			for (const auto& a : sim.GetWorld().Entities().All()) {
				if (!a.alive) continue;
				const float mx = mmX + static_cast<float>(a.pos.ToCell().x) * scale;
				const float my = mmY + static_cast<float>(a.pos.ToCell().y) * scale;
				const uint32_t color = (a.playerId == 0) ? 0xFF00C8FF : 0xFFFF4040;
				batch.AddSolid(mx - 1.0f, my - 1.0f, 2.0f, 2.0f, color);
			}
			(void)mmX; (void)mmY;
		}
		batch.End();

		renderer.BeginFrame(window.Width(), window.Height());
		renderer.DrawBatch(batch.Vertices(), camera);
		renderer.EndFrame();

		// ---- 标题（HUD）----
		fpsTimer += dt;
		++fpsFrames;
		if (fpsTimer >= 0.4) {
			const double fps = fpsFrames / fpsTimer;
			char title[256];
			int p0Units = 0, p1Units = 0, buildings = 0;
			for (const auto& a : sim.GetWorld().Entities().All()) {
				if (!a.alive) continue;
				if (a.playerId == 0) { if (a.Has(kCompBuilding)) ++buildings; else ++p0Units; }
				else ++p1Units;
			}
			std::snprintf(title, sizeof(title),
				"M9 Demo | FPS %.0f | Credits %d | My units %d | Buildings %d | Enemy %d | Selected %zu",
				fps, sim.GetWorld().Credits(0), p0Units, buildings, p1Units, selection.size());
			SetWindowTextA(window.Handle(), title);
			fpsTimer = 0.0;
			fpsFrames = 0;
		}
	}

	renderer.Shutdown();
	window.Destroy();
	return 0;
}

// RTS Engine — app/game_main.cpp
// M10 可玩 Demo：玩家（P0）vs AI（P1），完整 HUD + 命令面板。
// 操作：
//   左键点选/拖拽框选己方单位；右键移动/攻击
//   底部命令面板按钮：建造兵营 / 生产步兵 / 生产坦克
//   1/2/3 按类型全选；WASD/方向键平移；滚轮缩放
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>

#include "core/core.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"
#include "render/snapshot.h"
#include "render/sprite_batch.h"
#include "render/camera.h"
#include "render/gl_renderer.h"
#include "render/font.h"
#include "render/text.h"
#include "render/ui.h"
#include "platform/win32_window.h"
#include "audio/audio.h"
#include "sim/campaign.h"

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

struct WorldPoint { float x = 0, y = 0; };

} // namespace

int main() {
	Win32Window window;
	if (!window.Create("RTS Engine — M10 HUD (You vs AI)", 1280, 720)) return 1;

	GLRenderer renderer;
	if (!renderer.Initialize(window.DeviceContext())) return 1;

	// 字体纹理
	int fontW = 0, fontH = 0;
	const auto fontPixels = BitmapFont::BuildTextureData(fontW, fontH);
	const uint32_t fontTex = renderer.UploadTexture(fontPixels, fontW, fontH);

	// 音频
	AudioSystem audio;
	const bool audioOk = audio.Init();
	if (audioOk) {
		audio.SetMasterVolume(0.5f);
		audio.StartMusic();
	}

	Simulation sim(MakeMap(), 0x2024, kRules);
	sim.AddAI(1, 99);
	sim.GetWorld().AddCredits(0, 2000);
	sim.GetWorld().AddCredits(1, 1500);

	// M15：加载关卡
	LevelDef levelDef;
	LoadLevel("levels/level1.yaml", levelDef);
	Campaign campaign(levelDef, 0, 1);

	// 玩家初始单位
	{
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = 0;
		c.x = 60 * kSubPerCell;
		c.y = 60 * kSubPerCell;
		c.targetId = 0;
		sim.Commands().Push(c);
		sim.Step();
		c.targetId = 1;
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

	// UI 按钮（屏幕坐标，随窗口尺寸更新）
	ui::Button btnBuild;
	ui::Button btnInfantry;
	ui::Button btnTank;

	// 输入状态
	bool prevLeft = false;
	int dragStartX = 0, dragStartY = 0;
	bool dragging = false;
	int prevNum[3] = {0, 0, 0};

	auto last = std::chrono::steady_clock::now();
	double accumulator = 0.0;
	constexpr double tickDt = 1.0 / 30.0;
	double fpsTimer = 0.0;
	int fpsFrames = 0;
	double fps = 0.0;

	while (window.PumpMessages()) {
		const auto now = std::chrono::steady_clock::now();
		double dt = std::chrono::duration<double>(now - last).count();
		last = now;
		if (dt > 0.1) dt = 0.1;

		const int W = window.Width();
		const int H = window.Height();
		const float mx = static_cast<float>(window.MouseX());
		const float my = static_cast<float>(window.MouseY());

		// ---- 相机 ----
		const float panSpeed = 500.0f * static_cast<float>(dt) / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= panSpeed;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += panSpeed;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= panSpeed;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += panSpeed;

		const int wheel = window.ConsumeWheel();
		if (wheel != 0) {
			camera.ZoomAt((wheel > 0) ? 1.1f : 1.0f / 1.1f, mx, my);
		}
		camera.viewW = W;
		camera.viewH = H;

		// ---- 布局 ----
		const float topBarH = 36.0f;
		const float bottomH = 90.0f;
		btnBuild.rect = ui::Rect{10.0f, static_cast<float>(H) - bottomH + 14.0f, 170.0f, 62.0f};
		btnBuild.label = "BUILD (300)";
		btnInfantry.rect = ui::Rect{192.0f, static_cast<float>(H) - bottomH + 14.0f, 170.0f, 62.0f};
		btnInfantry.label = "INFANTRY (50)";
		btnTank.rect = ui::Rect{374.0f, static_cast<float>(H) - bottomH + 14.0f, 170.0f, 62.0f};
		btnTank.label = "TANK (150)";

		const int credits = sim.GetWorld().Credits(0);
		int myUnits = 0, myBuildings = 0, enemyUnits = 0;
		EntityId idleBarracks = 0;
		for (const auto& a : sim.GetWorld().Entities().All()) {
			if (!a.alive) continue;
			if (a.playerId == 0) {
				if (a.Has(kCompBuilding)) {
					++myBuildings;
					if (idleBarracks == 0 && a.producer.queued == 0) idleBarracks = a.id;
				} else ++myUnits;
			} else ++enemyUnits;
		}
		btnBuild.enabled = (credits >= 300);
		btnInfantry.enabled = (idleBarracks != 0 && credits >= 50);
		btnTank.enabled = (idleBarracks != 0 && credits >= 150);

		// ---- 左键 ----
		const bool left = window.IsMouseDown(0);
		if (!left && prevLeft && dragging) {
			dragging = false;
			const int dx = window.MouseX() - dragStartX;
			const int dy = window.MouseY() - dragStartY;
			if (dx * dx + dy * dy < 36) {
				WorldPoint mp;
				camera.ScreenToWorld(mx, my, mp.x, mp.y);
				EntityId best = 0;
				int64_t bestDist = INT64_MAX;
				for (const auto& a : sim.GetWorld().Entities().All()) {
					if (!a.alive || a.playerId != 0) continue;
					const ScreenPos p = ProjectCell(a.pos.ToCell(), 0);
					const int64_t ddx = static_cast<int64_t>(p.x) - static_cast<int64_t>(mp.x);
					const int64_t ddy = static_cast<int64_t>(p.y) - static_cast<int64_t>(mp.y);
					const int64_t d2 = ddx * ddx + ddy * ddy;
					if (d2 < bestDist && d2 < 36 * 36) { bestDist = d2; best = a.id; }
				}
				if (best != 0) { selection.assign(1, best); audio.Play(Sfx::Click); }
				else selection.clear();
			} else {
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
					if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) selection.push_back(a.id);
				}
			}
		}

		// 左键按下：UI 按钮 or 开始框选
		if (left && !prevLeft) {
			if (my < topBarH || my > static_cast<float>(H) - bottomH) {
				if (btnBuild.Hit(mx, my) && myBuildings == 0) {
					const CellCoord center = ScreenToCell(ScreenPos{static_cast<int32_t>(camera.centerX),
						static_cast<int32_t>(camera.centerY)});
					Command cmd;
					cmd.type = CommandType::Build;
					cmd.playerId = 0;
					cmd.x = center.x * kSubPerCell;
					cmd.y = center.y * kSubPerCell;
					cmd.targetId = 0;
					sim.Commands().Push(cmd);
					audio.Play(Sfx::Build);
				} else if (btnTank.Hit(mx, my) && idleBarracks != 0) {
					Command cmd;
					cmd.type = CommandType::Produce;
					cmd.entityId = idleBarracks;
					cmd.targetId = 2;
					sim.Commands().Push(cmd);
					audio.Play(Sfx::Produce);
				} else if (btnInfantry.Hit(mx, my) && idleBarracks != 0) {
					Command cmd;
					cmd.type = CommandType::Produce;
					cmd.entityId = idleBarracks;
					cmd.targetId = 1;
					sim.Commands().Push(cmd);
					audio.Play(Sfx::Produce);
				}
			} else {
				dragStartX = window.MouseX();
				dragStartY = window.MouseY();
				dragging = true;
			}
		}
		prevLeft = left;

		// ---- 右键：移动/攻击 ----
		{
			static bool prevRight = false;
			const bool right = window.IsMouseDown(1);
			if (right && !prevRight && !selection.empty()) {
				WorldPoint mp;
				camera.ScreenToWorld(mx, my, mp.x, mp.y);
				EntityId enemy = 0;
				int64_t bestDist = INT64_MAX;
				for (const auto& a : sim.GetWorld().Entities().All()) {
					if (!a.alive || a.playerId == 0) continue;
					const ScreenPos p = ProjectCell(a.pos.ToCell(), 0);
					const int64_t ddx = static_cast<int64_t>(p.x) - static_cast<int64_t>(mp.x);
					const int64_t ddy = static_cast<int64_t>(p.y) - static_cast<int64_t>(mp.y);
					const int64_t d2 = ddx * ddx + ddy * ddy;
					if (d2 < bestDist && d2 < 36 * 36) { bestDist = d2; enemy = a.id; }
				}
				for (EntityId id : selection) {
					Command cmd;
					if (enemy != 0) {
						cmd.type = CommandType::Attack;
						cmd.entityId = id;
						cmd.targetId = enemy;
						audio.Play(Sfx::Attack);
					} else {
						cmd.type = CommandType::Move;
						cmd.entityId = id;
						cmd.x = static_cast<int32_t>(mp.x / kTileW * 2) * kSubPerCell / 2;
						cmd.y = static_cast<int32_t>(mp.y / kTileH * 2) * kSubPerCell / 2;
					}
					sim.Commands().Push(cmd);
				}
			}
			prevRight = right;
		}

		// ---- 静音切换（M）----
		{
			static bool prevM = false;
			const bool mDown = window.IsKeyDown('M');
			if (mDown && !prevM && audioOk) {
				const float v = (audio.MasterVolume() > 0.01f) ? 0.0f : 0.5f;
				audio.SetMasterVolume(v);
			}
			prevM = mDown;
		}

		// ---- 快速选择 1/2/3 ----
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
			// 关卡：生成波次 + 判定胜负
			if (!campaign.Finished()) {
				campaign.Update(sim.GetWorld(), sim.Commands(), sim.GetWorld().CurrentTick());
			}
			// 重新开始（R 键）
			{
				static bool prevR = false;
				const bool rDown = window.IsKeyDown('R');
				if (rDown && !prevR && campaign.Finished()) {
					// 重建世界与关卡
					sim = Simulation(MakeMap(), 0x2024, kRules);
					sim.AddAI(1, 99);
					sim.GetWorld().AddCredits(0, 2000);
					sim.GetWorld().AddCredits(1, 1500);
					campaign = Campaign(levelDef, 0, 1);
					selection.clear();
				}
				prevR = rDown;
			}
			sim.Step();
			accumulator -= tickDt;
		}

		// ---- 渲染：世界 ----
		const RenderSnapshot snapshot = RenderSnapshot::FromWorld(sim.GetWorld());
		SpriteBatch worldBatch;
		worldBatch.Begin();
		for (const auto& s : snapshot.sprites) {
			worldBatch.AddSolid(s.worldX, s.worldY, s.w, s.h, s.color);
		}
		for (EntityId id : selection) {
			const Actor* a = sim.GetWorld().Entities().Get(id);
			if (a == nullptr || !a->alive) continue;
			const ScreenPos p = ProjectCell(a->pos.ToCell(), 0);
			// 选中高亮
			worldBatch.AddSolid(static_cast<float>(p.x) - 12.0f, static_cast<float>(p.y) - 12.0f,
				24.0f, 24.0f, 0x8000FF00);
			// 生命条（单位上方）
			if (a->health.maxHp > 0) {
				const float barW = 28.0f;
				const float barH = 4.0f;
				const float bx = static_cast<float>(p.x) - barW * 0.5f;
				const float by = static_cast<float>(p.y) - 20.0f;
				const float ratio = static_cast<float>(a->health.hp) / static_cast<float>(a->health.maxHp);
				worldBatch.AddSolid(bx, by, barW, barH, 0xFF202020);
				worldBatch.AddSolid(bx, by, barW * ratio, barH, 0xFF40E040);
			}
		}
		worldBatch.End();

		// ---- 渲染：UI ----
		std::vector<SpriteVertex> uiVerts;
		ui::BuildPanel(uiVerts, ui::Rect{0, 0, static_cast<float>(W), topBarH}, 0xE6202020);
		ui::BuildPanel(uiVerts, ui::Rect{0, static_cast<float>(H) - bottomH,
			static_cast<float>(W), bottomH}, 0xE6202020);

		const uint32_t btnFill = 0xFF2E4A2E;
		const uint32_t btnFillDisabled = 0xFF3A3A3A;
		const uint32_t btnBorder = 0xFF889988;
		ui::BuildButton(uiVerts, btnBuild, btnBuild.enabled ? btnFill : btnFillDisabled, btnBorder);
		ui::BuildButton(uiVerts, btnInfantry, btnInfantry.enabled ? btnFill : btnFillDisabled, btnBorder);
		ui::BuildButton(uiVerts, btnTank, btnTank.enabled ? btnFill : btnFillDisabled, btnBorder);

		// 小地图
		{
			const float mmSize = 150.0f;
			const float mmX = static_cast<float>(W) - mmSize - 10.0f;
			const float mmY = static_cast<float>(H) - bottomH - mmSize - 10.0f;
			ui::BuildPanel(uiVerts, ui::Rect{mmX, mmY, mmSize, mmSize}, 0xCC000000);
			const float scale = mmSize / static_cast<float>(std::max(sim.GetWorld().Width(),
				sim.GetWorld().Height()));
			for (const auto& a : sim.GetWorld().Entities().All()) {
				if (!a.alive) continue;
				const float px = mmX + static_cast<float>(a.pos.ToCell().x) * scale;
				const float py = mmY + static_cast<float>(a.pos.ToCell().y) * scale;
				const uint32_t color = (a.playerId == 0) ? 0xFF00C8FF : 0xFFFF4040;
				ui::BuildPanel(uiVerts, ui::Rect{px - 1, py - 1, 2, 2}, color);
			}
		}

		renderer.BeginFrame(W, H);
		renderer.DrawBatch(worldBatch.Vertices(), camera);
		renderer.DrawScreenBatch(uiVerts);

		// 文字
		std::vector<SpriteVertex> textVerts;
		{
			char line[160];
			std::snprintf(line, sizeof(line), "CREDITS %d   UNITS %d   BUILDINGS %d   ENEMY %d   FPS %.0f",
				credits, myUnits, myBuildings, enemyUnits, fps);
			text::Build(textVerts, line, 12.0f, 8.0f, 1.5f, 0xFFE0E0E0);

			text::Build(textVerts, btnBuild.label,
				btnBuild.rect.x + 10.0f, btnBuild.rect.y + 18.0f, 1.6f,
				btnBuild.enabled ? 0xFFFFFFFF : 0xFF888888);
			text::Build(textVerts, btnInfantry.label,
				btnInfantry.rect.x + 10.0f, btnInfantry.rect.y + 18.0f, 1.6f,
				btnInfantry.enabled ? 0xFFFFFFFF : 0xFF888888);
			text::Build(textVerts, btnTank.label,
				btnTank.rect.x + 10.0f, btnTank.rect.y + 18.0f, 1.6f,
				btnTank.enabled ? 0xFFFFFFFF : 0xFF888888);

			char info[160];
			std::snprintf(info, sizeof(info), "SELECTED %zu", selection.size());
			text::Build(textVerts, info, btnTank.rect.x + btnTank.rect.w + 30.0f,
				static_cast<float>(H) - bottomH + 20.0f, 1.5f, 0xFFC0C0C0);

			// 生产进度
			const Actor* producing = nullptr;
			for (const auto& a : sim.GetWorld().Entities().All()) {
				if (!a.alive || a.playerId != 0 || !a.Has(kCompBuilding)) continue;
				if (a.producer.queued > 0) { producing = &a; break; }
			}
			if (producing != nullptr && producing->producer.total > 0) {
				const float px = btnTank.rect.x + btnTank.rect.w + 30.0f;
				const float py = static_cast<float>(H) - bottomH + 52.0f;
				const float ratio = static_cast<float>(producing->producer.total - producing->producer.remaining)
					/ static_cast<float>(producing->producer.total);
				ui::BuildPanel(uiVerts, ui::Rect{px, py, 140.0f, 14.0f}, 0xFF202020);
				ui::BuildPanel(uiVerts, ui::Rect{px, py, 140.0f * ratio, 14.0f}, 0xFF40A0FF);
				char prodText[64];
				std::snprintf(prodText, sizeof(prodText), "PRODUCING %d%%", static_cast<int>(ratio * 100.0f));
				text::Build(textVerts, prodText, px, py + 18.0f, 1.2f, 0xFFA0C0FF);
			}
		}
		renderer.DrawScreenTextured(textVerts, fontTex);
		renderer.EndFrame();

		// ---- FPS ----
		fpsTimer += dt;
		++fpsFrames;
		if (fpsTimer >= 0.4) {
			fps = fpsFrames / fpsTimer;
			fpsTimer = 0.0;
			fpsFrames = 0;
		}
	}

	audio.Shutdown();
	renderer.Shutdown();
	window.Destroy();
	return 0;
}

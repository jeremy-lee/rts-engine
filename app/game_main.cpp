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
#include <fstream>
#include <sstream>

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
#include "render/software_renderer.h"
#include "render/assets.h"
#include "platform/win32_window.h"
#include "audio/audio.h"
#include "sim/campaign.h"
#include "net/lockstep.h"

using namespace rts;

namespace {

const Rules kRules = Rules::FromYAML(R"(
units:
  harvester:
    name: "采矿车"
    cost: 100
    hp: 150
    speed: 120
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
    speed: 160
    weapon:
      damage: 10
      range: 4096
      cooldown: 15
  tank:
    name: "坦克"
    cost: 150
    buildTime: 30
    hp: 300
    speed: 100
    weapon:
      damage: 25
      range: 5120
      cooldown: 30
upgrade:
  cost: 300
  attack: 5
  hp: 50
  speed: 2
buildings:
  turret:
    name: "防御塔"
    cost: 200
    buildTime: 15
    hp: 400
    width: 2
    height: 2
    turret: true
    weapon:
      damage: 20
      range: 6144
      cooldown: 45
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

// 把工作目录设为「含 assets/ 的目录」（exe 目录或其父目录）
static void SetWorkingDirToExe() {
	char buf[MAX_PATH] = {};
	if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return;
	std::string p(buf);
	const size_t pos = p.find_last_of("\\/");
	if (pos == std::string::npos) return;
	const std::string dir = p.substr(0, pos);
	const size_t pos2 = dir.find_last_of("\\/");
	if (pos2 != std::string::npos) {
		const std::string parent = dir.substr(0, pos2);
		if (GetFileAttributesA((parent + "/assets").c_str()) != INVALID_FILE_ATTRIBUTES) {
			SetCurrentDirectoryA(parent.c_str());
			return;
		}
	}
	if (GetFileAttributesA((dir + "/assets").c_str()) != INVALID_FILE_ATTRIBUTES) {
		SetCurrentDirectoryA(dir.c_str());
	}
}

int main(int argc, char** argv) {
	SetWorkingDirToExe();
	// M22：联机模式（--host 或 --client <ip>）
	LockstepSession netSession;
	bool netMode = false;
	bool netHost = false;
	if (argc >= 2) {
		const std::string arg = argv[1];
		if (arg == "--host") {
			netMode = true;
			netHost = true;
		} else if (arg == "--client" && argc >= 3) {
			netMode = true;
			netHost = false;
		}
	}

	Win32Window window;
	if (!window.Create("RTS Engine — M10 HUD (You vs AI)", 1280, 720)) return 1;

	GLRenderer renderer;
	if (!renderer.Initialize(window.DeviceContext())) return 1;

	// 字体纹理
	int fontW = 0, fontH = 0;
	const auto fontPixels = BitmapFont::BuildTextureData(fontW, fontH);
	const uint32_t fontTex = renderer.UploadTexture(fontPixels, fontW, fontH);

	// M16：美术图集纹理
	int atlasW = 0, atlasH = 0;
	const auto atlasPixels = AssetAtlas::Build(atlasW, atlasH);
	const uint32_t atlasTex = renderer.UploadTexture(atlasPixels, atlasW, atlasH);

	// 地形纹理（程序化 64×32，与网格 1:1）
	int terrW = 0, terrH = 0;
	std::vector<uint8_t> terrPixels;
	if (!TerrainGfx::Load("assets/terrain_atlas.bmp", terrPixels, terrW, terrH)) {
		terrPixels = TerrainGfx::Build(terrW, terrH); // 回退：程序化纯色
	}
	const uint32_t terrainTex = renderer.UploadTexture(terrPixels, terrW, terrH);

	// M16：C&C 素材图集（占位美术，OpenRA 提取）
	CncAtlas::LoadMeta("assets/cnc_atlas.txt"); // 精灵尺寸/布局（缺失会让单位尺寸为 0）
	std::vector<uint8_t> cncPixels;
	int cncW = 0, cncH = 0;
	const bool cncOk = CncAtlas::Load("assets/cnc_atlas.bmp", cncPixels, cncW, cncH);
	const uint32_t cncTex = cncOk ? renderer.UploadTexture(cncPixels, cncW, cncH) : 0;

	// 软件渲染用：精灵代表色（无纹理时）
	auto spriteRepColor = [](int idx) -> uint32_t {
		switch (static_cast<SpriteId>(idx)) {
			case SpriteId::Grass: return 0xFF3A5A2A;
			case SpriteId::Water: return 0xFF1A3A6B;
			case SpriteId::Sand: return 0xFFA68C5A;
			case SpriteId::Rock: return 0xFF555555;
			case SpriteId::Highland: return 0xFF4A7A3A;
			case SpriteId::Barracks: return 0xFFB0A080;
			case SpriteId::Tank: return 0xFF708060;
			default: return 0xFFC0C0C0;
		}
	};

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

	// M20：关卡流程（选关 → 游玩 → 结算）
	const std::vector<std::string> levelPaths = {
		"levels/level1.yaml", "levels/level2.yaml", "levels/level3.yaml",
		"levels/level4.yaml", "levels/level5.yaml", "levels/level6.yaml",
	};
	enum class GameState { LevelSelect, Playing, Result };
	GameState gameState = GameState::LevelSelect;
	int currentLevel = 0;

	// 预加载关卡名
	std::vector<std::string> levelNames;
	for (const auto& p : levelPaths) {
		LevelDef ld;
		if (LoadLevel(p, ld)) levelNames.push_back(ld.name);
		else levelNames.push_back(p);
	}

	// M22：联机会话
	bool netReady = false;
	if (netMode) {
		NetInit();
		const uint16_t port = 39020;
		if (netHost) {
			netReady = netSession.StartHost(port, 1);
		} else {
			netReady = netSession.StartClient(argv[2], port);
		}
		if (!netReady) {
			std::printf("[net] 连接失败\n");
			return 1;
		}
	}

	// 联机模式：本地命令缓冲
	std::vector<Command> localCmds;
	auto pushCommand = [&](const Command& cmd) {
		if (netMode) localCmds.push_back(cmd);
		else sim.Commands().Push(cmd);
	};

	LevelDef levelDef;
	Campaign campaign;

	std::vector<EntityId> selection;

	auto startLevel = [&](int idx) {
		if (idx < 0 || idx >= static_cast<int>(levelPaths.size())) return;
		currentLevel = idx;
		levelDef = LevelDef();
		LoadLevel(levelPaths[idx], levelDef);
		sim = Simulation(MakeMap(), 0x2024, kRules);
		sim.AddAI(1, 99);
		sim.GetWorld().AddCredits(0, 2000);
		sim.GetWorld().AddCredits(1, 1500);
		{
			Command c;
			c.type = CommandType::Spawn;
			c.playerId = 0;
			c.x = 60 * kSubPerCell;
			c.y = 60 * kSubPerCell;
			c.targetId = 0;
			pushCommand(c);
			sim.Step();
			c.targetId = 1;
			c.x = 62 * kSubPerCell;
			pushCommand(c);
			c.x = 60 * kSubPerCell;
			c.y = 62 * kSubPerCell;
			pushCommand(c);
		}
		campaign = Campaign(levelDef, 0, 1);
		selection.clear();
		gameState = GameState::Playing;
	};

	// M22：联机模式直接开始一局（无战役波次，双方各 1 个步兵）
	if (netMode) {
		const uint8_t myPlayer = netSession.LocalPlayerId();
		sim = Simulation(MakeMap(), 0x2024, kRules);
		sim.GetWorld().AddCredits(0, 2000);
		sim.GetWorld().AddCredits(1, 2000);
		campaign = Campaign(LevelDef(), 0, 1);
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = myPlayer;
		c.x = (myPlayer == 0) ? 60 * kSubPerCell : 75 * kSubPerCell;
		c.y = (myPlayer == 0) ? 60 * kSubPerCell : 75 * kSubPerCell;
		c.targetId = 1; // infantry
		localCmds.push_back(c);
		gameState = GameState::Playing;
	}

	Camera camera;
	camera.viewW = window.Width();
	camera.viewH = window.Height();
	{
		const ScreenPos c = ProjectCell(CellCoord(60, 60), 0);
		camera.centerX = static_cast<float>(c.x);
		camera.centerY = static_cast<float>(c.y);
		camera.zoom = 1.8f;
	}

	// M14：渲染后端切换（F1 = GL / 软件渲染）
	bool softwareMode = false; // TEMP: GL 验证
	bool revealAll = true;    // TEMP: 全揭示验证
	float markerX = 0.0f, markerY = 0.0f; // 命令目标标记（世界坐标）
	float markerTimer = 0.0f;
	uint32_t swTex = 0;
	SoftwareRenderer swRenderer;
	double swFrameMs = 0.0;

	// UI 按钮（屏幕坐标，随窗口尺寸更新）
	ui::Button btnBuild;
	ui::Button btnInfantry;
	ui::Button btnTank;
	ui::Button btnTurret;

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
		{
			// OpenRA Viewport.ClampToBounds：限制相机在地图内
			const int32_t mw = sim.GetWorld().Width();
			const int32_t mh = sim.GetWorld().Height();
			camera.ClampToBounds(static_cast<float>(mw) * -16.0f, 0.0f,
				static_cast<float>(mw) * 16.0f, static_cast<float>(mw + mh) * 16.0f);
		}
		const float panSpeed = 500.0f * static_cast<float>(dt) / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= panSpeed;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += panSpeed;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= panSpeed;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += panSpeed;

		const int wheel = window.ConsumeWheel();
		if (wheel != 0) {
			camera.ZoomAt((wheel > 0) ? 1.1f : 1.0f / 1.1f, mx, my);
		}
		// OpenRA 风格：鼠标到窗口边缘自动滚动（避开顶部/底部 UI）
		{
			const float edge = 24.0f;
			const float scroll = 900.0f * static_cast<float>(dt) / camera.zoom;
			const float uiTop = 36.0f;
			const float uiBottom = 90.0f;
			if (mx >= 0.0f && mx < edge) camera.centerX -= scroll;
			if (mx > static_cast<float>(W) - edge) camera.centerX += scroll;
			if (my >= uiTop && my < uiTop + edge) camera.centerY -= scroll;
			if (my > static_cast<float>(H) - uiBottom - edge && my < static_cast<float>(H) - uiBottom) camera.centerY += scroll;
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
		btnTurret.rect = ui::Rect{556.0f, static_cast<float>(H) - bottomH + 14.0f, 170.0f, 62.0f};
		btnTurret.label = "TURRET (200)";

		// 小地图布局
		const float mmSize = 150.0f;
		const float mmX = static_cast<float>(W) - mmSize - 10.0f;
		const float mmY = static_cast<float>(H) - bottomH - mmSize - 10.0f;

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
		btnTurret.enabled = (credits >= 200);

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
				// 小地图点击：移动相机
				if (mx >= mmX && mx <= mmX + mmSize && my >= mmY && my <= mmY + mmSize) {
					const float fx = (mx - mmX) / mmSize;
					const float fy = (my - mmY) / mmSize;
					const CellCoord target(static_cast<int32_t>(fx * sim.GetWorld().Width()),
						static_cast<int32_t>(fy * sim.GetWorld().Height()));
					const ScreenPos p = ProjectCell(target, 0);
					camera.centerX = static_cast<float>(p.x);
					camera.centerY = static_cast<float>(p.y);
				} else if (btnBuild.Hit(mx, my) && myBuildings == 0) {
					const CellCoord center = ScreenToCell(ScreenPos{static_cast<int32_t>(camera.centerX),
						static_cast<int32_t>(camera.centerY)});
					Command cmd;
					cmd.type = CommandType::Build;
					cmd.playerId = 0;
					cmd.x = center.x * kSubPerCell;
					cmd.y = center.y * kSubPerCell;
					cmd.targetId = 0;
					pushCommand(cmd);
					audio.Play(Sfx::Build);
				} else if (btnTank.Hit(mx, my) && idleBarracks != 0) {
					Command cmd;
					cmd.type = CommandType::Produce;
					cmd.entityId = idleBarracks;
					cmd.targetId = 2;
					pushCommand(cmd);
					audio.Play(Sfx::Produce);
				} else if (btnInfantry.Hit(mx, my) && idleBarracks != 0) {
					Command cmd;
					cmd.type = CommandType::Produce;
					cmd.entityId = idleBarracks;
					cmd.targetId = 1;
					pushCommand(cmd);
					audio.Play(Sfx::Produce);
				} else if (btnTurret.Hit(mx, my)) {
					const CellCoord center = ScreenToCell(ScreenPos{static_cast<int32_t>(camera.centerX),
						static_cast<int32_t>(camera.centerY)});
					Command cmd;
					cmd.type = CommandType::Build;
					cmd.playerId = 0;
					cmd.x = center.x * kSubPerCell;
					cmd.y = center.y * kSubPerCell;
					cmd.targetId = 1; // turret
					pushCommand(cmd);
					audio.Play(Sfx::Build);
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
				if (enemy == 0) {
					markerX = mp.x;
					markerY = mp.y;
					markerTimer = 0.6f;
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
					pushCommand(cmd);
				}
			}
			prevRight = right;
		}

		// ---- 存档 / 读档（F5 / F9）----
		{
			static bool prevF5 = false, prevF9 = false;
			const bool f5 = window.IsKeyDown(VK_F5);
			if (f5 && !prevF5) {
				std::ofstream out("save.rts");
				if (out) {
					out << sim.GetWorld().SaveToText();
					out.close();
					audio.Play(Sfx::Produce);
				}
			}
			prevF5 = f5;
			const bool f9 = window.IsKeyDown(VK_F9);
			if (f9 && !prevF9) {
				std::ifstream in("save.rts");
				if (in) {
					std::stringstream ss;
					ss << in.rdbuf();
					in.close();
					if (sim.GetWorld().LoadFromText(ss.str())) {
						selection.clear();
						audio.Play(Sfx::Build);
					}
				}
			}
			prevF9 = f9;
		}

		// ---- 科技升级（U）----
		{
			static bool prevU = false;
			const bool uDown = window.IsKeyDown('U');
			if (uDown && !prevU && credits >= 300) {
				Command cmd;
				cmd.type = CommandType::Upgrade;
				cmd.playerId = 0;
				pushCommand(cmd);
				audio.Play(Sfx::Build);
			}
			prevU = uDown;
		}

		// ---- 全揭示切换（F2）----
		{
			static bool prevF2 = false;
			const bool f2 = window.IsKeyDown(VK_F2);
			if (f2 && !prevF2) revealAll = !revealAll;
			prevF2 = f2;
		}

		// ---- 后端切换（F1）----
		{
			static bool prevF1 = false;
			const bool f1 = window.IsKeyDown(VK_F1);
			if (f1 && !prevF1) softwareMode = !softwareMode;
			prevF1 = f1;
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
			if (netMode) {
				// M22：联机 —— host 跑战役，双方命令经锁步同步
				if (netHost && !campaign.Finished()) {
					CommandQueue waveQueue;
					campaign.Update(sim.GetWorld(), waveQueue, sim.GetWorld().CurrentTick());
					std::vector<Command> waveCmds;
					waveQueue.Consume(waveCmds);
					for (const auto& wc : waveCmds) localCmds.push_back(wc);
				}
				const auto merged = netSession.Step(sim.GetWorld().CurrentTick(), localCmds);
				localCmds.clear();
				for (const auto& mc : merged) sim.Commands().Push(mc);
				sim.Step();
				accumulator -= tickDt;
				continue;
			}
			// 关卡：生成波次 + 判定胜负
			if (!campaign.Finished()) {
				campaign.Update(sim.GetWorld(), sim.Commands(), sim.GetWorld().CurrentTick());
			}
			if (campaign.Finished()) {
				gameState = GameState::Result;
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
		const RenderSnapshot snapshot = RenderSnapshot::FromWorld(sim.GetWorld(), revealAll);
		// 渲染插值系数（tick 之间的进度 0-1）
		const float interp = (tickDt > 0.0) ? static_cast<float>(accumulator / tickDt) : 1.0f;
		// 地形批次（程序化图集 or 纯色）与单位批次（C&C 图集）
		SpriteBatch terrainBatch, unitBatch;
		terrainBatch.Begin();
		unitBatch.Begin();
		for (const auto& s : snapshot.sprites) {
			const float ix = s.prevX + (s.worldX - s.prevX) * interp;
			const float iy = s.prevY + (s.worldY - s.prevY) * interp;
			if (s.cncSprite >= 0 && cncOk) {
				float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
				const int team = (s.player == 1) ? 1 : 0;
				CncAtlas::GetUV(static_cast<CncSpriteId>(s.cncSprite), team, s.facing, s.animFrame, u0, v0, u1, v1);
				uint32_t tint = s.color;
				if (softwareMode && s.player != 255) {
					tint = (s.player == 0) ? 0xFF00C8FF : 0xFFFF4040;
				}
				unitBatch.AddSprite(s.worldX, s.worldY, s.w, s.h, u0, v0, u1, v1, tint);
			} else if (!softwareMode && s.spriteIndex >= 0) {
				// 地形：程序化 64×32 纹理（1:1）
				float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
				TerrainGfx::GetUVAt(s.spriteIndex, u0, v0, u1, v1);
				terrainBatch.AddSprite(s.worldX, s.worldY, s.w, s.h, u0, v0, u1, v1, s.color);
			} else {
				uint32_t color = s.color;
				if (s.spriteIndex >= 0 && s.color == 0xFFFFFFFF) color = spriteRepColor(s.spriteIndex);
				terrainBatch.AddSolid(s.worldX, s.worldY, s.w, s.h, color);
			}
		}
		// 选中标记（OpenRA SelectionDecorations 风格：四角 L 形框 + 分级血条）
		for (EntityId id : selection) {
			const Actor* a = sim.GetWorld().Entities().Get(id);
			if (a == nullptr || !a->alive) continue;
			const ScreenPos p = ProjectCell(a->pos.ToCell(), 0);
			const float px = static_cast<float>(p.x);
			const float py = static_cast<float>(p.y);
			const float half = 16.0f;
			const float len = 8.0f;
			const float th = 2.0f;
			const uint32_t selCol = 0xFF40FFFFu; // 亮青色（对比草地）
			// 四角 L 形
			terrainBatch.AddSolid(px - half, py - half, len, th, selCol);
			terrainBatch.AddSolid(px - half, py - half, th, len, selCol);
			terrainBatch.AddSolid(px + half - len, py - half, len, th, selCol);
			terrainBatch.AddSolid(px + half - th, py - half, th, len, selCol);
			terrainBatch.AddSolid(px - half, py + half - th, len, th, selCol);
			terrainBatch.AddSolid(px - half, py + half - len, th, len, selCol);
			terrainBatch.AddSolid(px + half - len, py + half - th, len, th, selCol);
			terrainBatch.AddSolid(px + half - th, py + half - len, th, len, selCol);
			// 分级血条（绿 >60% / 黄 30-60% / 红 <30%）
			if (a->health.maxHp > 0) {
				const float barW = 28.0f;
				const float barH = 4.0f;
				const float bx = px - barW * 0.5f;
				const float by = py - 22.0f;
				const float ratio = static_cast<float>(a->health.hp) / static_cast<float>(a->health.maxHp);
				uint32_t hpCol = 0xFF40E040u;
				if (ratio < 0.3f) hpCol = 0xFFE04040u;
				else if (ratio < 0.6f) hpCol = 0xFFE0C040u;
				terrainBatch.AddSolid(bx - 1.0f, by - 1.0f, barW + 2.0f, barH + 2.0f, 0xFF101010);
				terrainBatch.AddSolid(bx, by, barW * ratio, barH, hpCol);
			}
		}
		terrainBatch.End();
		unitBatch.End();

		// ---- VFX：曳光 / 命中 / 死亡爆炸 ----
		std::vector<SpriteVertex> fxVerts;
		for (const auto& e : snapshot.effects) {
			if (e.kind == 0) {
				for (int i = 1; i <= 6; ++i) {
					const float t = static_cast<float>(i) / 7.0f;
					const float wx = e.fromX + (e.toX - e.fromX) * t;
					const float wy = e.fromY + (e.toY - e.fromY) * t;
					float sx = 0, sy = 0;
					camera.WorldToScreen(wx, wy, sx, sy);
					ui::BuildPanel(fxVerts, ui::Rect{sx - 2, sy - 2, 4, 4}, 0xFFFFC040u);
				}
			} else if (e.kind == 1) {
				float sx = 0, sy = 0;
				camera.WorldToScreen(e.toX, e.toY, sx, sy);
				ui::BuildPanel(fxVerts, ui::Rect{sx - 4, sy - 1, 8, 3}, 0xFFFFE080u);
				ui::BuildPanel(fxVerts, ui::Rect{sx - 1, sy - 4, 3, 8}, 0xFFFFE080u);
			} else {
				// 死亡爆炸（三阶段：闪 → 火球 → 烟）
				float sx = 0, sy = 0;
				camera.WorldToScreen(e.fromX, e.fromY, sx, sy);
				const int age = e.age;
				if (age <= 4) {
					const float r = 3.0f + static_cast<float>(age) * 2.0f;
					const uint8_t a = static_cast<uint8_t>(255 - age * 40);
					ui::BuildPanel(fxVerts, ui::Rect{sx - r, sy - r * 0.5f, r * 2, r}, (static_cast<uint32_t>(a) << 24) | 0xFFFFE0u);
				} else if (age <= 16) {
					const float r = 8.0f + static_cast<float>(age - 4) * 1.5f;
					const uint8_t a = static_cast<uint8_t>(255 - (age - 4) * 10);
					ui::BuildPanel(fxVerts, ui::Rect{sx - r * 0.5f, sy - r * 0.25f, r, r * 0.5f}, (static_cast<uint32_t>(a) << 24) | 0xFF6020u);
					for (int i = 0; i < 8; ++i) {
						const float ang = static_cast<float>(i) * 0.7853982f;
						const float px = sx + r * std::cos(ang);
						const float py = sy + r * std::sin(ang) * 0.5f;
						ui::BuildPanel(fxVerts, ui::Rect{px - 2, py - 2, 4, 4}, (static_cast<uint32_t>(a) << 24) | 0xFFA040u);
					}
				} else {
					const float r = 14.0f + static_cast<float>(age - 16) * 2.0f;
					const uint8_t a = static_cast<uint8_t>(std::max(0, 200 - (age - 16) * 15));
					for (int i = 0; i < 6; ++i) {
						const float ang = static_cast<float>(i) * 1.0471976f;
						const float px = sx + r * std::cos(ang);
						const float py = sy + r * std::sin(ang) * 0.5f - static_cast<float>(age - 16) * 0.6f;
						ui::BuildPanel(fxVerts, ui::Rect{px - 3, py - 3, 6, 6}, (static_cast<uint32_t>(a) << 24) | 0x707070u);
					}
				}
			}
		}
		if (!fxVerts.empty()) {
			renderer.DrawScreenBatch(fxVerts);
		}

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
		ui::BuildButton(uiVerts, btnTurret, btnTurret.enabled ? btnFill : btnFillDisabled, btnBorder);

		// 小地图
		{
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

		// M14：世界渲染（GL 或软件后端）
		if (softwareMode) {
			const auto t0 = std::chrono::steady_clock::now();
			swRenderer.Resize(W, H);
			swRenderer.Clear(0xFF101018);
			for (const auto& s : snapshot.sprites) {
				const float ix = s.prevX + (s.worldX - s.prevX) * interp;
				const float iy = s.prevY + (s.worldY - s.prevY) * interp;
				float sx = 0.0f, sy = 0.0f;
				camera.WorldToScreen(ix + s.w * 0.5f, iy + s.h * 0.5f, sx, sy);
				uint32_t color = s.color;
				if (s.spriteIndex >= 0 && s.color == 0xFFFFFFFF) color = spriteRepColor(s.spriteIndex);
				swRenderer.FillRect(sx - s.w * 0.5f, sy - s.h * 0.5f, s.w, s.h, color);
			}
			swRenderer.SyncRgba();
			if (swTex == 0) {
				swTex = renderer.UploadTexture(swRenderer.Rgba(), W, H);
			} else {
				renderer.UpdateTexture(swTex, swRenderer.Rgba(), W, H);
			}
			const auto t1 = std::chrono::steady_clock::now();
			swFrameMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

			renderer.BeginFrame(W, H);
			std::vector<SpriteVertex> worldQuad(6);
			BuildQuad(0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H),
				0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, worldQuad.data());
			renderer.DrawScreenTextured(worldQuad, swTex);
		} else {
			renderer.BeginFrame(W, H);
			// 地形用程序化纹理，单位用 C&C 图集
			renderer.DrawTexturedBatch(terrainBatch.Vertices(), camera, terrainTex);
			if (cncOk) {
				renderer.DrawTexturedBatch(unitBatch.Vertices(), camera, cncTex);
			}
		}
		renderer.DrawScreenBatch(uiVerts);

		// 文字
		std::vector<SpriteVertex> textVerts;
		{
			char line[200];
			if (softwareMode) {
				std::snprintf(line, sizeof(line), "CREDITS %d  UNITS %d  BUILD %d  ENEMY %d  FPS %.0f  BACKEND SOFT %.1fms",
					credits, myUnits, myBuildings, enemyUnits, fps, swFrameMs);
			} else {
				std::snprintf(line, sizeof(line), "CREDITS %d  UNITS %d  BUILD %d  ENEMY %d  FPS %.0f  BACKEND GL",
					credits, myUnits, myBuildings, enemyUnits, fps);
			}
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

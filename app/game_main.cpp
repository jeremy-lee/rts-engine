// RTS Engine — app/game_main.cpp
// M10 可玩 Demo：玩家（P0）vs AI（P1），完整 HUD + 命令面板。
// 操作：
//   左键点选/拖拽框选己方单位；右键移动/攻击
//   底部命令面板按钮：建造兵营 / 生产步兵 / 生产坦克
//   1/2/3 按类型全选；WASD/方向键平移；滚轮缩放
#include <cstdio>
#include <filesystem>
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
#include "render/screenshot.h"
#include "render/tileset.h"
#include "render/render_graph.h"
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

// P1-4：外部地图（.oramap 由 oramap_import 转出的 <base>.bin + <base>_meta.txt，或我们的 .map）
// 一旦给了路径就用它开局，而不是 GenerateTestMap。
std::string g_rulesDir;        // OpenRA 的 rules 目录（可选，给了就用 ActorInfo 填实体）
std::string g_mapBinaryPath;   // 已转换好的二进制地图
std::string g_mapMetaPath;     // 配套的 _meta.txt（可选）

bool LoadExternalMap(Map& out) {
	if (g_mapBinaryPath.empty()) return false;
	Map loaded;
	if (!loaded.LoadBinary(g_mapBinaryPath)) {
		std::fprintf(stderr, "[game] failed to load map: %s\n", g_mapBinaryPath.c_str());
		return false;
	}
	if (!g_mapMetaPath.empty()) {
		if (!loaded.LoadMeta(g_mapMetaPath)) {
			std::fprintf(stderr, "[game] failed to load meta: %s (continuing)\n", g_mapMetaPath.c_str());
		}
	}
	out = std::move(loaded);
	return true;
}

Map MakeMap() {
	if (!g_mapBinaryPath.empty()) {
		Map loaded;
		if (LoadExternalMap(loaded)) return loaded;
	}
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
	// --autoshot：跑若干帧后用 glReadPixels 存一张 BMP（in-game 截图，避开外部截屏的 DPI 干扰）
	bool autoShot = false;
	// --map <path>：用外部地图开局。支持 .oramap（内部调用 rts_oramap_import 转换）、
	// 已转换的 .bin（配同名 _meta.txt）以及我们自己的 .map。
	std::string mapArg;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--autoshot") autoShot = true;
		else if (a == "--map" && i + 1 < argc) { mapArg = argv[++i]; }
		else if (a == "--rules" && i + 1 < argc) { g_rulesDir = argv[++i]; }
		else if (!a.empty() && a[0] != '-' && mapArg.empty()) mapArg = a;   // 位置参数也当作地图
	}
	if (!mapArg.empty()) {
		std::filesystem::path mp(mapArg);
		const std::string ext = mp.extension().string();
		std::string base;
		if (ext == ".oramap" || std::filesystem::is_directory(mp)) {
			// 交给 rts_oramap_import 转成 <base>.bin + <base>_meta.txt
			base = (std::filesystem::temp_directory_path() / ("rts_import_" + mp.stem().string())).string();
			// 用 exe 同目录的导入器，避免依赖 PATH（SetWorkingDirToExe 只改了 cwd）
			char exeBuf[MAX_PATH] = {};
			GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
			std::string exeDir(exeBuf);
			const size_t slash = exeDir.find_last_of("\\/");
			if (slash != std::string::npos) exeDir = exeDir.substr(0, slash + 1);
			const std::string importer = exeDir + "rts_oramap_import.exe";
			const std::string cmd = "\"" + importer + "\" \"" + mp.string() + "\" \"" + base + "\"";

			// 用 CreateProcessA 而不是 std::system：system 会经由 cmd.exe 中转，
			// 在资源重定向/引号场景下返回码不可靠，这里要的是子进程真实退出码。
			int rc = -1;
			{
				std::string cmdline = cmd;   // CreateProcess 需要可写缓冲
				STARTUPINFOA si = {};
				si.cb = sizeof(si);
				PROCESS_INFORMATION pi = {};
				const BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
					0, nullptr, nullptr, &si, &pi);
				if (ok) {
					WaitForSingleObject(pi.hProcess, 30000);
					DWORD exitCode = 1;
					GetExitCodeProcess(pi.hProcess, &exitCode);
					rc = static_cast<int>(exitCode);
					CloseHandle(pi.hProcess);
					CloseHandle(pi.hThread);
				}
			}

			if (rc == 0) {
				// rts_oramap_import 的输出：<base>.map + <base>_meta.txt (+ <base>_actors.txt)
				g_mapBinaryPath = base + ".map";
				g_mapMetaPath = base + "_meta.txt";
				std::printf("[game] imported %s -> %s\n", mp.string().c_str(), g_mapBinaryPath.c_str());
			} else {
				std::fprintf(stderr, "[game] oramap import failed (rc=%d) for %s\n", rc, mp.string().c_str());
			}
		} else if (ext == ".bin" || ext == ".map") {
			base = (mp.parent_path() / mp.stem()).string();
			g_mapBinaryPath = mp.string();
			const std::string meta = base + "_meta.txt";
			if (std::filesystem::exists(meta)) g_mapMetaPath = meta;
		}
		if (!g_mapBinaryPath.empty()) {
			std::printf("[game] map: %s%s\n", g_mapBinaryPath.c_str(),
				g_mapMetaPath.empty() ? "" : " (+meta)");
		}
	}
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
	// 多套 tileset：全部载入，按地图的 TilesetName 切换（缺省 TEMPERAT）
	TileSet::LoadSet("TEMPERAT", "assets/tileset_TEMPERAT.txt");
	TileSet::LoadSet("SNOW", "assets/tileset_SNOW.txt");
	TileSet::LoadSet("INTERIOR", "assets/tileset_INTERIOR.txt");
	TileSet::UseSet("TEMPERAT");
	if (!TerrainGfx::Load(TileSet::CurrentBitmapPath(), terrPixels, terrW, terrH)) {
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

	// P1-5：如果给了 --rules，加载 OpenRA 的 ActorInfo 并交给 World，
	// 之后 Spawn 会优先用 TraitInfo（血量/速度/FireDelay）而不是硬编码 UnitDef。
	Ruleset actorRules;
	if (!g_rulesDir.empty()) {
		std::string err;
		if (actorRules.LoadDirectory(g_rulesDir.c_str(), &err)) {
			std::printf("[game] actorRules: %zu actors (%zu concrete)\n",
				actorRules.actors.size(), actorRules.ConcreteActorCount());
		} else {
			std::fprintf(stderr, "[game] rules load failed: %s\n", err.c_str());
		}
	}

	Simulation sim(MakeMap(), 0x2024, kRules);
	if (!actorRules.Empty()) sim.GetWorld().SetActorRules(&actorRules);
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
		{
			// 起始基地：一个兵营（buildingId 1 = barracks）。没有它 BUILD 恒为 0，
			// 玩家也没有能下 Produce 命令的建筑（Spawn 只能造单位，建筑要走 Build）。
			Command c;
			c.type = CommandType::Build;
			c.playerId = 0;
			c.x = 57 * kSubPerCell;
			c.y = 57 * kSubPerCell;
			c.targetId = 1;
			pushCommand(c);

			// AI 也给它一个兵营，避免开局就被玩家单方面碾压
			c.playerId = 1;
			c.x = 78 * kSubPerCell;
			c.y = 78 * kSubPerCell;
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
	bool revealAll = false;   // P2-7：默认按玩家视野计算迷雾，F2 可切换全揭示（调试）
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
	// 建筑放置模式：点 BUILD 进入，左键落点确认，右键取消。
	bool placingBuilding = false;
	int32_t placingBuildingId = 0;   // 待建造的 building numericId
	CellCoord lastPlacedCell(-1, -1); // 拖拽连续放置时避免同格重复建
	int prevNum[3] = {0, 0, 0};
	int prevOwnUnits = 0;   // 上一帧的己方单位数（用于检测死亡）

	auto last = std::chrono::steady_clock::now();
	double accumulator = 0.0;
	constexpr double tickDt = 1.0 / 30.0;
	double fpsTimer = 0.0;
	int fpsFrames = 0;
	double fps = 0.0;

	// 直接开局：跳过 LevelSelect，让 rts_game 一启动就能打（否则玩家没单位，只能看地形）。
	if (!netMode && !levelPaths.empty()) startLevel(0);

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
			// OpenRA Viewport.ClampToBounds：限制相机在地图内。
		// 注意等距投影：世界 X = (x-y)*kStepX ∈ [-mh*12, mw*12]，Y = (x+y+1)*kStepY ∈ [0,(mw+mh)*12]。
		// 之前传的是正交公式 (0,0,mw*32,mh*32)，既用错了 tile 尺寸也没有负半轴，夹不住。
			const int32_t mw = sim.GetWorld().Width();
			const int32_t mh = sim.GetWorld().Height();
			// 正交网格：世界范围 = [0, w*32] × [0, h*32]
			camera.ClampToBounds(-static_cast<float>(mh) * static_cast<float>(kStepX), 0.0f,
				static_cast<float>(mw) * static_cast<float>(kTileW),
				static_cast<float>(mh) * static_cast<float>(kTileH));
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
		// 侧栏（OpenRA 风格）：右侧竖排，从下往上依次是建造图标、生产队列、资金/电力。
		const float sidebarW = 210.0f;
		const float sideX = static_cast<float>(W) - sidebarW + 12.0f;
		const float sideW = sidebarW - 24.0f;
		const float sideBottom = static_cast<float>(H) - bottomH - 12.0f;
		const float btnH = 54.0f;
		const float btnGap = 8.0f;
		btnBuild.rect = ui::Rect{sideX, sideBottom - btnH, sideW, btnH};
		btnBuild.label = "BUILD (300)";
		btnInfantry.rect = ui::Rect{sideX, sideBottom - 2.0f * (btnH + btnGap), sideW, btnH};
		btnInfantry.label = "INFANTRY (50)";
		btnTank.rect = ui::Rect{sideX, sideBottom - 3.0f * (btnH + btnGap), sideW, btnH};
		btnTank.label = "TANK (150)";
		btnTurret.rect = ui::Rect{sideX, sideBottom - 4.0f * (btnH + btnGap), sideW, btnH};
		btnTurret.label = "TURRET (200)";

		// 小地图挪到左下，避开右侧侧栏
		const float mmSize = 150.0f;
		const float mmX = 10.0f;
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
				std::fprintf(stderr, "[input] click-select: best=%u -> selection=%zu\n", best, selection.size());
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

		// 左键按下：UI 按钮 or 放置 or 框选
		// 注意顺序：侧栏按钮必须最先判定。按钮的 y 落在游戏区范围内，
		// 如果放在"顶部栏之外"的分支里会被框选分支吞掉（侧栏改版后踩过这个坑）。
		if (left && !prevLeft) {
			const bool inSidebar = (mx >= sideX - 12.0f);
			const bool inMinimap = (mx >= mmX && mx <= mmX + mmSize && my >= mmY && my <= mmY + mmSize);

			if (inMinimap && !placingBuilding) {
				const float fx = (mx - mmX) / mmSize;
				const float fy = (my - mmY) / mmSize;
				const CellCoord target(static_cast<int32_t>(fx * sim.GetWorld().Width()),
					static_cast<int32_t>(fy * sim.GetWorld().Height()));
				const ScreenPos p = ProjectCell(target, 0);
				camera.centerX = static_cast<float>(p.x);
				camera.centerY = static_cast<float>(p.y);
			} else if (inSidebar && btnBuild.Hit(mx, my)) {
				// 进入放置模式（不再要求 myBuildings==0，开局有兵营也应该能继续建）
				placingBuilding = true;
				placingBuildingId = 0;
				lastPlacedCell = CellCoord(-1, -1);
				audio.Play(Sfx::Click);
			} else if (inSidebar && btnTurret.Hit(mx, my)) {
				placingBuilding = true;
				placingBuildingId = 1;
				lastPlacedCell = CellCoord(-1, -1);
				audio.Play(Sfx::Click);
			} else if (inSidebar && btnTank.Hit(mx, my) && idleBarracks != 0) {
				Command cmd;
				cmd.type = CommandType::Produce;
				cmd.entityId = idleBarracks;
				cmd.targetId = 2;
				pushCommand(cmd);
				audio.Play(Sfx::Produce);
			} else if (inSidebar && btnInfantry.Hit(mx, my) && idleBarracks != 0) {
				Command cmd;
				cmd.type = CommandType::Produce;
				cmd.entityId = idleBarracks;
				cmd.targetId = 1;
				pushCommand(cmd);
				audio.Play(Sfx::Produce);
			} else if (placingBuilding && !inSidebar &&
				my >= topBarH && my <= static_cast<float>(H) - bottomH) {
				// 放置模式：点击处就是建筑落点
				WorldPoint mp;
				camera.ScreenToWorld(mx, my, mp.x, mp.y);
				const CellCoord cell = ScreenToCell(ScreenPos{static_cast<int32_t>(mp.x), static_cast<int32_t>(mp.y)});
				Command cmd;
				cmd.type = CommandType::Build;
				cmd.playerId = 0;
				cmd.x = cell.x * kSubPerCell;
				cmd.y = cell.y * kSubPerCell;
				cmd.targetId = placingBuildingId;
				pushCommand(cmd);
				audio.Play(Sfx::Build);
				lastPlacedCell = cell;
			} else if (my >= topBarH && my <= static_cast<float>(H) - bottomH && !inSidebar) {
				dragStartX = window.MouseX();
				dragStartY = window.MouseY();
				dragging = true;
			}
		}
		// 拖拽连续放置：按住左键划过新格子就落一个
		if (placingBuilding && left && prevLeft && mx < sideX - 12.0f &&
			my >= topBarH && my <= static_cast<float>(H) - bottomH) {
			WorldPoint mp;
			camera.ScreenToWorld(mx, my, mp.x, mp.y);
			const CellCoord cell = ScreenToCell(ScreenPos{static_cast<int32_t>(mp.x), static_cast<int32_t>(mp.y)});
			if (cell.x != lastPlacedCell.x || cell.y != lastPlacedCell.y) {
				Command cmd;
				cmd.type = CommandType::Build;
				cmd.playerId = 0;
				cmd.x = cell.x * kSubPerCell;
				cmd.y = cell.y * kSubPerCell;
				cmd.targetId = placingBuildingId;
				pushCommand(cmd);
				audio.Play(Sfx::Build);
				lastPlacedCell = cell;
			}
		}
		prevLeft = left;
		// ---- 右键：移动/攻击 ----
		{
			static bool prevRight = false;
			const bool right = window.IsMouseDown(1);
			if (right && !prevRight && placingBuilding) {
				// 右键取消放置模式
				placingBuilding = false;
				audio.Play(Sfx::Click);
			} else if (right && !prevRight && !selection.empty()) {
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
				std::fprintf(stderr, "[input] right-click: selection=%zu enemy=%u\n", selection.size(), enemy);
				for (EntityId id : selection) {
					Command cmd;
					cmd.playerId = 0;
					if (enemy != 0) {
						cmd.type = CommandType::Attack;
						cmd.entityId = id;
						cmd.targetId = enemy;
						audio.Play(Sfx::Attack);
					} else {
						cmd.type = CommandType::Move;
						cmd.entityId = id;
						// 正交：屏幕像素 → 子格坐标（每格 kTileW/kTileH 像素）
						cmd.x = static_cast<int32_t>(mp.x) * kSubPerCell / kTileW;
						cmd.y = static_cast<int32_t>(mp.y) * kSubPerCell / kTileH;
						std::fprintf(stderr, "[input]   move unit=%u -> cell(%d,%d)\n", id,
							cmd.x / kSubPerCell, cmd.y / kSubPerCell);
						audio.Play(Sfx::Move);
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
				TileSet::GetUV(s.spriteIndex >> 8, s.spriteIndex & 0xFF, u0, v0, u1, v1);
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
		// ---- 建筑放置预览（P0-2）----
		// 跟随鼠标画出待建建筑的 Footprint：每个格子一个半透明方块，
		// 全部可建为绿色，任意一格非法为红色。'_' 格不画。
		if (placingBuilding) {
			WorldPoint mp;
			camera.ScreenToWorld(mx, my, mp.x, mp.y);
			const CellCoord origin = ScreenToCell(ScreenPos{static_cast<int32_t>(mp.x), static_cast<int32_t>(mp.y)});
			const BuildingDef* def = nullptr;
			if (placingBuildingId >= 0 &&
				static_cast<size_t>(placingBuildingId) < sim.GetWorld().GetRules().buildingIds.size()) {
				def = sim.GetWorld().GetRules().FindBuilding(
					sim.GetWorld().GetRules().buildingIds[static_cast<size_t>(placingBuildingId)]);
			}
			const int32_t fw = (def != nullptr && def->width > 0) ? def->width : 2;
			const int32_t fh = (def != nullptr && def->height > 0) ? def->height : 2;
			const char* mask = (def != nullptr && !def->footprint.empty()) ? def->footprint.data() : nullptr;
			const bool ok = sim.GetWorld().GetMap().CanBuildAt(origin, fw, fh, mask);
			const uint32_t fill = ok ? 0x6040E040u : 0x60E04040u;   // 绿 / 红（半透明）
			for (int32_t y = 0; y < fh; ++y) {
				for (int32_t x = 0; x < fw; ++x) {
					if (mask != nullptr &&
						mask[static_cast<size_t>(y) * static_cast<size_t>(fw) + static_cast<size_t>(x)] == '_') continue;
					const ScreenPos p = ProjectCell(CellCoord(origin.x + x, origin.y + y), 0);
					terrainBatch.AddSolid(static_cast<float>(p.x) - static_cast<float>(kTileW) * 0.5f,
						static_cast<float>(p.y) - static_cast<float>(kTileH) * 0.5f,
						static_cast<float>(kTileW), static_cast<float>(kTileH), fill);
				}
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

		// 框选矩形：拉框过程中把选择框画出来（OpenRA 的选择框是半透明白底 + 亮边）。
		// 之前 dragging 只记录了起点，从来没有把这个框画出来。
		if (dragging && !placingBuilding) {
			const float bx0 = static_cast<float>(std::min(dragStartX, window.MouseX()));
			const float by0 = static_cast<float>(std::min(dragStartY, window.MouseY()));
			const float bx1 = static_cast<float>(std::max(dragStartX, window.MouseX()));
			const float by1 = static_cast<float>(std::max(dragStartY, window.MouseY()));
			const float bw = bx1 - bx0;
			const float bh = by1 - by0;
			if (bw > 0.5f && bh > 0.5f) {
				ui::BuildPanel(uiVerts, ui::Rect{bx0, by0, bw, bh}, 0x30FFFFFF);  // 半透明填充
				const uint32_t edge = 0xE0FFFFFF;                                   // 亮边框
				const float t = 1.5f;
				ui::BuildPanel(uiVerts, ui::Rect{bx0, by0, bw, t}, edge);           // 上
				ui::BuildPanel(uiVerts, ui::Rect{bx0, by1 - t, bw, t}, edge);       // 下
				ui::BuildPanel(uiVerts, ui::Rect{bx0, by0, t, bh}, edge);           // 左
				ui::BuildPanel(uiVerts, ui::Rect{bx1 - t, by0, t, bh}, edge);       // 右
			}
		}

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
		// 帧图（UE5 RDG 风格）：World → UI → Text
		RenderGraph graph;
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
			graph.AddPass("World", [&](PassContext& ctx) {
				std::vector<SpriteVertex> worldQuad(6);
				BuildQuad(0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H),
					0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, worldQuad.data());
				renderer.DrawScreenTextured(worldQuad, swTex);
				ctx.stats.drawCalls = 1;
				ctx.stats.vertices = 6;
			});
		} else {
			renderer.BeginFrame(W, H);
			graph.AddPass("World", [&](PassContext& ctx) {
				renderer.DrawTexturedBatch(terrainBatch.Vertices(), camera, terrainTex); // 地形用混合：菱形 tile 的透明角要透出下层
				ctx.stats.drawCalls = 1;
				ctx.stats.vertices = static_cast<uint32_t>(terrainBatch.Vertices().size());
				if (cncOk) {
					renderer.DrawTexturedBatch(unitBatch.Vertices(), camera, cncTex);
					ctx.stats.drawCalls += 1;
					ctx.stats.vertices += static_cast<uint32_t>(unitBatch.Vertices().size());
				}
			});
		}

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

			// P2-6：己方单位数减少时播死亡音
			if (myUnits < prevOwnUnits) audio.Play(Sfx::Death);
			prevOwnUnits = myUnits;

			text::Build(textVerts, btnBuild.label,
				btnBuild.rect.x + 10.0f, btnBuild.rect.y + 18.0f, 1.6f,
				btnBuild.enabled ? 0xFFFFFFFF : 0xFF888888);
			text::Build(textVerts, btnInfantry.label,
				btnInfantry.rect.x + 10.0f, btnInfantry.rect.y + 18.0f, 1.6f,
				btnInfantry.enabled ? 0xFFFFFFFF : 0xFF888888);
			text::Build(textVerts, btnTank.label,
				btnTank.rect.x + 10.0f, btnTank.rect.y + 18.0f, 1.6f,
				btnTank.enabled ? 0xFFFFFFFF : 0xFF888888);
			text::Build(textVerts, btnTurret.label,
				btnTurret.rect.x + 10.0f, btnTurret.rect.y + 18.0f, 1.6f,
				btnTurret.enabled ? 0xFFFFFFFF : 0xFF888888);

			char info[160];
			if (placingBuilding) {
				// 放置模式下给个明确提示，否则玩家会以为点地图没反应
				std::snprintf(info, sizeof(info), "PLACING - RMB CANCEL");
			} else {
				std::snprintf(info, sizeof(info), "SELECTED %zu", selection.size());
			}
			text::Build(textVerts, info, sideX + 4.0f, btnTurret.rect.y - 34.0f, 1.5f, 0xFFC0C0C0);

			// 生产进度
			const Actor* producing = nullptr;
			for (const auto& a : sim.GetWorld().Entities().All()) {
				if (!a.alive || a.playerId != 0 || !a.Has(kCompBuilding)) continue;
				if (a.producer.queued > 0) { producing = &a; break; }
			}
			if (producing != nullptr && producing->producer.total > 0) {
				const float px = sideX + 4.0f;
				const float py = btnTurret.rect.y - 18.0f;
				const float ratio = static_cast<float>(producing->producer.total - producing->producer.remaining)
					/ static_cast<float>(producing->producer.total);
				ui::BuildPanel(uiVerts, ui::Rect{px, py, 140.0f, 14.0f}, 0xFF202020);
				ui::BuildPanel(uiVerts, ui::Rect{px, py, 140.0f * ratio, 14.0f}, 0xFF40A0FF);
				char prodText[64];
				std::snprintf(prodText, sizeof(prodText), "PRODUCING %d%%", static_cast<int>(ratio * 100.0f));
				text::Build(textVerts, prodText, px, py + 18.0f, 1.2f, 0xFFA0C0FF);
			}
		}
		graph.AddPass("UI", [&](PassContext& ctx) {

			renderer.DrawScreenBatch(uiVerts);
			ctx.stats.drawCalls = 1;
			ctx.stats.vertices = static_cast<uint32_t>(uiVerts.size());
		});
		graph.AddPass("Text", [&](PassContext& ctx) {
			renderer.DrawScreenTextured(textVerts, fontTex);
			ctx.stats.drawCalls = 1;
			ctx.stats.vertices = static_cast<uint32_t>(textVerts.size());
		});
		graph.Execute(W, H, dt);

		// ---- 帧图统计 HUD（F3，对应 UE5 的 stat rhi）----
		{
			static bool showStats = false;
			static bool prevF3 = false;
			const bool f3 = window.IsKeyDown(VK_F3) != 0;
			if (f3 && !prevF3) showStats = !showStats;
			prevF3 = f3;
			if (showStats) {
				const auto& stats = graph.Stats();
				const float panelH = 26.0f + 16.0f * static_cast<float>(stats.size());
				std::vector<SpriteVertex> panelVerts;
				ui::BuildPanel(panelVerts, ui::Rect{8.0f, 44.0f, 400.0f, panelH}, 0xD0101010);
				renderer.DrawScreenBatch(panelVerts);

				std::vector<SpriteVertex> statsVerts;
				char buf[192];
				std::snprintf(buf, sizeof(buf), "FRAME %.2f ms   %zu passes   F3 toggle",
					graph.Totals().cpuMs, stats.size());
				text::Build(statsVerts, buf, 14.0f, 50.0f, 1.1f, 0xFFFFD060);
				float y = 66.0f;
				for (const auto& r : stats) {
					std::snprintf(buf, sizeof(buf), "%-6s %6.2fms  dc %-3u  vtx %-6u",
						r.name.c_str(), r.stats.cpuMs, r.stats.drawCalls, r.stats.vertices);
					text::Build(statsVerts, buf, 14.0f, y, 1.0f, 0xFFA0E0A0);
					y += 16.0f;
				}
				renderer.DrawScreenTextured(statsVerts, fontTex);
			}
		}
		renderer.EndFrame();

		if (autoShot) {
			static int shotFrames = 0;
			if (++shotFrames == 90) { // 给采矿/AI 一点时间，画面稳定后再截
				CaptureScreenshot(W, H, "game_shot.bmp");
				autoShot = false;
			}
		}

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

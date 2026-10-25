// RTS Engine — tools/autotest_visual.cpp
// 可视化 AI 自动对战：打开窗口，观看两个 AI 实时对战（像看比赛）。
// 用法：rts_autotest_visual [--seed S] [--ticks MAX]
// 操作：鼠标右键拖拽 = 平移相机；滚轮 = 缩放；R = 重开；ESC = 退出
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>

#include "core/core.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"
#include "sim/map.h"
#include "render/snapshot.h"
#include "render/sprite_batch.h"
#include "render/camera.h"
#include "render/gl_renderer.h"
#include "render/font.h"
#include "render/text.h"
#include "render/ui.h"
#include "render/cnc_assets.h"
#include "render/screenshot.h"
#include "platform/win32_window.h"
#include <GL/gl.h>

using namespace rts;

namespace {

const Rules kRules = Rules::FromYAML(R"(
upgrade:
  cost: 300
  attack: 5
  hp: 50
  speed: 2
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
	Map map(128, 128, 0, kWalkable | kBuildable);
	map.SetResourceAmount(CellCoord(20, 20), 12000);
	map.SetResourceAmount(CellCoord(108, 108), 12000);
	return map;
}

struct VisualState {
	Simulation sim;
	int kills0 = 0, kills1 = 0;
	uint64_t seed = 101;

	VisualState() : sim(Map(4, 4), 1, Rules()) {}
};

// 初始化一局（双 AI + 开局单位）
VisualState StartMatch(uint64_t seed) {
	VisualState vs;
	vs.seed = seed;
	vs.sim = Simulation(MakeMap(), seed, kRules);
	vs.sim.AddAI(0, seed + 1);
	vs.sim.AddAI(1, seed + 2);
	vs.sim.GetWorld().AddCredits(0, 2000);
	vs.sim.GetWorld().AddCredits(1, 2000);

	auto addUnit = [&](uint8_t player, CellCoord pos) {
		const EntityId id = vs.sim.GetWorld().Entities().Create(player,
			SubCoord(pos.x * kSubPerCell, pos.y * kSubPerCell),
			kCompMovable | kCompHealth | kCompArmament | kCompVision);
		Actor* a = vs.sim.GetWorld().Entities().Get(id);
		a->health.hp = 100;
		a->health.maxHp = 100;
		a->movable.speed = 160; // 5 格/秒
		a->armament.damage = 10;
		a->armament.range = 4096;
		a->armament.cooldown = 15;
		a->vision.range = 8 * kSubPerCell;
	};
	addUnit(0, CellCoord(26, 64));
	addUnit(1, CellCoord(102, 64));
	return vs;
}

} // namespace

// 把工作目录设为「含 assets/ 的目录」（exe 目录或其父目录）
static void SetWorkingDirToExe() {
	char buf[MAX_PATH] = {};
	if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return;
	std::string p(buf);
	const size_t pos = p.find_last_of("\\/");
	if (pos == std::string::npos) return;
	const std::string dir = p.substr(0, pos);
	// 优先父目录（build/ 的上级通常是项目根，含 assets/）
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
	uint64_t seed = 101;
	bool autoshot = false;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--seed" && i + 1 < argc) seed = static_cast<uint64_t>(std::atoi(argv[++i]));
		else if (arg == "--autoshot") autoshot = true;
	}

	Win32Window window;
	if (!window.Create("RTS Engine — AI vs AI AutoTest (visual)", 1280, 720)) return 1;

	GLRenderer renderer;
	if (!renderer.Initialize(window.DeviceContext())) return 1;
	{
		GLint maxTex = 0;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
		std::printf("[gl] GL_MAX_TEXTURE_SIZE = %d\n", maxTex);
		std::fflush(stdout);
	}

	// 字体与图集
	int fontW = 0, fontH = 0;
	const auto fontPixels = BitmapFont::BuildTextureData(fontW, fontH);
	const uint32_t fontTex = renderer.UploadTexture(fontPixels, fontW, fontH);

	int atlasW = 0, atlasH = 0;
	const auto atlasPixels = AssetAtlas::Build(atlasW, atlasH);
	const uint32_t atlasTex = renderer.UploadTexture(atlasPixels, atlasW, atlasH);

	std::vector<uint8_t> cncPixels;
	int cncW = 0, cncH = 0;
	const bool cncOk = CncAtlas::Load("assets/cnc_atlas.png", cncPixels, cncW, cncH);
	const uint32_t cncTex = cncOk ? renderer.UploadTexture(cncPixels, cncW, cncH) : 0;

	// 地形纹理（程序化 64×32）
	int terrW = 0, terrH = 0;
	const auto terrPixels = TerrainGfx::Build(terrW, terrH);
	const uint32_t terrainTex = renderer.UploadTexture(terrPixels, terrW, terrH);

	VisualState vs = StartMatch(seed);

	Camera camera;
	camera.viewW = window.Width();
	camera.viewH = window.Height();
	{
		// 相机对准战场中心（192×192 地图，拉远以看到双方）
		const ScreenPos c = ProjectCell(CellCoord(64, 64), 0);
		camera.centerX = static_cast<float>(c.x);
		camera.centerY = static_cast<float>(c.y);
		camera.zoom = 1.8f; // 放大到每帧位移 >1 像素
	}

	auto last = std::chrono::steady_clock::now();
	double accumulator = 0.0;
	constexpr double tickDt = 1.0 / 30.0;
	double fpsTimer = 0.0;
	int fpsFrames = 0;
	double fps = 0.0;
	bool finished = false;
	int winner = -1;

	while (window.PumpMessages()) {
		const auto now = std::chrono::steady_clock::now();
		double dt = std::chrono::duration<double>(now - last).count();
		last = now;
		if (dt > 0.1) dt = 0.1;

		const int W = window.Width();
		const int H = window.Height();
		const float mx = static_cast<float>(window.MouseX());
		const float my = static_cast<float>(window.MouseY());

		// 相机：跟随 P0 部队（近景，能看到单位动作）+ 右键拖拽 + 滚轮
		{
			float cx = 0, cy = 0;
			int n = 0;
			for (const auto& a : vs.sim.GetWorld().Entities().All()) {
				if (!a.alive || a.playerId != 0) continue;
				const ScreenPos p = ProjectCell(a.pos.ToCell(), 0);
				cx += static_cast<float>(p.x);
				cy += static_cast<float>(p.y);
				++n;
			}
			if (n > 0) {
				cx /= static_cast<float>(n);
				cy /= static_cast<float>(n);
				camera.centerX += (cx - camera.centerX) * 0.08f;
				camera.centerY += (cy - camera.centerY) * 0.08f;
			}
			static int lastX = -1, lastY = -1;
			if (window.IsMouseDown(1)) {
				if (lastX >= 0) camera.PanScreen(static_cast<float>(window.MouseX() - lastX),
					static_cast<float>(window.MouseY() - lastY));
				lastX = window.MouseX();
				lastY = window.MouseY();
			} else {
				lastX = -1;
				lastY = -1;
			}
			const int wheel = window.ConsumeWheel();
			if (wheel != 0) camera.ZoomAt((wheel > 0) ? 1.1f : 1.0f / 1.1f, mx, my);
			camera.viewW = W;
			camera.viewH = H;
			// OpenRA 风格：相机限制在地图世界矩形内
			{
				const int32_t mw = vs.sim.GetWorld().Width();
				const int32_t mh = vs.sim.GetWorld().Height();
				camera.ClampToBounds(static_cast<float>(mw) * -16.0f, 0.0f,
					static_cast<float>(mw) * 16.0f, static_cast<float>(mw + mh) * 16.0f);
			}
		}

		// 重开
		{
			static bool prevR = false;
			const bool r = window.IsKeyDown('R');
			if (r && !prevR) {
				vs = StartMatch(seed);
				finished = false;
				winner = -1;
			}
			prevR = r;
			if (window.IsKeyDown(VK_ESCAPE)) break;
		}

		// 逻辑 tick（AI 自动对战）
		accumulator += dt;
		while (accumulator >= tickDt) {
			if (!finished) {
				vs.sim.Step();
				const Tick tick = vs.sim.GetWorld().CurrentTick();
				int alive0 = 0, alive1 = 0;
				for (const auto& a : vs.sim.GetWorld().Entities().All()) {
					if (!a.alive) continue;
					if (a.playerId == 0) ++alive0;
					else ++alive1;
				}
				if (tick > 120 && (alive0 == 0 || alive1 == 0)) {
					finished = true;
					winner = (alive0 == 0) ? 1 : 0;
				}
				if (tick >= 12000) {
					finished = true;
					winner = (vs.kills0 > vs.kills1) ? 0 : (vs.kills1 > vs.kills0) ? 1 : -1;
				}
			}
			accumulator -= tickDt;
		}

		// 统计击杀（按死亡实体）
		{
			int k0 = 0, k1 = 0;
			for (const auto& a : vs.sim.GetWorld().Entities().All()) {
				if (a.alive) continue;
				if (a.playerId == 0) ++k1;
				else ++k0;
			}
			vs.kills0 = k0;
			vs.kills1 = k1;
		}

		// 渲染
		const RenderSnapshot snapshot = RenderSnapshot::FromWorld(vs.sim.GetWorld(), true);
		{
			static int dbgT = 0;
			if (dbgT < 12) {
				for (const auto& a : vs.sim.GetWorld().Entities().All()) {
					if (a.alive && a.Has(kCompMovable) && a.movable.moving) {
						std::printf("[m] tick=%lld pos=(%d,%d) prev=(%d,%d) facing=%d\n",
							static_cast<long long>(vs.sim.GetWorld().CurrentTick()),
							a.pos.x, a.pos.y, a.movable.prevPos.x, a.movable.prevPos.y,
							static_cast<int>(a.movable.facing));
						fflush(stdout);
						++dbgT;
						break;
					}
				}
			}
		}
		const float interp = (accumulator > 0.0) ? static_cast<float>(accumulator / tickDt) : 1.0f;

		SpriteBatch terrainBatch, unitBatch;
		terrainBatch.Begin();
		unitBatch.Begin();
		{
			static bool dbgUnit = false;
			if (!dbgUnit) {
				for (const auto& s : snapshot.sprites) {
					if (s.cncSprite >= 0) {
						std::printf("[unit] cnc=%d x=%.0f y=%.0f w=%.0f h=%.0f player=%d facing=%d\n",
							s.cncSprite, s.worldX, s.worldY, s.w, s.h, s.player, s.facing);
						fflush(stdout);
					}
				}
				dbgUnit = true;
			}
		}
		for (const auto& s : snapshot.sprites) {
			const float ix = s.prevX + (s.worldX - s.prevX) * interp;
			const float iy = s.prevY + (s.worldY - s.prevY) * interp;
			if (s.cncSprite >= 0 && cncOk) {
				// 单位：RA 图集
				float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
				CncAtlas::GetUV(static_cast<CncSpriteId>(s.cncSprite), (s.player == 1) ? 1 : 0,
					s.facing, s.animFrame, u0, v0, u1, v1);
				unitBatch.AddSprite(ix, iy, s.w, s.h, u0, v0, u1, v1, s.color);
			} else if (s.spriteIndex >= 0) {
				// 地形：程序化 64×32 纹理（1:1）
				float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
				TerrainGfx::GetUV(static_cast<TerrainKind>(s.spriteIndex), u0, v0, u1, v1);
				terrainBatch.AddSprite(s.worldX, s.worldY, s.w, s.h, u0, v0, u1, v1, s.color);
			} else {
				terrainBatch.AddSolid(s.worldX, s.worldY, s.w, s.h, s.color);
			}
		}
		terrainBatch.End();
		unitBatch.End();

		// 攻击特效
		std::vector<SpriteVertex> uiVerts;
		for (const auto& e : snapshot.effects) {
			for (int i = 1; i <= 5; ++i) {
				const float t = static_cast<float>(i) / 6.0f;
				const float wx = e.fromX + (e.toX - e.fromX) * t;
				const float wy = e.fromY + (e.toY - e.fromY) * t;
				float sx = 0, sy = 0;
				camera.WorldToScreen(wx, wy, sx, sy);
				ui::BuildPanel(uiVerts, ui::Rect{sx - 2, sy - 2, 4, 4}, 0xFFFFA020u);
			}
		}
		// 顶部 HUD 条
		ui::BuildPanel(uiVerts, ui::Rect{0, 0, static_cast<float>(W), 40}, 0xE0202020);

		renderer.BeginFrame(W, H);
		// 地形（程序化纹理）+ 单位（RA 图集）
		renderer.DrawTexturedBatch(terrainBatch.Vertices(), camera, terrainTex);
		if (cncOk) {
			renderer.DrawTexturedBatch(unitBatch.Vertices(), camera, cncTex);
		}

		renderer.DrawScreenBatch(uiVerts);

		// 文字
		{
			std::vector<SpriteVertex> tv;
			char line[200];
			int u0 = 0, u1 = 0;
			for (const auto& a : vs.sim.GetWorld().Entities().All()) {
				if (!a.alive) continue;
				if (a.playerId == 0) ++u0;
				else ++u1;
			}
			std::snprintf(line, sizeof(line),
				"AUTOTEST  tick=%lld  P0:%d  P1:%d  kills %d:%d  FPS %.0f  %s",
				static_cast<long long>(vs.sim.GetWorld().CurrentTick()), u0, u1,
				vs.kills0, vs.kills1, fps,
				finished ? (winner == 0 ? "[ P0 WINS ]" : winner == 1 ? "[ P1 WINS ]" : "[ DRAW ]")
					: "[ FIGHTING ]");
			text::Build(tv, line, 12.0f, 10.0f, 1.5f, 0xFFE0E0E0);

			if (finished) {
				const char* result = (winner == 0) ? "P0 WINS!" : (winner == 1) ? "P1 WINS!" : "DRAW";
				const uint32_t col = (winner == 0) ? 0xFF00C8FFu : (winner == 1) ? 0xFFFF4040u : 0xFFFFD040u;
				text::Build(tv, result, static_cast<float>(W) * 0.5f - text::Measure(result, 4.0f) * 0.5f,
					static_cast<float>(H) * 0.4f, 4.0f, col);
				const char* hint = "PRESS R TO RESTART   ESC TO QUIT";
				text::Build(tv, hint, static_cast<float>(W) * 0.5f - text::Measure(hint, 1.6f) * 0.5f,
					static_cast<float>(H) * 0.4f + 60.0f, 1.6f, 0xFFC0C0C0);
			}
			renderer.DrawScreenTextured(tv, fontTex);
		}
		// 自动截图（调试/验证）
		if (autoshot) {
			static int frameCount = 0;
			++frameCount;
			if (frameCount == 90 || frameCount == 300 || frameCount == 600) {
				char shot[64];
				std::snprintf(shot, sizeof(shot), "shot_%04d.bmp", frameCount);
				CaptureScreenshot(W, H, shot);
				std::printf("[shot] %s\n", shot);
				std::fflush(stdout);
			}
		}
		renderer.EndFrame();

		// FPS
		fpsTimer += dt;
		++fpsFrames;
		if (fpsTimer >= 0.5) {
			fps = fpsFrames / fpsTimer;
			fpsTimer = 0.0;
			fpsFrames = 0;
		}
	}

	renderer.Shutdown();
	window.Destroy();
	return 0;
}

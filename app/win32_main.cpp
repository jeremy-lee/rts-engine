// RTS Engine — app/win32_main.cpp
// M6 可视化 demo：AI vs AI 对局（蓝方 P0 vs 红方 P1）
// 逻辑 30Hz 固定步长；渲染可变帧率。相机 WASD/滚轮/右键拖拽。
#include <cstdio>
#include <chrono>

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
	Map map = Map::GenerateTestMap(256, 256, 2024);
	// 两侧矿区
	map.SetResourceAmount(CellCoord(40, 40), 8000);
	map.SetResourceAmount(CellCoord(216, 216), 8000);
	return map;
}

} // namespace

int main() {
	Win32Window window;
	if (!window.Create("RTS Engine M6 — AI vs AI", 1280, 720)) return 1;

	GLRenderer renderer;
	if (!renderer.Initialize(window.DeviceContext())) return 1;

	Simulation sim(MakeMap(), 0xA1B2C3, kRules);
	sim.AddAI(0, 31);
	sim.AddAI(1, 32);
	sim.GetWorld().AddCredits(0, 2000);
	sim.GetWorld().AddCredits(1, 2000);

	Camera camera;
	camera.viewW = window.Width();
	camera.viewH = window.Height();
	{
		const ScreenPos center = ProjectCell(CellCoord(128, 128), 0);
		camera.centerX = static_cast<float>(center.x);
		camera.centerY = static_cast<float>(center.y);
		camera.zoom = 0.8f;
	}

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

		// 相机控制
		const float panSpeed = 500.0f * static_cast<float>(dt) / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= panSpeed;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += panSpeed;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= panSpeed;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += panSpeed;

		{
			static int lastX = -1, lastY = -1;
			if (window.IsMouseDown(1)) {
				const int mx = window.MouseX(), my = window.MouseY();
				if (lastX >= 0) camera.PanScreen(static_cast<float>(mx - lastX), static_cast<float>(my - lastY));
				lastX = mx;
				lastY = my;
			} else {
				lastX = -1;
				lastY = -1;
			}
		}

		const int wheel = window.ConsumeWheel();
		if (wheel != 0) {
			const float factor = (wheel > 0) ? 1.1f : 1.0f / 1.1f;
			camera.ZoomAt(factor, static_cast<float>(window.MouseX()), static_cast<float>(window.MouseY()));
		}
		camera.viewW = window.Width();
		camera.viewH = window.Height();

		// 逻辑 tick
		accumulator += dt;
		while (accumulator >= tickDt) {
			sim.Step();
			accumulator -= tickDt;
		}

		// 渲染
		const RenderSnapshot snapshot = RenderSnapshot::FromWorld(sim.GetWorld());
		SpriteBatch batch;
		batch.Begin();
		for (const auto& s : snapshot.sprites) {
			batch.AddSolid(s.worldX, s.worldY, s.w, s.h, s.color);
		}
		batch.End();

		renderer.BeginFrame(window.Width(), window.Height());
		renderer.DrawBatch(batch.Vertices(), camera);
		renderer.EndFrame();

		// 标题
		fpsTimer += dt;
		++fpsFrames;
		if (fpsTimer >= 0.5) {
			const double fps = fpsFrames / fpsTimer;
			char title[160];
			std::snprintf(title, sizeof(title),
				"RTS M6 AI vs AI | %.0f FPS | %zu sprites | tick %lld | P0:%d P1:%d",
				fps, snapshot.sprites.size(),
				static_cast<long long>(sim.GetWorld().CurrentTick()),
				sim.GetWorld().Credits(0), sim.GetWorld().Credits(1));
			SetWindowTextA(window.Handle(), title);
			fpsTimer = 0.0;
			fpsFrames = 0;
		}
	}

	renderer.Shutdown();
	window.Destroy();
	return 0;
}

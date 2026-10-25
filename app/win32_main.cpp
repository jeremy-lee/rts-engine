// RTS Engine — app/win32_main.cpp
// M3 可视化 demo：256x256 地图 + 100 单位 + 相机控制（WASD/滚轮/右键拖拽）。
// 逻辑 30Hz 固定步长；渲染可变帧率。
#include <cstdio>
#include <chrono>
#include <string>

#include "core/core.h"
#include "sim/simulation.h"
#include "render/snapshot.h"
#include "render/sprite_batch.h"
#include "render/camera.h"
#include "render/gl_renderer.h"
#include "platform/win32_window.h"

using namespace rts;

int main() {
	Win32Window window;
	if (!window.Create("RTS Engine — M3 rendering demo", 1280, 720)) {
		return 1;
	}

	GLRenderer renderer;
	if (!renderer.Initialize(window.DeviceContext())) {
		return 1;
	}

	constexpr int32_t kMapW = 256, kMapH = 256;
	constexpr int32_t kUnits = 100;
	const Map map = Map::GenerateTestMap(kMapW, kMapH, 2024);
	Simulation sim(map, 0xC0FFEE1234ull);

	// 生成 100 单位
	{
		for (int i = 0; i < kUnits; ++i) {
			Command c;
			c.type = CommandType::Spawn;
			c.playerId = static_cast<uint8_t>(i % 2);
			c.x = (40 + (i % 10) * 3) * kSubPerCell;
			c.y = (40 + (i / 10) * 3) * kSubPerCell;
			sim.Commands().Push(c);
		}
		sim.Step();
		// 全部向中心移动
		for (int i = 0; i < kUnits; ++i) {
			Command m;
			m.type = CommandType::Move;
			m.entityId = static_cast<uint32_t>(i + 1);
			m.x = (128 + (i % 6) - 3) * kSubPerCell;
			m.y = (128 + (i / 6) % 6 - 3) * kSubPerCell;
			sim.Commands().Push(m);
		}
	}

	// 相机初始对准地图中心
	Camera camera;
	camera.viewW = window.Width();
	camera.viewH = window.Height();
	{
		const ScreenPos center = ProjectCell(CellCoord(kMapW / 2, kMapH / 2), 0);
		camera.centerX = static_cast<float>(center.x);
		camera.centerY = static_cast<float>(center.y);
		camera.zoom = 0.7f;
	}

	auto last = std::chrono::steady_clock::now();
	double accumulator = 0.0;
	double tickDt = 1.0 / 30.0;
	double fpsTimer = 0.0;
	int fpsFrames = 0;
	double fps = 0.0;

	while (window.PumpMessages()) {
		const auto now = std::chrono::steady_clock::now();
		double dt = std::chrono::duration<double>(now - last).count();
		last = now;
		if (dt > 0.1) dt = 0.1; // 防跳帧

		// ---- 相机控制 ----
		const float panSpeed = 500.0f * static_cast<float>(dt) / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= panSpeed;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += panSpeed;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= panSpeed;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += panSpeed;

		if (window.IsMouseDown(1)) { // 右键拖拽
			static int lastX = -1, lastY = -1;
			const int mx = window.MouseX(), my = window.MouseY();
			if (lastX >= 0) {
				camera.PanScreen(static_cast<float>(mx - lastX), static_cast<float>(my - lastY));
			}
			lastX = mx;
			lastY = my;
			if (!window.IsMouseDown(1)) { lastX = -1; lastY = -1; }
		}

		const int wheel = window.ConsumeWheel();
		if (wheel != 0) {
			const float factor = (wheel > 0) ? 1.1f : 1.0f / 1.1f;
			camera.ZoomAt(factor, static_cast<float>(window.MouseX()), static_cast<float>(window.MouseY()));
		}

		camera.viewW = window.Width();
		camera.viewH = window.Height();

		// ---- 逻辑 tick（固定步长）----
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
		batch.End();

		renderer.BeginFrame(window.Width(), window.Height());
		renderer.DrawBatch(batch.Vertices(), camera);
		renderer.EndFrame();

		// ---- FPS 标题 ----
		fpsTimer += dt;
		++fpsFrames;
		if (fpsTimer >= 0.5) {
			fps = fpsFrames / fpsTimer;
			char title[128];
			std::snprintf(title, sizeof(title),
				"RTS Engine M3 | %.0f FPS | %zu sprites | tick %lld | zoom %.2f",
				fps, snapshot.sprites.size(),
				static_cast<long long>(sim.GetWorld().CurrentTick()), camera.zoom);
			SetWindowTextA(window.Handle(), title);
			fpsTimer = 0.0;
			fpsFrames = 0;
		}
	}

	renderer.Shutdown();
	window.Destroy();
	return 0;
}

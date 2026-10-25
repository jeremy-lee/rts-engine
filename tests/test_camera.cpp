// RTS Engine — tests/test_camera.cpp
#include "test_framework.h"
#include "render/camera.h"

using namespace rts;

TEST(camera_world_screen_roundtrip) {
	Camera cam;
	cam.centerX = 1000.0f;
	cam.centerY = 500.0f;
	cam.zoom = 1.5f;
	cam.viewW = 1280;
	cam.viewH = 720;

	for (float wx : {-500.0f, 0.0f, 1000.0f, 3000.0f}) {
		for (float wy : {-200.0f, 500.0f, 1500.0f}) {
			float sx = 0, sy = 0, rx = 0, ry = 0;
			cam.WorldToScreen(wx, wy, sx, sy);
			cam.ScreenToWorld(sx, sy, rx, ry);
			CHECK(std::abs(rx - wx) < 0.01f);
			CHECK(std::abs(ry - wy) < 0.01f);
		}
	}
}

TEST(camera_center_maps_to_screen_center) {
	Camera cam;
	cam.centerX = 777.0f;
	cam.centerY = 333.0f;
	cam.zoom = 2.0f;
	cam.viewW = 800;
	cam.viewH = 600;
	float sx = 0, sy = 0;
	cam.WorldToScreen(cam.centerX, cam.centerY, sx, sy);
	CHECK(std::abs(sx - 400.0f) < 0.01f);
	CHECK(std::abs(sy - 300.0f) < 0.01f);
}

TEST(camera_pan) {
	Camera cam;
	cam.zoom = 1.0f;
	cam.PanScreen(100.0f, -50.0f);
	CHECK(std::abs(cam.centerX - (-100.0f)) < 0.01f);
	CHECK(std::abs(cam.centerY - 50.0f) < 0.01f);
}

TEST(camera_zoom_keeps_anchor) {
	Camera cam;
	cam.viewW = 800;
	cam.viewH = 600;
	cam.centerX = 100.0f;
	cam.centerY = 100.0f;
	cam.zoom = 1.0f;
	// 锚点 (200, 150) 处的世界坐标
	float wx0 = 0, wy0 = 0;
	cam.ScreenToWorld(200.0f, 150.0f, wx0, wy0);
	cam.ZoomAt(2.0f, 200.0f, 150.0f);
	float wx1 = 0, wy1 = 0;
	cam.ScreenToWorld(200.0f, 150.0f, wx1, wy1);
	CHECK(std::abs(wx1 - wx0) < 0.01f);
	CHECK(std::abs(wy1 - wy0) < 0.01f);
	CHECK(std::abs(cam.zoom - 2.0f) < 0.001f);
}

TEST(camera_clamp) {
	Camera cam;
	cam.viewW = 800;
	cam.viewH = 600;
	cam.zoom = 1.0f;
	cam.centerX = -5000.0f;
	cam.centerY = 99999.0f;
	cam.ClampToWorld(2000.0f, 1000.0f);
	CHECK(cam.centerX >= 0.0f);
	CHECK(cam.centerY <= 1000.0f);
}

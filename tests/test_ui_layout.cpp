// RTS Engine — tests/test_ui_layout.cpp
// 覆盖侧栏改版踩过的真实 bug：
//   按钮从底部栏移到右侧侧栏后，y 落进了游戏区范围，
//   而点击检测还在"顶部栏之外"的分支里 → 点按钮走成了框选，完全没反应。
// 这些断言保证「按钮一定能被点到」这个性质不会再次回归。
#include "test_framework.h"

#include "app/ui_layout.h"
#include "render/tileset.h"

#include <cstdio>

using namespace rts;
using namespace rts::uilayout;

namespace {
// 按钮中心点
struct Pt { float x, y; };
Pt Center(const ui::Rect& r) { return Pt{r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }
} // namespace

TEST(ui_layout_buttons_land_inside_play_area_vertically) {
	// 这就是当初的 bug 场景：按钮 y 全部小于屏幕高 - 底部栏，
	// 用 "my > H - bottomH" 这类判定永远命中不到。
	const Layout L = Compute(1280.0f, 720.0f);
	const float legacyBottomBarTop = L.screenH - kBottomH;

	const ui::Rect btns[4] = {L.btnBuild.rect, L.btnInfantry.rect, L.btnTank.rect, L.btnTurret.rect};
	for (const ui::Rect& r : btns) {
		CHECK(r.y + r.h <= legacyBottomBarTop);   // 完全在旧"底部栏"之上
		CHECK(r.y >= L.playTop());                // 也不在顶部栏里
	}
	// 因此必须有独立的侧栏判定，而不是靠底部栏条件
	CHECK(L.InSidebar(btns[0].x + 1.0f, btns[0].y + 1.0f));
}

TEST(ui_layout_each_button_center_hits_its_own_button_only) {
	const Layout L = Compute(1280.0f, 720.0f);
	const Pt b = Center(L.btnBuild.rect);
	const Pt i = Center(L.btnInfantry.rect);
	const Pt t = Center(L.btnTank.rect);
	const Pt u = Center(L.btnTurret.rect);

	CHECK(L.btnBuild.Hit(b.x, b.y));
	CHECK(!L.btnBuild.Hit(i.x, i.y));
	CHECK(!L.btnBuild.Hit(t.x, t.y));
	CHECK(!L.btnBuild.Hit(u.x, u.y));

	CHECK(L.btnInfantry.Hit(i.x, i.y));
	CHECK(!L.btnInfantry.Hit(b.x, b.y));

	CHECK(L.btnTurret.Hit(u.x, u.y));
	CHECK(!L.btnTurret.Hit(b.x, b.y));
}

TEST(ui_layout_classify_prefers_sidebar_buttons) {
	const Layout L = Compute(1280.0f, 720.0f);
	const Pt b = Center(L.btnBuild.rect);
	const Pt u = Center(L.btnTurret.rect);

	// 不处于放置模式时，侧栏按钮必须解析成按钮，绝不能是 PlayArea
	CHECK(L.Classify(b.x, b.y, false) == Layout::HitTarget::BuildButton);
	CHECK(L.Classify(u.x, u.y, false) == Layout::HitTarget::TurretButton);
	CHECK(L.Classify(b.x, b.y, false) != Layout::HitTarget::PlayArea);

	// 同一坐标如果不在侧栏内，就不能算按钮
	CHECK(L.Classify(L.sideX - 40.0f, b.y, false) == Layout::HitTarget::PlayArea);
}

TEST(ui_layout_play_area_excludes_sidebar) {
	const Layout L = Compute(1280.0f, 720.0f);
	// 侧栏最左侧之外是游戏区，之内的同一 y 不是
	CHECK(L.InPlayArea(L.playRight() - 1.0f, 300.0f));
	CHECK(!L.InPlayArea(L.playRight() + 1.0f, 300.0f));
	// 侧栏内任意按钮点都不会被当作放置落点
	const Pt t = Center(L.btnTank.rect);
	CHECK(!L.InPlayArea(t.x, t.y));
}

TEST(ui_layout_buttons_do_not_overlap_and_stay_on_screen) {
	const Layout L = Compute(1280.0f, 720.0f);
	const ui::Rect r[4] = {L.btnTurret.rect, L.btnTank.rect, L.btnInfantry.rect, L.btnBuild.rect};
	for (int i = 0; i + 1 < 4; ++i) {
		// 自上而下排列，相邻两个不能重叠
		CHECK(r[i].y + r[i].h <= r[i + 1].y);
	}
	for (const ui::Rect& b : r) {
		CHECK(b.x >= 0.0f);
		CHECK(b.x + b.w <= L.screenW);
		CHECK(b.y >= 0.0f);
		CHECK(b.y + b.h <= L.screenH);
	}
	// 侧栏不能压住小地图
	CHECK(L.sideX > L.mmX + L.mmSize);
}

TEST(ui_layout_survives_common_resolutions) {
	// 1280x720 / 1400x800 / 1920x1080 下按钮都要完整可见且可点
	const float sizes[3][2] = {{1280.0f, 720.0f}, {1400.0f, 800.0f}, {1920.0f, 1080.0f}};
	for (const auto& s : sizes) {
		const Layout L = Compute(s[0], s[1]);
		const ui::Rect btns[4] = {L.btnBuild.rect, L.btnInfantry.rect, L.btnTank.rect, L.btnTurret.rect};
		for (const ui::Rect& r : btns) {
			CHECK(r.w > 0.0f);
			CHECK(r.h > 0.0f);
			CHECK(r.x + r.w <= L.screenW);
			CHECK(r.y >= 0.0f);
			CHECK(r.y + r.h <= L.screenH);
			const Pt c = Center(r);
			CHECK(L.Classify(c.x, c.y, false) != Layout::HitTarget::None);
		}
	}
}

// ---- 地形图集的帧号语义（变形号必须取模，不能 clamp）----
// 真实 RA 地图 (agenda.oramap) 里 clear1 的 index 最大到 29，而 clear1.tem 只有 16 帧。
// 之前越界 clamp 到最后一帧，导致不同变体全画成同一张图，整片地面看起来是乱拼的。
TEST(tileset_frame_index_uses_modulo) {
	using namespace rts;
	TileSet::LoadSet("TEMPERAT", "assets/tileset_TEMPERAT.txt");
	CHECK(TileSet::UseSet("TEMPERAT"));

	float a0 = 0, b0 = 0, c0 = 0, d0 = 0;
	float a1 = 0, b1 = 0, c1 = 0, d1 = 0;
	// clear1 (templateId 255) 有 16 帧
	CHECK(TileSet::GetUV(255, 0, a0, b0, c0, d0));
	CHECK(TileSet::GetUV(255, 16, a1, b1, c1, d1));
	// 16 % 16 == 0：必须和帧 0 完全一致，而不是被 clamp 到帧 15
	CHECK_EQ(a0, a1);
	CHECK_EQ(b0, b1);
	CHECK_EQ(c0, c1);
	CHECK_EQ(d0, d1);

	CHECK(TileSet::GetUV(255, 17, a1, b1, c1, d1));
	CHECK(TileSet::GetUV(255, 1, a0, b0, c0, d0));
	CHECK_EQ(a0, a1);
	CHECK_EQ(b0, b1);

	// 越界变体不能和"最后一帧"重合（这正是旧 clamp 的行为）
	CHECK(TileSet::GetUV(255, 29, a1, b1, c1, d1));
	CHECK(TileSet::GetUV(255, 15, a0, b0, c0, d0));
	CHECK(!(a0 == a1 && b0 == b1));   // 29 % 16 = 13 != 15
}
// ---- 框选矩形 ----
TEST(ui_selection_rect_normalizes_drag_direction) {
	// 四个方向的拖拽都要得到同一个矩形
	const SelectionRect a = ComputeSelectionRect(100, 100, 300, 250, 1280, 720);
	const SelectionRect b = ComputeSelectionRect(300, 250, 100, 100, 1280, 720);
	const SelectionRect c = ComputeSelectionRect(300, 100, 100, 250, 1280, 720);
	const SelectionRect d = ComputeSelectionRect(100, 250, 300, 100, 1280, 720);
	CHECK_EQ(a.x, b.x); CHECK_EQ(a.y, b.y); CHECK_EQ(a.w, b.w); CHECK_EQ(a.h, b.h);
	CHECK_EQ(a.x, c.x); CHECK_EQ(a.y, c.y); CHECK_EQ(a.w, c.w); CHECK_EQ(a.h, c.h);
	CHECK_EQ(a.x, d.x); CHECK_EQ(a.y, d.y); CHECK_EQ(a.w, d.w); CHECK_EQ(a.h, d.h);
	CHECK_EQ(a.x, 100.0f);
	CHECK_EQ(a.y, 100.0f);
	CHECK_EQ(a.w, 200.0f);
	CHECK_EQ(a.h, 150.0f);
}

TEST(ui_selection_rect_hidden_for_a_click) {
	// 原地点击（几乎没有位移）不该画出框
	const SelectionRect r = ComputeSelectionRect(200, 200, 200, 200, 1280, 720);
	CHECK(!r.Visible());
	const SelectionRect tiny = ComputeSelectionRect(200, 200, 200.4f, 200.4f, 1280, 720);
	CHECK(!tiny.Visible());
}

TEST(ui_selection_rect_clamped_to_play_area) {
	// 从 UI 区域里起拖也要夹进游戏区，框不能画到顶栏/底栏/侧栏上
	const SelectionRect r = ComputeSelectionRect(5, 5, 1275, 715, 1280, 720);
	CHECK(r.x >= 0.0f);
	CHECK(r.y >= kTopBarH);
	CHECK(r.x + r.w <= 1280.0f - kSidebarW);
	CHECK(r.y + r.h <= 720.0f - kBottomH);
	CHECK(r.Visible());
}
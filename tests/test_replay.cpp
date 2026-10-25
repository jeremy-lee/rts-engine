// RTS Engine — tests/test_replay.cpp
// M2 验收核心：回放重放必须与直接模拟得到相同状态哈希。
#include "test_framework.h"
#include "sim/replay.h"

using namespace rts;

namespace {

Replay BuildReplay(int32_t units) {
	Replay replay;
	for (int i = 0; i < units; ++i) {
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = static_cast<uint8_t>(i % 2);
		c.x = (20 + (i % 8) * 2) * kSubPerCell;
		c.y = (20 + (i / 8) * 2) * kSubPerCell;
		replay.Record(0, c);
	}
	for (int i = 0; i < units; ++i) {
		Command m;
		m.type = CommandType::Move;
		m.entityId = static_cast<uint32_t>(i + 1);
		m.x = (60 + (i % 4)) * kSubPerCell;
		m.y = (40 + (i / 4) % 4) * kSubPerCell;
		replay.Record(10, m);
	}
	return replay;
}

} // namespace

TEST(replay_playback_deterministic) {
	const Map map = Map::GenerateTestMap(64, 64, 100);
	const Replay replay = BuildReplay(16);

	const uint64_t h1 = replay.Playback(map, 42, 200);
	const uint64_t h2 = replay.Playback(map, 42, 200);
	CHECK_EQ(h1, h2);
}

TEST(replay_matches_direct_simulation) {
	// 关键验收：回放重放 == 直接按 tick 投递命令的模拟
	const Map map = Map::GenerateTestMap(64, 64, 200);
	const Replay replay = BuildReplay(12);

	// 直接模拟
	Simulation sim(map, 42);
	size_t next = 0;
	const auto& frames = replay.Frames();
	for (Tick t = 0; t < 200; ++t) {
		while (next < frames.size() && frames[next].tick <= t) {
			sim.Commands().Push(frames[next].cmd);
			++next;
		}
		sim.Step();
	}
	const uint64_t direct = sim.GetWorld().StateHash();
	const uint64_t played = replay.Playback(map, 42, 200);
	CHECK_EQ(direct, played);
}

TEST(replay_hash_stable) {
	const Replay a = BuildReplay(4);
	const Replay b = BuildReplay(4);
	CHECK_EQ(a.Hash(), b.Hash());
}

TEST(replay_different_seed_differs) {
	const Map map = Map::GenerateTestMap(64, 64, 300);
	const Replay replay = BuildReplay(8);
	const uint64_t h1 = replay.Playback(map, 1, 100);
	const uint64_t h2 = replay.Playback(map, 2, 100);
	CHECK_NE(h1, h2);
}

// RTS Engine — sim/replay.h
// 回放：记录「tick + 命令」序列，用同种子重放 → 状态哈希必须一致。
// 这是锁步联机的调试/校验基础设施（M7 会扩展为网络消息）。
#pragma once

#include <cstdint>
#include <vector>
#include "command.h"
#include "world.h"
#include "simulation.h"

namespace rts {

struct ReplayFrame {
	Tick tick = 0;
	Command cmd;
};

class Replay {
public:
	void Record(Tick tick, const Command& cmd) {
		m_frames.push_back(ReplayFrame{tick, cmd});
	}

	const std::vector<ReplayFrame>& Frames() const { return m_frames; }
	size_t FrameCount() const { return m_frames.size(); }

	void Clear() { m_frames.clear(); }

	// 重放：用给定地图与种子从零模拟到 ticks，期间按 tick 投递记录的命令。
	// 返回最终状态哈希。
	uint64_t Playback(const Map& map, uint64_t seed, Tick ticks) const {
		Simulation sim(map, seed);
		size_t next = 0;
		for (Tick t = 0; t < ticks; ++t) {
			while (next < m_frames.size() && m_frames[next].tick <= t) {
				sim.Commands().Push(m_frames[next].cmd);
				++next;
			}
			sim.Step();
		}
		return sim.GetWorld().StateHash();
	}

	uint64_t Hash() const {
		uint64_t h = 1469598103934665603ull;
		for (const auto& f : m_frames) {
			h = HashMix(h, static_cast<uint64_t>(f.tick));
			const uint8_t* p = reinterpret_cast<const uint8_t*>(&f.cmd);
			for (size_t i = 0; i < sizeof(Command); ++i) {
				h = HashMix(h, p[i]);
			}
		}
		return h;
	}

private:
	std::vector<ReplayFrame> m_frames;
};

} // namespace rts

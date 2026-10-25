// RTS Engine — sim/command_queue.h
// 命令队列：输入 → Command → 队列 → 在 tick 边界消费。
// M0 版本：FIFO，按到达顺序在下一 tick 统一执行（锁步要求：所有客户端收到相同顺序）。
#pragma once

#include <vector>
#include "command.h"

namespace rts {

class CommandQueue {
public:
	void Push(const Command& cmd) { m_pending.push_back(cmd); }

	size_t PendingCount() const { return m_pending.size(); }

	// 取出本 tick 要执行的命令（交换后清空，保证顺序稳定）
	void Consume(std::vector<Command>& out) {
		out.swap(m_pending);
		m_pending.clear();
	}

	// 确定性哈希（用于联机指令校验）
	uint64 Hash() const {
		uint64 h = 1469598103934665603ull;
		for (const auto& c : m_pending) {
			const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
			for (size_t i = 0; i < sizeof(Command); ++i) {
				h ^= p[i];
				h *= 1099511628211ull;
			}
		}
		return h;
	}

private:
	std::vector<Command> m_pending;
};

} // namespace rts

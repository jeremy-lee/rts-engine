// RTS Engine — sim/command.h
// 命令：玩家操作在 tick 边界统一执行。命令必须是可序列化、可复制的平凡类型。
#pragma once

#include <cstdint>
#include <type_traits>
#include "core/core.h"

namespace rts {

enum class CommandType : uint8_t {
	None = 0,
	Spawn = 1,   // 生成实体（测试/编辑器用）
	Move = 2,    // 移动到目标子格坐标
	Stop = 3,    // 停止当前活动
	Attack = 4,  // 攻击目标实体
};

#pragma pack(push, 1)
struct Command {
	CommandType type = CommandType::None;
	uint8_t playerId = 0;
	uint16_t pad0 = 0;
	uint32_t entityId = 0;
	uint32_t targetId = 0;
	int32_t x = 0; // SubCoord.x
	int32_t y = 0; // SubCoord.y
};
#pragma pack(pop)

static_assert(sizeof(Command) == 20, "Command 必须是固定 20 字节（可序列化）");
static_assert(std::is_trivially_copyable_v<Command>, "Command 必须可平凡复制");

} // namespace rts

// RTS Engine — core/tick.h
// 固定步长时间常量。逻辑层禁止使用 wall-clock。
#pragma once

#include "fixed.h"

namespace rts {

// 逻辑固定步长：30 Hz
inline constexpr int kTicksPerSecond = 30;
inline constexpr int kTickMs = 1000 / kTicksPerSecond; // 33（整数毫秒，逻辑层不用）

// Tick 计数（逻辑时间唯一来源）
using Tick = int64;

constexpr Tick TickFromSeconds(int seconds) { return static_cast<Tick>(seconds) * kTicksPerSecond; }

} // namespace rts

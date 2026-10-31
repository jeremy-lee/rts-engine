// RTS Engine — sim/wdist.h
// WDist 解析（OpenRA 的距离字面量：`5c0` / `0c512` / `128`）。
//
// 单独成头的原因：ParseWDist 原本定义在 weapon.h 里，而 weapon.h 又定义运行时组件
// `Armament`，和 entity.h 的同名结构体冲突。traits.cpp 只需要这个解析函数，
// 不应该为此把整条 weapon.h 依赖拉进来。
#pragma once

#include <cstdint>
#include <string>

namespace rts {

// 支持 "5c0" / "0c512" / "128"(纯数字按子格) / "1c"(c 后缺省为 0)
int32_t ParseWDist(const std::string& s, int32_t fallback = 0);

} // namespace rts

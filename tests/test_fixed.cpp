// RTS Engine — tests/test_fixed.cpp
#include "test_framework.h"
#include "core/fixed.h"

using namespace rts;

TEST(fixed_basic_arithmetic) {
	CHECK_EQ(Fixed::FromInt(3).Raw(), 3 * Fixed::kOne);
	CHECK_EQ((Fixed::FromInt(2) + Fixed::FromInt(3)).Round(), 5);
	CHECK_EQ((Fixed::FromInt(10) - Fixed::FromInt(4)).Round(), 6);
	CHECK_EQ((Fixed::FromInt(2) * Fixed::FromInt(3)).Round(), 6);
	CHECK_EQ((Fixed::FromInt(10) / Fixed::FromInt(4)).Round(), 3); // 2.5 -> 3（四舍五入）
}

TEST(fixed_fraction_math) {
	// 0.5 * 0.5 = 0.25
	const Fixed half = Fixed::FromRaw(Fixed::kOne / 2);
	const Fixed quarter = half * half;
	CHECK_EQ(quarter.Raw(), Fixed::kOne / 4);
	// 1/3 近似
	const Fixed third = Fixed::FromInt(1) / Fixed::FromInt(3);
	CHECK(third.Raw() > 0);
	CHECK(third.Raw() < Fixed::kOne);
}

TEST(fixed_negative) {
	CHECK_EQ(Fixed::FromInt(-5).Round(), -5);
	CHECK_EQ((Fixed::FromInt(-5) + Fixed::FromInt(2)).Round(), -3);
	CHECK_EQ((Fixed::FromInt(-3) * Fixed::FromInt(2)).Round(), -6);
}

TEST(fixed_truncation_and_round) {
	CHECK_EQ(Fixed::FromRaw(Fixed::kOne + Fixed::kHalf / 2).Round(), 1); // 1.25 -> 1
	CHECK_EQ(Fixed::FromRaw(2 * Fixed::kOne + Fixed::kHalf + 1).Round(), 3); // 2.5+ -> 3
}

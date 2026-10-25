// RTS Engine — tests/test_random.cpp
#include "test_framework.h"
#include "core/random.h"
#include <vector>

using namespace rts;

TEST(random_deterministic_same_seed) {
	Rng a(12345), b(12345);
	for (int i = 0; i < 100; ++i) {
		CHECK_EQ(a.Next(), b.Next());
	}
}

TEST(random_different_seed_differs) {
	Rng a(1), b(2);
	bool anyDiff = false;
	for (int i = 0; i < 10; ++i) {
		if (a.Next() != b.Next()) anyDiff = true;
	}
	CHECK(anyDiff);
}

TEST(random_range_in_bounds) {
	Rng r(999);
	for (int i = 0; i < 1000; ++i) {
		const uint64_t v = r.Range(100);
		CHECK(v < 100);
	}
}

TEST(random_range_int) {
	Rng r(7);
	for (int i = 0; i < 1000; ++i) {
		const int32_t v = r.RangeInt(-5, 5);
		CHECK(v >= -5);
		CHECK(v < 5);
	}
}

TEST(random_fixed_zero_one) {
	Rng r(42);
	for (int i = 0; i < 1000; ++i) {
		const Fixed f = r.NextFixed();
		CHECK(f.Raw() >= 0);
		CHECK(f.Raw() < Fixed::kOne);
	}
}

TEST(random_fixed_negative) {
	CHECK_EQ(Fixed::FromInt(-5).Round(), -5);
}

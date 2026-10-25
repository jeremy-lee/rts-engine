// RTS Engine — tests/test_framework.h
// 零依赖轻量测试框架：注册测试 → 顺序执行 → 汇总结果。
#pragma once

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <functional>

namespace test {

struct TestCase {
	std::string name;
	std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
	static std::vector<TestCase> r;
	return r;
}

inline int& Failures() {
	static int f = 0;
	return f;
}

struct Registrar {
	Registrar(const char* name, std::function<void()> fn) {
		Registry().push_back({name, std::move(fn)});
	}
};

inline void Fail(const char* file, int line, const char* expr) {
	std::printf("    [FAIL] %s:%d  %s\n", file, line, expr);
	++Failures();
}

inline int RunAll() {
	int passed = 0;
	for (auto& t : Registry()) {
		const int before = Failures();
		std::printf("[ RUN  ] %s\n", t.name.c_str());
		t.fn();
		if (Failures() == before) {
			std::printf("[  OK  ] %s\n", t.name.c_str());
			++passed;
		}
	}
	std::printf("\n=== %d/%zu tests passed, %d assertion(s) failed ===\n",
		passed, Registry().size(), Failures());
	return Failures() == 0 ? 0 : 1;
}

} // namespace test

#define TEST(name) \
	static void test_##name(); \
	static test::Registrar registrar_##name(#name, test_##name); \
	static void test_##name()

#define CHECK(expr) do { if (!(expr)) test::Fail(__FILE__, __LINE__, #expr); } while (0)
#define CHECK_EQ(a, b) do { if (!((a) == (b))) test::Fail(__FILE__, __LINE__, #a " == " #b); } while (0)
#define CHECK_NE(a, b) do { if (!((a) != (b))) test::Fail(__FILE__, __LINE__, #a " != " #b); } while (0)

// RTS Engine — tests/test_main.cpp
#include "test_framework.h"
#include <cstdio>

int main() {
	// 无缓冲输出：崩溃时也能看到最后执行到哪个测试
	setvbuf(stdout, nullptr, _IONBF, 0);
	std::printf("=== RTS Engine tests ===\n");
	return test::RunAll();
}

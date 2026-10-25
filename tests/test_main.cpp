// RTS Engine — tests/test_main.cpp
#include "test_framework.h"
#include <cstdio>

int main() {
	std::printf("=== RTS Engine tests ===\n");
	return test::RunAll();
}

#include "dsp/LadderTap.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace jxmp;

int main() {
	const float s[4] = {3.f, 3.f, 3.f, 3.f};
	for (bool steep : {false, true}) {
		assert(std::fabs(ladderTap(LP, steep, 3.f, s) - 3.f) < 1e-5f);
		assert(std::fabs(ladderTap(NOTCH, steep, 3.f, s) - 3.f) < 1e-5f);
		assert(std::fabs(ladderTap(BP, steep, 3.f, s)) < 1e-5f);
		assert(std::fabs(ladderTap(HP, steep, 3.f, s)) < 1e-5f);
	}
	const float z[4] = {0, 0, 0, 0};
	assert(std::fabs(ladderTap(HP, true, 2.f, z) - 2.f) < 1e-5f);  // HF passes the HP
	std::puts("ladder: all tests passed");
	return 0;
}

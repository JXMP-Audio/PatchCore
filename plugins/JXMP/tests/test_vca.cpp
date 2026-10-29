#include "dsp/Vca.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
	assert(jxmp::vcaGain(0.f, false) == 0.f);
	assert(jxmp::vcaGain(1.f, false) == 1.f);
	assert(std::fabs(jxmp::vcaGain(0.f, true)) < 1e-6f);
	assert(std::fabs(jxmp::vcaGain(1.f, true) - 1.f) < 1e-6f);
	for (float x = 0.05f; x < 1.f; x += 0.05f)
		assert(jxmp::vcaGain(x, true) < jxmp::vcaGain(x, false));
	assert(jxmp::vcaGain(-3.f, false) == 0.f && jxmp::vcaGain(5.f, true) == 1.f);  // clamped
	std::puts("vca: all tests passed");
	return 0;
}

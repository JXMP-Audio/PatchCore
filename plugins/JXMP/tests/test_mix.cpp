#include "dsp/Mix.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main() {
	const float in[4] = {1.f, 2.f, 3.f, 4.f};
	const float level[4] = {1.f, 0.5f, 0.f, 1.f};
	const float cv[4] = {1.f, 1.f, 1.f, 0.5f};
	const bool mute[4] = {false, false, false, true};
	// 1·1 + 2·0.5 + 0 + muted = 2, master 1
	assert(near(mix4(in, level, cv, mute, 1.f), 2.f));
	assert(near(mix4(in, level, cv, mute, 2.f), 4.f));
	const bool none[4] = {false, false, false, false};
	// CV scales its channel: 4 · 1 · 0.5 = 2 more
	assert(near(mix4(in, level, cv, none, 1.f), 4.f));
	// CV is clamped to 0..1
	const float cvBad[4] = {-3.f, 7.f, 1.f, 1.f};
	assert(near(mix4(in, level, cvBad, none, 1.f), 0.f + 1.f + 0.f + 4.f));
	assert(near(cvGain(5.f, true), 0.5f) && near(cvGain(0.f, false), 1.f) && near(cvGain(-2.f, true), 0.f));
	std::puts("mix: all tests passed");
	return 0;
}

#include "dsp/Att.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main() {
	assert(near(attenuvert(5.f, 0.5f, 0.f), 2.5f));
	assert(near(attenuvert(5.f, -1.f, 1.f), -4.f));
	assert(near(attenuvert(5.f, 2.f, 0.f), 10.f));
	assert(near(attenuvert(5.f, 5.f, 0.f), 10.f));  // gain clamped to ±2
	assert(near(attenuvert(8.f, 1.f, 8.f), 12.f));  // output clamped to ±12 V

	// Normalling: an unpatched input takes the input above it; the first one is a 10 V constant
	float in[4] = {3.f, 0.f, -2.f, 0.f};
	const bool patched[4] = {true, false, true, false};
	float out[4];
	normalInputs(in, patched, out);
	assert(near(out[0], 3.f) && near(out[1], 3.f) && near(out[2], -2.f) && near(out[3], -2.f));
	const bool none[4] = {false, false, false, false};
	normalInputs(in, none, out);
	for (int i = 0; i < 4; ++i)
		assert(near(out[i], 10.f));
	std::puts("att: all tests passed");
	return 0;
}

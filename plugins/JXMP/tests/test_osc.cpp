#include "dsp/Osc.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

int main() {
	assert(morph(1, 2, 3, 4, 0.f) == 1 && morph(1, 2, 3, 4, 1.f) == 2 && morph(1, 2, 3, 4, 2.f) == 3 && morph(1, 2, 3, 4, 3.f) == 4);
	assert(std::fabs(morph(0, 10, 0, 0, 0.5f) - 5.f) < 1e-5f);
	assert(morph(1, 2, 3, 4, -1.f) == 1 && morph(1, 2, 3, 4, 9.f) == 4);  // clamped

	float o[8];
	assert(unisonOffsets(1, 30.f, o) == 1 && o[0] == 0.f);
	for (int n = 2; n <= 7; ++n) {
		assert(unisonOffsets(n, 30.f, o) == n);
		float sum = 0, mx = 0;
		for (int i = 0; i < n; ++i) { sum += o[i]; mx = std::max(mx, std::fabs(o[i])); }
		assert(std::fabs(sum) < 1e-4f && std::fabs(mx - 30.f) < 1e-4f);
	}
	assert(unisonOffsets(12, 30.f, o) == 7);

	// Sub divider: one octave down toggles once per main cycle
	SubDivider sub;
	int flips = 0;
	float last = sub.process(0.f, 1);
	for (int cyc = 0; cyc < 8; ++cyc)
		for (int i = 0; i < 100; ++i) {
			const float v = sub.process(i / 100.f, 1);
			if (v != last) ++flips;
			last = v;
		}
	// 8 cycles have 7 phase wraps between them
	assert(flips == 7);
	std::puts("osc: all tests passed");
	return 0;
}

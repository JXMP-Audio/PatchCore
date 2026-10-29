#include "dsp/Slew.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

static bool near(float a, float b, float e = 0.02f) { return std::fabs(a - b) < e; }

int main() {
	const float dt = 1.f / 1000.f;
	// Linear: a 10 V step with a 1 s rise takes 1 s (rate 10 V/s), halfway after 0.5 s
	Slew s;
	for (int i = 0; i < 500; ++i) s.process(10.f, 1.f, 1.f, false, dt);
	assert(near(s.value, 5.f));
	for (int i = 0; i < 520; ++i) s.process(10.f, 1.f, 1.f, false, dt);
	assert(near(s.value, 10.f, 1e-4f));  // and stops exactly at the target
	// Fall uses its own time: 0.1 s for 10 V down
	for (int i = 0; i < 50; ++i) s.process(0.f, 1.f, 0.1f, false, dt);
	assert(near(s.value, 5.f));
	// Exponential: after one time constant ~63 % of the way
	Slew e;
	for (int i = 0; i < 1000; ++i) e.process(10.f, 1.f, 1.f, true, dt);
	assert(near(e.value, 6.32f, 0.05f));
	// Zero time follows at once
	Slew z;
	z.process(3.f, 0.f, 0.f, false, dt);
	assert(near(z.value, 3.f, 1e-6f));
	std::puts("slew: all tests passed");
	return 0;
}

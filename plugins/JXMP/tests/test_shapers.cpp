// JX-FLD, JX-RNG, JX-LGC, JX-SW logic
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/dsp/Shapers.hpp"

using namespace jxmp::shapers;

static bool near(float a, float b, float eps = 1e-4f) {
	return std::fabs(a - b) <= eps;
}

int main() {
	// Fold 0, symmetry 0: unchanged inside ±5 V
	assert(near(fold(2.5f, 0.f, 0.f), 2.5f) && near(fold(-4.f, 0.f, 0.f), -4.f));
	// Past 5 V it folds back: 6 V -> 4 V
	assert(near(fold(6.f, 0.f, 0.f), 4.f));
	// Always within ±5 V
	for (float x = -10.f; x <= 10.f; x += 0.01f)
		for (float a : {0.f, 0.5f, 1.f})
			assert(std::fabs(fold(x, a, 0.3f)) <= 5.0001f);
	// Ring: 5 V × 5 V = 5 V; MIX 0 passes A
	assert(near(ring(5.f, 5.f, 1.f), 5.f) && near(ring(5.f, -5.f, 1.f), -5.f) && near(ring(3.f, 5.f, 0.f), 3.f));
	// Logic
	Gates g{true, false};
	assert(!g.andGate() && g.orGate() && g.xorGate());
	g = Gates{true, true};
	assert(g.andGate() && g.orGate() && !g.xorGate());
	// Stepper: 0,1,2,0 with length 3; reset; a shorter length keeps the step inside it
	Stepper s;
	s.clock(3);
	s.clock(3);
	assert(s.current(3) == 2);
	s.clock(3);
	assert(s.current(3) == 0);
	s.clock(4);
	s.clock(4);
	s.clock(4);
	assert(s.current(4) == 3 && s.current(2) == 1);
	s.reset();
	assert(s.current(4) == 0);
	std::printf("shapers: all tests passed\n");
	return 0;
}

#include "dsp/Seq.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

int main() {
	StepCounter s;
	assert(s.step == 0);
	// Before the first clock the sequence sits on step 1 (index 0); the first clock plays step 1
	s.clock(8);
	assert(s.step == 0);
	s.clock(8);
	assert(s.step == 1);
	for (int i = 0; i < 6; ++i)
		s.clock(8);
	assert(s.step == 7);
	s.clock(8);
	assert(s.step == 0);  // wraps at the length

	// Shorter length wraps earlier, and a step outside a new length folds back
	s.clock(3); s.clock(3); s.clock(3);
	assert(s.step == 0);
	s.step = 6;
	s.clock(4);
	assert(s.step == 0);

	// Reset: back to step 1, and the next clock plays step 1 again
	s.clock(8); s.clock(8);
	s.reset();
	assert(s.step == 0);
	s.clock(8);
	assert(s.step == 0);
	s.clock(8);
	assert(s.step == 1);

	// Internal clock: 120 BPM = 2 Hz, high for the first half of each beat
	InternalClock clk;
	int rising = 0;
	bool last = false;
	for (int i = 0; i < 48000; ++i) {
		const bool high = clk.process(120.f, 1.f / 48000.f);
		if (high && !last) ++rising;
		last = high;
	}
	assert(rising == 2);
	std::puts("seq: all tests passed");
	return 0;
}

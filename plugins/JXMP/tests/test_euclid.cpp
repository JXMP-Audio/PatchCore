// JX-EUC logic: Euclidean patterns, rotation, accent, stepping
#include <cassert>
#include <cstdio>
#include <string>

#include "../src/dsp/Euclid.hpp"

using namespace jxmp::euclid;

static std::string pattern(int steps, int hits, int rotate) {
	std::string s;
	for (int i = 0; i < steps; ++i)
		s += hit(i, steps, hits, rotate) ? 'x' : '.';
	return s;
}

int main() {
	// Classic Euclidean rhythms
	assert(pattern(8, 3, 0) == "x..x..x.");
	assert(pattern(16, 4, 0) == "x...x...x...x...");
	assert(pattern(5, 2, 0) == "x..x.");
	assert(pattern(16, 0, 0) == "................");
	assert(pattern(4, 4, 0) == "xxxx");
	// Too many hits are clamped to the steps, steps to 1..16
	assert(pattern(4, 9, 0) == "xxxx");
	assert(hit(0, 0, 1, 0));  // 0 steps = 1 step
	// Rotation moves the pattern later: 2 hits on 16 rotated by 4 = beats 2 and 4
	assert(pattern(16, 2, 4) == "....x.......x...");
	assert(pattern(8, 3, -1) == pattern(8, 3, 7));
	// The accent is the first hit, moved with the rotation
	assert(accentStep(16, 4) == 4 && hit(accentStep(16, 4), 16, 2, 4));
	assert(accentStep(8, 9) == 1);

	// Stepping: reset waits, the first clock plays step 0, then it wraps at the length
	Track t;
	assert(t.current(8) == -1);
	t.clock(8);
	assert(t.current(8) == 0);
	for (int i = 0; i < 8; ++i)
		t.clock(8);
	assert(t.current(8) == 0);
	t.clock(8);
	t.clock(8);
	assert(t.current(8) == 2);
	// A shorter length keeps the step inside it
	assert(t.current(2) == 0);
	t.reset();
	assert(t.current(8) == -1);

	std::printf("euclid: all tests passed\n");
	return 0;
}

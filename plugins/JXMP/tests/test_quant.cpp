#include "dsp/Quant.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
	// Chromatic: nearest semitone
	assert(near(quantize(0.04f, SCALE_CHROMATIC, 0), 0.f));
	assert(near(quantize(0.05f, SCALE_CHROMATIC, 0), 1.f / 12));
	assert(near(quantize(-0.04f, SCALE_CHROMATIC, 0), 0.f));
	// C major: C# (1/12) is between C and D -> rounds to the nearer one, ties go down
	assert(near(quantize(1.f / 12, SCALE_MAJOR, 0), 0.f));
	assert(near(quantize(1.4f / 12, SCALE_MAJOR, 0), 2.f / 12));
	// F# in C major -> F or G; 6.2 semitones is nearer G
	assert(near(quantize(6.2f / 12, SCALE_MAJOR, 0), 7.f / 12));
	// Octaves are kept
	assert(near(quantize(1.f + 4.1f / 12, SCALE_MAJOR, 0), 1.f + 4.f / 12));
	assert(near(quantize(-1.f + 11.2f / 12, SCALE_MAJOR, 0), -1.f + 11.f / 12));
	// Root D: D major contains F# (6) but not F (5)
	assert(near(quantize(5.f / 12, SCALE_MAJOR, 2), 6.f / 12) || near(quantize(5.f / 12, SCALE_MAJOR, 2), 4.f / 12));
	assert(near(quantize(6.f / 12, SCALE_MAJOR, 2), 6.f / 12));
	// Minor pentatonic in A (root 9): A C D E G
	assert(near(quantize(1.f / 12, SCALE_MINOR_PENTA, 9), 0.f));    // C# -> C
	assert(near(quantize(10.f / 12, SCALE_MINOR_PENTA, 9), 9.f / 12));  // A# -> A
	// Every scale has its root
	for (int s = 0; s < NUM_SCALES; ++s)
		assert(scaleMask((Scale) s) & 1);
	// Note index of a voltage, for the LEDs
	assert(noteOf(0.f) == 0 && noteOf(7.f / 12) == 7 && noteOf(-1.f / 12) == 11);
	std::puts("quant: all tests passed");
	return 0;
}

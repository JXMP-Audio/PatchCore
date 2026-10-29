#include "dsp/Lfo.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
	// Shapes are bipolar ±1 and start at 0 (sine, triangle, saw) at phase 0
	assert(near(lfoShape(LFO_SIN, 0.f, 0.5f), 0.f) && near(lfoShape(LFO_SIN, 0.25f, 0.5f), 1.f) && near(lfoShape(LFO_SIN, 0.75f, 0.5f), -1.f));
	assert(near(lfoShape(LFO_TRI, 0.f, 0.5f), 0.f) && near(lfoShape(LFO_TRI, 0.25f, 0.5f), 1.f) && near(lfoShape(LFO_TRI, 0.75f, 0.5f), -1.f));
	assert(near(lfoShape(LFO_SAW, 0.f, 0.5f), 0.f) && near(lfoShape(LFO_SAW, 0.49f, 0.5f), 0.98f) && near(lfoShape(LFO_SAW, 0.51f, 0.5f), -0.98f));
	assert(lfoShape(LFO_SQR, 0.2f, 0.3f) == 1.f && lfoShape(LFO_SQR, 0.4f, 0.3f) == -1.f);

	// Unipolar shifts the start to the bottom: 0..1, starting at 0
	assert(near(lfoOut(LFO_SIN, 0.f, 0.5f, true, false), 0.f) && near(lfoOut(LFO_SIN, 0.5f, 0.5f, true, false), 1.f));
	assert(near(lfoOut(LFO_SAW, 0.25f, 0.5f, false, true), -0.5f));  // inverted bipolar

	// Rate: 2^param Hz with the default clock (2 Hz), scaled by an external clock
	assert(near(lfoFreq(0.f, 2.f), 1.f) && near(lfoFreq(1.f, 2.f), 2.f) && near(lfoFreq(-8.f, 2.f), 1.f / 256.f));
	assert(near(lfoFreq(0.f, 4.f), 2.f));

	// Phase advance wraps and never steps more than half a cycle
	float ph = 0.9f;
	ph = lfoAdvance(ph, 1.f, 0.2f);
	assert(near(ph, 0.1f));
	assert(near(lfoAdvance(0.f, 1000.f, 1.f), 0.5f));
	std::puts("lfo: all tests passed");
	return 0;
}

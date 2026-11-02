// JX-CHR: depth 0 is a plain delay, the output stays bounded, the mix law
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/dsp/Chorus.hpp"

using namespace jxmp::chorus;

int main() {
	const float fs = 48000.f;
	// Depth 0, chorus: the wet signal is the input 7 ms later
	Chorus c;
	c.setSampleRate(fs);
	const int d = (int) (0.007f * fs);
	float out[2000];
	for (int i = 0; i < 2000; ++i) {
		float l, r;
		c.process(i == 0 ? 1.f : 0.f, 0.f, 1.f, 0.f, false, &l, &r);
		out[i] = l;
	}
	assert(std::fabs(out[d] - 1.f) < 0.01f && std::fabs(out[d - 5]) < 1e-6f);

	// Full depth, ensemble, loud input: bounded, both sides alive
	Chorus e;
	e.setSampleRate(fs);
	float peakL = 0.f, peakR = 0.f;
	for (int i = 0; i < 48000; ++i) {
		const float x = 5.f * std::sin(2.f * float(M_PI) * 220.f * i / fs);
		float l, r;
		e.process(x, x, 3.f, 1.f, true, &l, &r);
		assert(std::fabs(l) <= 5.01f && std::fabs(r) <= 5.01f);
		peakL = std::max(peakL, std::fabs(l));
		peakR = std::max(peakR, std::fabs(r));
	}
	assert(peakL > 1.f && peakR > 1.f);

	// Mix: 0 = dry, 1 = wet
	float m;
	mix(2.f, 3.f, 0.f, &m);
	assert(std::fabs(m - 2.f) < 1e-5f);
	mix(2.f, 3.f, 1.f, &m);
	assert(std::fabs(m - 3.f) < 1e-5f);

	std::printf("chorus: all tests passed\n");
	return 0;
}

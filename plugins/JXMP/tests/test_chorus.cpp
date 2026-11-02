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

	// A NaN at the input is dropped, not kept in the delay line
	Chorus n;
	n.setSampleRate(fs);
	for (int i = 0; i < 4800; ++i) {
		float l, r;
		n.process(i == 10 ? NAN : 0.5f, 0.5f, 1.f, 0.5f, true, &l, &r);
		assert(std::isfinite(l) && std::isfinite(r));
	}
	// Switching chorus <-> ensemble glides the delay: no jump (click) in the output
	{
		Chorus sw;
		sw.setSampleRate(fs);
		float last = 0.f, worst = 0.f;
		for (int i = 0; i < 9600; ++i) {
			const float x = std::sin(2.f * float(M_PI) * 220.f * i / fs);
			float l, r;
			sw.process(x, x, 1.f, 0.f, i >= 4800, &l, &r);
			if (i > 2000)
				worst = std::max(worst, std::fabs(l - last));
			last = l;
		}
		assert(worst < 0.06f);  // a 220 Hz sine moves at most 0.029 per sample
	}
	std::printf("chorus: all tests passed\n");
	return 0;
}

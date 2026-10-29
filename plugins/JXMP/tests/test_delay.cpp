#include "dsp/Delay.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace jxmp;

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

int main() {
	// Impulse through 100 samples of delay, 50 % feedback, fully wet, neutral tone
	DelayCore d;
	d.init(1000);
	std::vector<float> out;
	for (int n = 0; n < 400; ++n)
		out.push_back(d.process(n == 0 ? 1.f : 0.f, 100.f, 0.5f, 0.f, 1.f, 1.f / 48000.f));
	assert(near(out[0], 0.f) && near(out[100], 1.f) && near(out[200], 0.5f) && near(out[300], 0.25f));
	assert(near(out[150], 0.f));

	// Dry only passes the input unchanged
	DelayCore dry;
	dry.init(1000);
	assert(near(dry.process(0.7f, 100.f, 0.5f, 0.f, 0.f, 1.f / 48000.f), 0.7f));

	// The wet signal is available on its own, whatever the mix
	DelayCore wetOnly;
	wetOnly.init(1000);
	for (int n = 0; n <= 100; ++n)
		wetOnly.process(n == 0 ? 1.f : 0.f, 100.f, 0.5f, 0.f, 0.f, 1.f / 48000.f);
	assert(near(wetOnly.lastWet, 1.f));

	// Feedback above 1 is clamped: the echoes never grow
	DelayCore fb;
	fb.init(1000);
	float peak = 0.f;
	for (int n = 0; n < 20000; ++n)
		peak = std::max(peak, std::fabs(fb.process(n == 0 ? 1.f : 0.f, 50.f, 5.f, 0.f, 1.f, 1.f / 48000.f)));
	assert(peak <= 1.0001f);

	// Lowpass tone damps a Nyquist-rate signal in the repeats, highpass damps DC
	DelayCore lp, hp;
	lp.init(1000);
	hp.init(1000);
	float lpOut = 0.f, hpOut = 0.f;
	for (int n = 0; n < 2000; ++n) {
		const float alt = (n % 2) ? 1.f : -1.f;
		lpOut = lp.process(alt, 10.f, 0.f, -1.f, 1.f, 1.f / 48000.f);
		hpOut = hp.process(1.f, 10.f, 0.f, 1.f, 1.f, 1.f / 48000.f);
	}
	assert(std::fabs(lpOut) < 0.3f);
	assert(std::fabs(hpOut) < 0.3f);

	// Delay time law: 1 ms .. 10 s, 100 ms in the middle
	assert(near(delayTime(0.f), 0.001f, 1e-6f) && near(delayTime(0.5f), 0.1f, 1e-5f) && near(delayTime(1.f), 10.f, 1e-3f));
	std::puts("delay: all tests passed");
	return 0;
}

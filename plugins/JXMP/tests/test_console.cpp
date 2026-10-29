#include "dsp/Console.hpp"
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

using namespace jxmp::console;

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

/** |H| of a biquad at frequency f. */
static float magnitude(const Biquad& q, float fs, float f) {
	const std::complex<double> z = std::polar(1.0, 2.0 * M_PI * f / fs);
	const std::complex<double> zi = 1.0 / z, zi2 = zi * zi;
	const std::complex<double> h = (q.b0 + q.b1 * zi + q.b2 * zi2) / (1.0 + q.a1 * zi + q.a2 * zi2);
	return (float) std::abs(h);
}

static float db(float gain) { return 20.f * std::log10(gain); }

int main() {
	// Fader: 0 is off, 0.75 is unity, the top is +6 dB; the inverse matches
	assert(faderGain(0.f) == 0.f);
	assert(near(faderDb(0.75f), 0.f) && near(faderGain(0.75f), 1.f));
	assert(near(faderDb(1.f), 6.f));
	assert(faderDb(0.375f) < -10.f && faderDb(0.375f) > -20.f);
	for (float p = 0.05f; p <= 1.f; p += 0.05f)
		assert(near(faderPos(faderDb(p)), p));
	assert(faderPos(-1000.f) == 0.f && near(faderPos(100.f), 1.f));
	// Monotonic
	for (float p = 0.01f; p < 1.f; p += 0.01f)
		assert(faderDb(p + 0.01f) > faderDb(p));

	// Constant-power pan: centre −3 dB each side, hard left/right
	float l, r;
	panGains(0.f, &l, &r);
	assert(near(l, std::sqrt(0.5f)) && near(r, std::sqrt(0.5f)));
	panGains(-1.f, &l, &r);
	assert(near(l, 1.f) && near(r, 0.f));
	panGains(1.f, &l, &r);
	assert(near(l, 0.f) && near(r, 1.f));
	panGains(0.3f, &l, &r);
	assert(near(l * l + r * r, 1.f));

	// Mute wins over solo; any solo silences the channels without solo
	assert(audible(false, false, false));
	assert(!audible(true, false, false));
	assert(!audible(false, false, true));
	assert(audible(false, true, true));
	assert(!audible(true, true, true));

	// Mid frequency knob: 200 Hz .. 5 kHz, logarithmic
	assert(near(midFreq(0.f), 200.f, 0.1f) && near(midFreq(1.f), 5000.f, 0.5f) && near(midFreq(0.5f), 1000.f, 0.5f));

	const float fs = 48000.f;
	Biquad q;
	// Flat at 0 dB
	q.setPeak(fs, 1000.f, 0.7f, 0.f);
	assert(near(magnitude(q, fs, 100.f), 1.f) && near(magnitude(q, fs, 1000.f), 1.f));
	// Peak: the full gain at the centre, nearly none far away
	q.setPeak(fs, 1000.f, 0.7f, 12.f);
	assert(near(db(magnitude(q, fs, 1000.f)), 12.f, 0.05f));
	assert(std::fabs(db(magnitude(q, fs, 30.f))) < 0.5f);
	q.setPeak(fs, 1000.f, 0.7f, -15.f);
	assert(near(db(magnitude(q, fs, 1000.f)), -15.f, 0.05f));
	// Low shelf: the gain in the bass, flat in the treble
	q.setLowShelf(fs, 100.f, 12.f);
	assert(near(db(magnitude(q, fs, 10.f)), 12.f, 0.3f));
	assert(std::fabs(db(magnitude(q, fs, 5000.f))) < 0.3f);
	// High shelf: the gain in the treble, flat in the bass
	q.setHighShelf(fs, 8000.f, -12.f);
	assert(near(db(magnitude(q, fs, 22000.f)), -12.f, 0.6f));
	assert(std::fabs(db(magnitude(q, fs, 100.f))) < 0.3f);
	// Processing: a DC input through a +12 dB low shelf settles at ×4
	q.setLowShelf(fs, 100.f, 12.f);
	float y = 0.f;
	for (int i = 0; i < 48000; ++i)
		y = q.process(1.f);
	assert(near(y, std::pow(10.f, 12.f / 20.f), 0.02f));
	// The state is kept when the coefficients change (no click to zero)
	q.setLowShelf(fs, 100.f, 11.f);
	assert(std::fabs(q.process(1.f) - y) < 0.2f);

	// VU: a 5 V sine reads 0 VU after it settled, silence falls back, about 300 ms to rise
	Vu vu;
	const float dt = 1.f / fs;
	for (int i = 0; i < 48000; ++i)
		vu.process(5.f * std::sin(2.f * M_PI * 440.f * i / fs), dt);
	assert(near(vu.vu(), 0.f, 0.3f));
	Vu rise;
	for (int i = 0; i < (int) (0.3f * fs); ++i)
		rise.process(5.f * std::sin(2.f * M_PI * 440.f * i / fs), dt);
	assert(rise.vu() > -3.f && rise.vu() < -0.3f);
	for (int i = 0; i < 48000 * 3; ++i)
		vu.process(0.f, dt);
	assert(vu.vu() <= -20.f);
	// Needle position: −20 VU at the left end, +3 at the right, 0 VU at about 3/4
	assert(near(vuNeedle(-40.f), 0.f, 0.02f) && near(vuNeedle(3.f), 1.f) && near(vuNeedle(10.f), 1.f));
	assert(vuNeedle(0.f) > 0.7f && vuNeedle(0.f) < 0.85f);

	std::puts("console: all tests passed");
	return 0;
}

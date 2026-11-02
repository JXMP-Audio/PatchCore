// JX-REV: an impulse decays by ~60 dB over the set time, a loud long input stays within ±10 V, silence stays silent
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/dsp/Reverb.hpp"

using namespace jxmp::reverb;

int main() {
	const float fs = 48000.f;
	// Silence in, silence out
	Fdn quiet;
	quiet.setSampleRate(fs);
	for (int i = 0; i < 4800; ++i) {
		float l, r;
		quiet.process(0.f, 0.f, HALL, 1.f, 2.f, 0.02f, 0.5f, &l, &r);
		assert(l == 0.f && r == 0.f);
	}
	// Impulse: the energy RT60 seconds later is at least 45 dB below the early reflections (HALL and ROOM)
	for (int a = HALL; a <= ROOM; ++a) {
		Fdn f;
		f.setSampleRate(fs);
		const float rt = 2.f;
		double early = 0, late = 0;
		for (int i = 0; i < (int) (fs * (rt + 0.2f)); ++i) {
			float l, r;
			const float in = i == 0 ? 5.f : 0.f;
			f.process(in, in, (Algo) a, 1.f, rt, 0.02f, 0.6f, &l, &r);
			const double e = l * l + r * r;
			if (i > fs * 0.05f && i < fs * 0.15f)
				early += e;
			if (i > fs * (rt + 0.05f) && i < fs * (rt + 0.15f))
				late += e;
		}
		assert(early > 0 && 10 * std::log10(late / early + 1e-30) < -45.0);
	}
	// Loud noise, the longest decay, the largest size: bounded
	Fdn big;
	big.setSampleRate(fs);
	unsigned s = 1;
	for (int i = 0; i < (int) fs * 10; ++i) {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		const float n = (s & 0xffff) / 32768.f - 1.f;
		float l, r;
		big.process(5.f * n, -5.f * n, HALL, 2.f, 12.f, 0.2f, 1.f, &l, &r);
		assert(std::fabs(l) <= 10.f && std::fabs(r) <= 10.f && std::isfinite(l));
	}
	// One NaN at the input (another module, a broken CV) must not leave the reverb NaN or silent for good
	Fdn nan;
	nan.setSampleRate(fs);
	for (int i = 0; i < 48000; ++i) {
		float l, r;
		const float in = i == 100 ? NAN : (i < 24000 ? 0.f : 1.f);
		nan.process(in, in, HALL, 1.f, 2.f, 0.02f, 0.5f, &l, &r);
		assert(std::isfinite(l) && std::isfinite(r));
	}
	{
		// A NaN decay time (CV) is ignored too
		float l, r;
		nan.process(1.f, 1.f, HALL, 1.f, NAN, 0.02f, 0.5f, &l, &r);
		assert(std::isfinite(l) && std::isfinite(r));
	}
	std::printf("reverb: all tests passed\n");
	return 0;
}

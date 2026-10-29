#include "dsp/Envelope.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

static int run(Envelope& e, float seconds, bool gate, Envelope::Mode m, int* eocs = nullptr, bool gateConnected = true) {
	const float dt = 1.f / 1000.f;
	int n = 0;
	for (float t = 0; t < seconds; t += dt) {
		e.process(dt, gate, false, gateConnected, 0.01f, 0.02f, 0.5f, 0.05f, 0.f, m);
		assert(std::isfinite(e.value) && e.value >= -1e-4f && e.value <= 1.0001f);
		if (e.eoc)
			++n;
	}
	if (eocs)
		*eocs = n;
	return n;
}

int main() {
	for (float s : {-1.f, 0.f, 1.f}) {
		assert(std::fabs(curve(0.f, s)) < 1e-6f && std::fabs(curve(1.f, s) - 1.f) < 1e-6f);
		for (float x = 0.f; x < 0.99f; x += 0.01f)
			assert(curve(x + 0.01f, s) >= curve(x, s));
	}
	assert(curve(0.3f, 1.f) > curve(0.3f, -1.f));

	// ADSR reaches sustain, then releases to 0 with one EOC
	Envelope e;
	run(e, 0.2f, true, Envelope::ADSR);
	assert(e.stage == Envelope::SUSTAIN && std::fabs(e.value - 0.5f) < 0.01f);
	int eocs = 0;
	run(e, 0.3f, false, Envelope::ADSR, &eocs);
	assert(e.value < 0.01f && eocs == 1);

	// AD ignores an early gate-off and ends at 0 with one EOC
	Envelope ad;
	ad.process(0.001f, true, false, true, 0.01f, 0.02f, 0.5f, 0.05f, 0.f, Envelope::AD);
	run(ad, 0.2f, false, Envelope::AD, &eocs);
	assert(ad.value < 0.01f && eocs == 1);

	// LOOP with the gate held cycles several times
	Envelope lp;
	run(lp, 0.5f, true, Envelope::LOOP, &eocs);
	assert(eocs >= 10);
	// LOOP free-runs when no gate is connected
	Envelope fr;
	run(fr, 0.5f, false, Envelope::LOOP, &eocs, false);
	assert(eocs >= 10);

	// Attack 0 does not divide by zero
	Envelope z;
	z.process(0.001f, true, false, true, 0.f, 0.f, 1.f, 0.f, 0.f, Envelope::ADSR);
	assert(std::isfinite(z.value));

	std::puts("envelope: all tests passed");
	return 0;
}

// JX-SCP logic: scales, trigger, sweep capture
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/dsp/Scope.hpp"

using namespace jxmp::scope;

static bool near(float a, float b, float eps = 1e-4f) {
	return std::fabs(a - b) <= eps;
}

int main() {
	// Steps are clamped to the knob range
	assert(near(timeDiv(0), 0.0001f) && near(timeDiv(99), 1.f) && near(timeDiv(-3), 0.0001f));
	assert(near(voltDiv(3), 1.f) && near(voltDiv(9), 5.f));
	assert(near(divisions(2.f, 1.f, 0.5f), 2.5f) && near(divisions(-5.f, 5.f, 0.f), -1.f));

	// Trigger: one rising crossing, noise around the level does not fire again
	Trigger t;
	int fires = 0;
	const float ramp[] = {-1.f, -0.5f, 0.f, 0.06f, 0.02f, 0.07f, 0.03f, 0.5f, 1.f};
	for (float x : ramp)
		fires += t.process(x, 0.f, true);
	assert(fires == 1);
	// Falling edge only when falling is chosen
	Trigger f;
	fires = 0;
	for (float x : {1.f, 0.5f, -0.5f, -1.f, 1.f})
		fires += f.process(x, 0.f, false);
	assert(fires == 1);
	// The first sample never fires (nothing to compare with)
	Trigger first;
	assert(!first.process(5.f, 0.f, true));

	// Capture: a 100 Hz sine at 48 kHz, 1 ms/div → one sweep is 8 ms; triggered sweeps start near 0 V rising
	const float fs = 48000.f, dt = 1.f / fs;
	Capture c;
	Trigger tr;
	for (int i = 0; i < 48000; ++i) {
		const float x = 5.f * std::sin(2.f * float(M_PI) * 100.f * i / fs);
		c.process(x, -x, tr.process(x, 0.f, true), dt, 0.001f);
	}
	assert(c.sweeps >= 50 && c.sweeps <= 100);  // at most one sweep per 10 ms period
	assert(std::fabs(c.shown[0][0]) < 0.5f && c.shown[0][4] > c.shown[0][0]);
	assert(near(c.shown[1][10], -c.shown[0][10]));
	// 8 ms of a 10 ms period: the sweep reaches the negative half
	float lowest = 0.f;
	for (int i = 0; i < kPoints; ++i)
		lowest = std::min(lowest, c.shown[0][i]);
	assert(lowest < -4.f);

	// No trigger: AUTO free-runs after two sweep lengths, a steady voltage still shows
	Capture dc;
	for (int i = 0; i < 4800; ++i)
		dc.process(2.f, 0.f, false, dt, 0.001f);
	assert(dc.sweeps >= 1 && near(dc.shown[0][100], 2.f));

	// NORM: no trigger, no sweep (the screen stays dark); a trigger starts one
	Capture norm;
	for (int i = 0; i < 4800; ++i)
		norm.process(2.f, 0.f, false, dt, 0.001f, false, false);
	assert(norm.sweeps == 0 && !norm.running());
	norm.process(2.f, 0.f, true, dt, 0.001f, false, false);
	assert(norm.running());

	// X–Y: free-run sweeps back to back
	Capture xy;
	for (int i = 0; i < 4800; ++i)
		xy.process(1.f, 1.f, false, dt, 0.001f, true);
	assert(xy.sweeps >= 5);

	// A long time base (1 s/div) at audio rate takes one point per 1/32 s, never more per sample
	Capture slow;
	for (int i = 0; i < 48000; ++i)
		slow.process(1.f, 0.f, i == 0, dt, 1.f);
	assert(!slow.running() || slow.index <= 33);

	std::printf("scope: all tests passed\n");
	return 0;
}

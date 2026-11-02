// JX-SCP logic: scales, trigger, capture (VCV-style: one buffer, holds without a trigger, min/max per point)
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
	Trigger f;
	fires = 0;
	for (float x : {1.f, 0.5f, -0.5f, -1.f, 1.f})
		fires += f.process(x, 0.f, false);
	assert(fires == 1);
	Trigger first;
	assert(!first.process(5.f, 0.f, true));

	const float fs = 48000.f, dt = 1.f / fs;
	const int mid = 0;  // the trigger point: the left edge, as on VCV's Scope

	// 100 Hz sine, 1 ms/div: every sweep starts on the trigger, which sits at the left edge (rising through 0 V)
	Capture c;
	Trigger tr;
	for (int i = 0; i < 48000; ++i) {
		const float x = 5.f * std::sin(2.f * float(M_PI) * 100.f * i / fs);
		c.process(x, -x, tr.process(x, 0.f, true), dt, 0.001f);
	}
	assert(c.sweeps >= 50 && c.sweeps <= 100);
	assert(std::fabs(c.value[0][mid]) < 0.5f && c.value[0][mid + 4] > c.value[0][mid]);
	assert(near(c.value[1][10], -c.value[0][10]));

	// Slow LFOs, sweeps shorter and longer than the period: the trace is continuous and starts on the trigger
	const float lfoHz[] = {1.f, 1.f, 2.f, 0.5f};
	const float spd[] = {0.01f, 0.2f, 0.1f, 0.5f};
	for (int k = 0; k < 4; ++k) {
		Capture l;
		Trigger lt;
		for (int i = 0; i < 6 * 48000; ++i) {
			const float x = 5.f * std::sin(2.f * float(M_PI) * lfoHz[k] * i / fs);
			l.process(x, 0.f, lt.process(x, 0.f, true), dt, spd[k]);
		}
		assert(std::fabs(l.value[0][mid]) < 0.1f);
		for (int i = 1; i < kPoints; ++i)
			assert(std::fabs(l.value[0][i] - l.value[0][i - 1]) < 0.5f);
	}

	// NORM without a trigger: no sweep, and what was written fades out (the phosphor); a trigger starts one
	Capture hold;
	for (int i = 0; i < 4800; ++i)
		hold.process(2.f, 0.f, false, dt, 0.001f, false, false);
	assert(hold.sweeps == 0 && !hold.running());
	hold.process(2.f, 0.f, true, dt, 0.001f, false, false);
	assert(hold.running() && hold.glow(0) > 0.99f);
	for (int i = 0; i < 48000; ++i)
		hold.process(2.f, 0.f, false, dt, 0.001f, false, false);
	assert(hold.glow(0) < 0.05f);

	// A slow sweep keeps its trail about one sweep long: written 2 s ago at 0.5 s/div (4 s sweep) still glows
	Capture slowGlow;
	slowGlow.process(1.f, 0.f, true, dt, 0.5f, false, false);
	for (int i = 0; i < 2 * 48000; ++i)
		slowGlow.process(1.f, 0.f, false, dt, 0.5f, false, false);
	assert(slowGlow.glow(0) > 0.6f);

	// AUTO without a trigger: the beam runs by itself, so a steady voltage still shows
	Capture autoRun;
	for (int i = 0; i < 9600; ++i)
		autoRun.process(2.f, 0.f, false, dt, 0.001f);
	assert(autoRun.sweeps >= 1 && near(autoRun.value[0][100], 2.f));

	// Free run: sweeps back to back without a trigger
	Capture free;
	for (int i = 0; i < 4800; ++i)
		free.process(1.f, 1.f, false, dt, 0.001f, true);
	assert(free.sweeps >= 5 && near(free.value[0][100], 1.f));

	// Min/max per point: audio at a slow time base shows its full swing in every point, not random samples
	Capture env;
	for (int i = 0; i < 4 * 48000; ++i) {
		const float x = 5.f * std::sin(2.f * float(M_PI) * 440.f * i / fs);
		env.process(x, 0.f, false, dt, 0.1f, true);
	}
	for (int i = 1; i < kPoints; ++i)
		assert(env.hi[0][i] > 4.5f && env.lo[0][i] < -4.5f);

	std::printf("scope: all tests passed\n");
	return 0;
}

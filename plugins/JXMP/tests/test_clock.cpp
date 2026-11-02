// JX-CLK logic: pulse following, gates, swing
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/dsp/Clock.hpp"

using namespace jxmp::clock;

static bool near(double a, double b, double eps = 1e-3) {
	return std::fabs(a - b) <= eps;
}

int main() {
	// 24 PPQN at 120 BPM: a pulse every 1/48 s; after 2 s the position is 4 beats and the tempo 120
	const double fs = 48000.0, dt = 1.0 / fs;
	PulseFollower f;
	int startedCount = 0;
	const int samplesPerPulse = 1000;  // 48000 / 48
	for (int i = 0; i < 2 * 48000; ++i) {
		f.process(i % samplesPerPulse == 0, dt);
		startedCount += f.started;
	}
	assert(f.running && startedCount == 1);
	assert(near(f.bpm(), 120.0, 0.01));
	assert(near(f.beats(), 4.0, 0.03));
	// Between pulses the position moves on smoothly, never past the next pulse
	// (measured from a pulse: after the loop above the position already sits just before the next pulse)
	PulseFollower g = f;
	g.process(true, dt);
	const double b0 = g.beats();
	for (int i = 0; i < samplesPerPulse / 2; ++i)
		g.process(false, dt);
	assert(g.beats() > b0 && g.beats() < b0 + 1.0 / 24);
	// No pulses: the clock stops after a few periods
	for (int i = 0; i < 48000 / 4; ++i)
		f.process(false, dt);
	assert(!f.running && near(f.beats(), 0.0));

	// 4 PPQN (sixteenths): 8 pulses = 2 beats
	PulseFollower s;
	s.ppqn = 4;
	for (int i = 0; i <= 8 * 6000; ++i)
		s.process(i % 6000 == 0, dt);
	assert(near(s.beats(), 2.0, 0.01));

	// Gates: beat output high on the first half of each beat
	assert(gate(0.0, 1.0, 0.5f, false) && gate(0.49, 1.0, 0.5f, false) && !gate(0.5, 1.0, 0.5f, false));
	assert(gate(3.1, 1.0, 0.5f, false) && !gate(-0.1, 1.0, 0.5f, false));
	// /4 = one bar in 4/4: high for two beats
	assert(gate(1.9, 4.0, 0.5f, false) && !gate(2.1, 4.0, 0.5f, false) && gate(4.2, 4.0, 0.5f, false));

	// Swing on sixteenths (period 0.25): straight = second tick at 0.25; 66 % = second tick at 0.33 of the pair
	assert(gate(0.25, 0.25, 0.5f, true));
	assert(!gate(0.30, 0.25, 0.66f, true) && gate(0.34, 0.25, 0.66f, true));
	// The first tick of a pair is never moved
	assert(gate(0.0, 0.25, 0.75f, true) && gate(0.5, 0.25, 0.75f, true));
	// Swing is limited to 75 %, the delayed tick still ends inside its period
	assert(gate(0.49, 0.25, 0.9f, true) && !gate(0.374, 0.25, 0.9f, true));
	// Not swung outputs ignore it
	assert(gate(0.25, 0.25, 0.75f, false));

	assert(kOutputs == 7 && swungOutput(0) && swungOutput(1) && !swungOutput(2));

	std::printf("clock: all tests passed\n");
	return 0;
}

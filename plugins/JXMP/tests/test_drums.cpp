// JX-DRM voices: they sound when triggered, decay to silence, stay in range, accent is louder, closed chokes open
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/dsp/Drums.hpp"

using namespace jxmp::drums;

template <class V, class F>
static float peak(V& v, F run, float seconds, float fs = 48000.f) {
	float p = 0.f;
	for (int i = 0; i < (int) (seconds * fs); ++i)
		p = std::max(p, std::fabs(run(v, 1.f / fs)));
	return p;
}

int main() {
	const float fs = 48000.f;
	auto kickRun = [](Kick& k, float dt) { return k.process(dt, 0.5f, 0.5f, 0.3f); };
	auto snareRun = [](Snare& s, float dt) { return s.process(dt, 0.5f, 0.5f, 0.5f); };
	auto clapRun = [](Clap& c, float dt) { return c.process(dt, 0.5f, 0.5f, 0.5f); };

	// Silent until triggered
	Kick k;
	assert(peak(k, kickRun, 0.1f) < 0.01f);
	// Triggered: loud at first, within ±10 V, silent after a few seconds
	k.trigger(false);
	const float kp = peak(k, kickRun, 0.1f);
	assert(kp > 3.f && kp <= 10.f);
	peak(k, kickRun, 3.f);
	assert(peak(k, kickRun, 0.1f) < 0.05f);
	// Accent is louder
	Kick a;
	a.trigger(true);
	assert(peak(a, kickRun, 0.1f) > kp);

	Snare s;
	s.trigger(false);
	const float sp = peak(s, snareRun, 0.1f);
	assert(sp > 1.f && sp <= 10.f);
	peak(s, snareRun, 3.f);
	assert(peak(s, snareRun, 0.1f) < 0.05f);

	Clap c;
	c.trigger(false);
	const float cp = peak(c, clapRun, 0.2f);
	assert(cp > 0.5f && cp <= 10.f);
	peak(c, clapRun, 3.f);
	assert(peak(c, clapRun, 0.1f) < 0.05f);

	// Hat: an open hat rings longer than a closed one; a closed trigger chokes a ringing open hat
	auto hatRun = [](Hat& h, float dt) { return h.process(dt, 0.5f, 0.6f, 0.5f); };
	Hat open, closed;
	open.trigger(true, false);
	closed.trigger(false, false);
	peak(open, hatRun, 0.15f);
	peak(closed, hatRun, 0.15f);
	assert(peak(open, hatRun, 0.05f) > 10.f * peak(closed, hatRun, 0.05f) + 1e-4f);
	open.trigger(false, false);  // choke: now it is a closed hat
	peak(open, hatRun, 0.2f);
	assert(peak(open, hatRun, 0.05f) < 0.05f);

	(void) fs;
	std::printf("drums: all tests passed\n");
	return 0;
}

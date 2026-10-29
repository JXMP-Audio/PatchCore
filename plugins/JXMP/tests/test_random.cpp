#include "dsp/Random.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

static bool near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) < e; }

int main() {
	RandomCore r;
	const float dt = 1.f / 1000.f;
	// A tick holds the new value at once; the smooth output glides from the old one to it over one period
	r.tick(0.8f);
	assert(near(r.held, 0.8f));
	assert(near(r.smooth(), 0.f));
	for (int i = 0; i < 500; ++i) r.advance(1.f, dt);   // half a period at 1 Hz
	assert(near(r.smooth(), 0.4f, 0.01f));               // cosine glide passes the middle at half time
	for (int i = 0; i < 499; ++i) r.advance(1.f, dt);
	assert(near(r.smooth(), 0.8f, 0.01f));
	// The internal rate ticks once per period
	RandomCore q;
	int ticks = 0;
	for (int i = 0; i < 4100; ++i)  // 4.1 s: away from the boundary of the 8th tick
		if (q.advance(2.f, dt)) ++ticks;
	assert(ticks == 8);
	// The glide never overshoots
	RandomCore g;
	g.tick(1.f);
	float mx = 0.f;
	for (int i = 0; i < 2000; ++i) { g.advance(1.f, dt); mx = std::max(mx, g.smooth()); }
	assert(mx <= 1.0001f);
	std::puts("random: all tests passed");
	return 0;
}

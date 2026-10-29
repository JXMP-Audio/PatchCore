#include "dsp/Noise.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>

struct TestRng {
	uint32_t s = 12345;
	float operator()() {
		s ^= s << 13; s ^= s >> 17; s ^= s << 5;
		return (s & 0xffffff) / float(0x1000000);
	}
};

/** Mean squared first difference relative to the variance: lower = more low-frequency energy */
template <class F>
static float roughness(F gen, int n) {
	double sum = 0, sum2 = 0, diff2 = 0, last = 0;
	for (int i = 0; i < n; ++i) {
		const double v = gen();
		sum += v; sum2 += v * v;
		if (i > 0) diff2 += (v - last) * (v - last);
		last = v;
	}
	const double mean = sum / n, var = sum2 / n - mean * mean;
	return float(diff2 / (n - 1) / var);
}

int main() {
	TestRng rng;
	jxmp::PinkNoise<8> pink;
	float maxAbs = 0.f;
	for (int i = 0; i < 100000; ++i)
		maxAbs = std::max(maxAbs, std::fabs(pink.process(rng)));
	assert(maxAbs <= 4.f);  // 8 values in -0.5..0.5

	TestRng r1, r2;
	jxmp::PinkNoise<8> p2;
	const float white = roughness([&] { return r1() - 0.5f; }, 200000);
	const float pinkR = roughness([&] { return p2.process(r2); }, 200000);
	assert(white > 1.8f && white < 2.2f);  // uncorrelated noise: about 2
	assert(pinkR < 0.5f * white);          // pink is much smoother
	std::puts("noise: all tests passed");
	return 0;
}

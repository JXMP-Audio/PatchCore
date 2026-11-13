/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <algorithm>
#include <cmath>

namespace jxmp {

/** Average of the analogue saw curve −expCurve over a cycle: 9 − 16.875·ln(5/3). The curve alone sits +0.38 above
 * zero (+1.9 V per voice at ±5 V), DC that a chord of unison voices piles up and a reverb then amplifies. */
constexpr float kAnalogSawMean = 0.37981760f;

/** The analogue saw of JX-OSC at x (0..1 within the ramp): Fundamental's exponential curve, centred on 0 V average.
 * OscillatorCore computes the same with SIMD. */
inline float analogSaw(float x) {
	return -(3.f + x * (-13.f + 5.f * x)) / (3.f + 2.f * x) - kAnalogSawMean;
}

inline float morph(float sin, float tri, float saw, float sqr, float m) {
	// NaN passes std::min/max unchanged and (int) NaN is a wild index: a NaN at MORPH crashed the app
	m = m >= 0.f ? std::min(m, 3.f) : 0.f;
	const float v[4] = {sin, tri, saw, sqr};
	const int i = std::min((int) m, 2);
	const float f = m - i;
	return v[i] + (v[i + 1] - v[i]) * f;
}

inline int unisonOffsets(int n, float cents, float* out) {
	n = std::min(std::max(n, 1), 7);
	if (n == 1) {
		out[0] = 0.f;
		return 1;
	}
	for (int i = 0; i < n; ++i)
		out[i] = cents * (2.f * i / (n - 1) - 1.f);
	return n;
}

/** Square sub-oscillator from the main phase: flips on every main phase wrap (÷2), every second wrap for ÷4. */
struct SubDivider {
	float lastPhase = 0.f;
	int count = 0;
	float process(float phase, int octaves) {
		if (phase < lastPhase)
			++count;
		lastPhase = phase;
		const int div = octaves >= 2 ? 2 : 1;
		return ((count / div) & 1) ? -1.f : 1.f;
	}
};

} // namespace jxmp

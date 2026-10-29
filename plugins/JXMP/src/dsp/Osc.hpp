/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <algorithm>
#include <cmath>

namespace jxmp {

inline float morph(float sin, float tri, float saw, float sqr, float m) {
	m = std::min(std::max(m, 0.f), 3.f);
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

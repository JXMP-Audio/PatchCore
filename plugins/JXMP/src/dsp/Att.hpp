/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>

namespace jxmp {

/** in × gain + offset, gain limited to ±2, the result to ±12 V. */
inline float attenuvert(float in, float gain, float offset) {
	gain = std::min(std::max(gain, -2.f), 2.f);
	return std::min(std::max(in * gain + offset, -12.f), 12.f);
}

/** Input of each row: its own socket if patched, else the row above; the first row defaults to 10 V. */
inline void normalInputs(const float in[4], const bool patched[4], float out[4]) {
	float last = 10.f;
	for (int i = 0; i < 4; ++i) {
		if (patched[i])
			last = in[i];
		out[i] = last;
	}
}

} // namespace jxmp

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>

namespace jxmp {

/** Gain from a 0..10 V CV input; an unpatched input means full gain. */
inline float cvGain(float volts, bool connected) {
	return connected ? std::min(std::max(volts / 10.f, 0.f), 1.f) : 1.f;
}

/** Sum of four channels: input × level × CV gain (clamped 0..1), muted channels skipped, then × master. */
inline float mix4(const float in[4], const float level[4], const float cv[4], const bool mute[4], float master) {
	float sum = 0.f;
	for (int i = 0; i < 4; ++i) {
		if (!mute[i])
			sum += in[i] * level[i] * std::min(std::max(cv[i], 0.f), 1.f);
	}
	return sum * master;
}

} // namespace jxmp

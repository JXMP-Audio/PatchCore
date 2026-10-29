/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace jxmp {

/** Slew limiter. Linear: moves at 10 V per `time` seconds. Exponential: one-pole with `time` as its time constant.
`rise` is used when going up, `fall` when going down; a time of 0 follows the input at once. */
struct Slew {
	float value = 0.f;

	float process(float target, float rise, float fall, bool exponential, float dt) {
		const float time = target > value ? rise : fall;
		if (time <= 0.f) {
			value = target;
			return value;
		}
		if (exponential) {
			value += (target - value) * (1.f - std::exp(-dt / time));
		}
		else {
			const float step = 10.f / time * dt;
			value = target > value ? std::min(value + step, target) : std::max(value - step, target);
		}
		return value;
	}
};

} // namespace jxmp

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace jxmp {

/** Sample & hold with a smooth companion: `held` jumps to every new value, `smooth()` glides to it
over one period with a cosine curve (no overshoot). */
struct RandomCore {
	float from = 0.f, to = 0.f, held = 0.f, phase = 0.f;

	void tick(float value) {
		from = smooth();
		to = value;
		held = value;
		phase = 0.f;
	}

	/** Moves time on by `dt` at `freq` ticks per second. With `wrap` (internal rate) it returns true when a
	new tick is due; without it (external clock) the phase just stops at the end of the glide. */
	bool advance(float freq, float dt, bool wrap = true) {
		phase += freq * dt;
		if (wrap && phase >= 1.f) {
			phase -= std::floor(phase);
			return true;
		}
		return false;
	}

	float smooth() const {
		const float p = std::min(std::max(phase, 0.f), 1.f);
		return from + (to - from) * (1.f - std::cos(float(M_PI) * p)) * 0.5f;
	}
};

} // namespace jxmp

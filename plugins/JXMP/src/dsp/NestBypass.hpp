/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-NEST BYPASS, no Rack dependency: the dry input is delayed like the plugin (one 64-frame block), and switching
 * blends the two over 10 ms, so BYPASS never jumps in time or clicks.
 */

#pragma once
#include <algorithm>
#include <cmath>
#include "NestIO.hpp"

namespace jxmp {
namespace nest {

constexpr int kBlockFrames = 64;

/** 0 = the plugin, 1 = the dry input; moves in 10 ms (counted in samples, so it lands exactly) */
struct BypassFade {
	static constexpr float kTime = 0.010f;
	int n = 0, steps = 480;

	float mix() const { return steps > 0 ? (float) n / (float) steps : 0.f; }
	/** One sample. `pluginPlays`: the plugin's output of this block is real (it ran); leaving a full bypass waits
	 * for that, as the resting plugin left nothing to blend with */
	void step(bool on, float sampleTime, bool pluginPlays = true) {
		if (!on && n >= steps && !pluginPlays)
			return;
		const int want = std::max(1, (int) std::lround(kTime / sampleTime));
		if (want != steps) {
			n = (int) std::lround(mix() * want);
			steps = want;
		}
		if (on && n < steps)
			++n;
		else if (!on && n > 0)
			--n;
	}
	/** False only while fully bypassed: then the plugin need not run */
	bool pluginNeeded(bool on) const { return !(on && n >= steps); }
};

inline float blend(float plugin, float dry, float mix) { return plugin + (dry - plugin) * mix; }

/** The input jacks one block late: what goes in at position p of the block comes out there in the next block */
struct DryDelay {
	float buf[kJacks][kBlockFrames] = {};
	float swap(int jack, int pos, float in) {
		const float out = buf[jack][pos];
		buf[jack][pos] = in;
		return out;
	}
};

} // namespace nest
} // namespace jxmp

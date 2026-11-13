/*
 * PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A real pause for the standalone transport: when it stops, the output fades out over 10 ms and then the engine is
 * not run at all (everything freezes where it is: oscillators, envelopes, delay and reverb tails). Play runs the
 * engine again and fades back in over 10 ms. No Rack dependency (tests/test_pause_fade.cpp).
 */

#pragma once

#include <algorithm>

namespace patchcore {

struct PauseFade {
	static constexpr float kSeconds = 0.010f;
	float gain = 1.f;

	/** True when the engine need not run: paused and already faded out */
	bool frozen(bool playing) const { return !playing && gain <= 0.f; }

	/** Gain for the next frame */
	float next(bool playing, float sampleRate) {
		const float step = 1.f / (kSeconds * sampleRate);
		gain = playing ? std::min(1.f, gain + step) : std::max(0.f, gain - step);
		return gain;
	}
};

} // namespace patchcore

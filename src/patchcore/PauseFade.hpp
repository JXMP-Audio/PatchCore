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
#include <cstdint>

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

/** While paused or stopped, a note heard from the Editor (drawn, clicked, dragged) runs the engine, and 2 s after it for
 * its release. The modules meanwhile see a stopped transport (`showStopped`), so nothing moves on (clocks,
 * sequencers, the song position) until the engine is frozen again. During playback it does nothing. */
struct PreviewRun {
	uint32_t frames = 0;  // still to run after the note
	bool showStopped = false;

	/** Before a part: true while the engine should sound for a note */
	bool begin(bool stopped, bool preview, double sampleRate) {
		if (!stopped) {
			frames = 0;
			showStopped = false;
		}
		else if (preview)
			frames = (uint32_t) (sampleRate * 2.0);
		if (stopped && frames > 0)
			showStopped = true;
		return frames > 0;
	}

	/** The engine was frozen again: the modules see the paused transport as before */
	void engine(bool runs) {
		if (!runs)
			showStopped = false;
	}

	/** After a part of `partFrames` */
	void end(bool preview, uint32_t partFrames) {
		if (frames > 0 && !preview)
			frames -= std::min(frames, partFrames);
	}
};

} // namespace patchcore

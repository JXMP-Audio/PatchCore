/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cmath>

namespace jxmp {

/** Step position of a sequencer. After a reset (and at start) the next clock plays the first step. */
struct StepCounter {
	int step = 0;
	bool armed = true;

	void clock(int length) {
		if (length < 1)
			length = 1;
		if (armed) {
			armed = false;
			step = 0;
			return;
		}
		// Past the end (also when the length was just shortened below the current step): back to the first step
		step = step + 1 >= length ? 0 : step + 1;
	}

	void reset() {
		step = 0;
		armed = true;
	}
};

/** Square clock at `bpm` beats per minute, high for the first half of each beat. Starts high. */
struct InternalClock {
	float phase = 0.f;

	bool process(float bpm, float dt) {
		const bool high = phase < 0.5f;
		phase += bpm / 60.f * dt;
		phase -= std::floor(phase);
		return high;
	}
};

} // namespace jxmp

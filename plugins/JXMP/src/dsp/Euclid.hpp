/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-EUC building blocks: Euclidean rhythms (hits spread as evenly as possible over the steps) with rotation,
 * and a track that steps on a clock. No Rack dependency, tested in tests/test_euclid.cpp.
 */

#pragma once

#include <algorithm>

namespace jxmp {
namespace euclid {

constexpr int kMaxSteps = 16;

/** True when `step` of a pattern with `hits` over `steps`, shifted later by `rotate` steps, is a hit. */
inline bool hit(int step, int steps, int hits, int rotate) {
	steps = std::min(std::max(steps, 1), kMaxSteps);
	hits = std::min(std::max(hits, 0), steps);
	if (hits == 0)
		return false;
	const int i = ((step - rotate) % steps + steps) % steps;
	// Bresenham: the same spacing as Bjorklund's algorithm, starting with a hit
	return (i * hits) % steps < hits;
}

/** The step that carries the accent: the first hit of the unrotated pattern (step 0), shifted by `rotate`. */
inline int accentStep(int steps, int rotate) {
	steps = std::min(std::max(steps, 1), kMaxSteps);
	return ((rotate % steps) + steps) % steps;
}

/** One track: moves one step per clock; after a reset the next clock plays step 0. */
struct Track {
	int step = -1;  // -1 = reset, waiting for the first clock

	void reset() {
		step = -1;
	}

	/** A step saved with the patch, kept inside the possible range */
	void restore(int s) {
		step = std::min(std::max(s, -1), kMaxSteps - 1);
	}

	void clock(int steps) {
		steps = std::min(std::max(steps, 1), kMaxSteps);
		step = (step + 1) % steps;
	}

	/** The current step, kept inside a length that may have shrunk */
	int current(int steps) const {
		steps = std::min(std::max(steps, 1), kMaxSteps);
		return step < 0 ? -1 : step % steps;
	}
};

} // namespace euclid
} // namespace jxmp

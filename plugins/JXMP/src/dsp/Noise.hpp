/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Based on VCV Fundamental (GPL-3.0-or-later), src/Noise.cpp, Copyright (C) VCV and contributors.
 * Pink noise by the Voss algorithm (http://www.firstpr.com.au/dsp/pink-noise/), with the random
 * source passed in so it can be tested without Rack.
 */

#pragma once

namespace jxmp {

template <int QUALITY = 8>
struct PinkNoise {
	int frame = -1;
	float values[QUALITY] = {};

	/** `uniform()` returns 0..1. Output range ±QUALITY/2. */
	template <class Uniform>
	float process(Uniform& uniform) {
		const int lastFrame = frame;
		frame++;
		if (frame >= (1 << QUALITY))
			frame = 0;
		const int diff = lastFrame ^ frame;
		float sum = 0.f;
		for (int i = 0; i < QUALITY; i++) {
			if (diff & (1 << i))
				values[i] = uniform() - 0.5f;
			sum += values[i];
		}
		return sum;
	}
};

} // namespace jxmp

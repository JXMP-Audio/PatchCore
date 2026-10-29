/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cmath>

namespace jxmp {

enum Scale {
	SCALE_CHROMATIC = 0,
	SCALE_MAJOR,
	SCALE_MINOR,
	SCALE_DORIAN,
	SCALE_MIXOLYDIAN,
	SCALE_HARMONIC_MINOR,
	SCALE_MAJOR_PENTA,
	SCALE_MINOR_PENTA,
	SCALE_BLUES,
	SCALE_WHOLE_TONE,
	NUM_SCALES
};

inline const char* scaleName(Scale s) {
	static const char* const names[NUM_SCALES] = {"Chromatic", "Major", "Minor", "Dorian", "Mixolydian",
	                                              "Harmonic minor", "Major pentatonic", "Minor pentatonic", "Blues", "Whole tone"};
	return names[s];
}

/** Bit i set = the scale has the note i semitones above its root. */
inline unsigned scaleMask(Scale s) {
	static const unsigned masks[NUM_SCALES] = {
		0xfff,
		(1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),
		(1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),
		(1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),
		(1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),
		(1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),
		(1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),
		(1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),
		(1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),
		(1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),
	};
	return masks[s];
}

/** True when the absolute note (semitones from C) is in the scale on `root` (0 = C .. 11 = B). */
inline bool inScale(int note, Scale s, int root) {
	const int rel = ((note - root) % 12 + 12) % 12;
	return (scaleMask(s) >> rel) & 1;
}

/** Nearest note of the scale to a V/oct voltage (0 V = C4); ties go to the lower note. */
inline float quantize(float volts, Scale s, int root) {
	const float semis = volts * 12.f;
	const int base = (int) std::floor(semis);
	int best = base;
	float bestDist = 1e9f;
	for (int n = base - 6; n <= base + 7; ++n) {
		if (!inScale(n, s, root))
			continue;
		const float dist = std::fabs(semis - (float) n);
		if (dist < bestDist - 1e-6f) {
			bestDist = dist;
			best = n;
		}
	}
	return best / 12.f;
}

/** Note (0 = C .. 11 = B) of a V/oct voltage, for the note LEDs. */
inline int noteOf(float volts) {
	const int n = (int) std::lround(volts * 12.f);
	return (n % 12 + 12) % 12;
}

} // namespace jxmp

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-FLD and JX-RNG building blocks: a triangle wavefolder with symmetry, a ring modulator with dry/wet, and
 * the logic and stepping of JX-LGC and JX-SW. No Rack dependency, tested in tests/test_shapers.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace jxmp {
namespace shapers {

/**
 * Wavefolder: the input (±5 V nominal) is amplified by 1 … 10 (FOLD 0 … 1), shifted by SYMMETRY (−1 … 1) and
 * folded back whenever it leaves ±5 V, so it stays within ±5 V and gains harmonics.
 */
inline float fold(float x, float amount, float symmetry) {
	float v = x / 5.f * (1.f + 9.f * std::min(std::max(amount, 0.f), 1.f)) + symmetry;
	// Triangle reflection into -1..1
	v = std::fmod(v + 1.f, 4.f);
	if (v < 0.f)
		v += 4.f;
	v = v < 2.f ? v - 1.f : 3.f - v;
	return 5.f * v;
}

/** Ring modulator: the product, scaled so two 5 V signals give 5 V; MIX 0 = A dry, 1 = the product */
inline float ring(float a, float b, float mix) {
	mix = std::min(std::max(mix, 0.f), 1.f);
	return a * (1.f - mix) + (a * b / 5.f) * mix;
}

/** Gate logic of JX-LGC (high above 1 V) */
struct Gates {
	bool a, b;
	bool andGate() const { return a && b; }
	bool orGate() const { return a || b; }
	bool xorGate() const { return a != b; }
};

/** Sequential switch of JX-SW: steps 0 … length-1 on each clock, back to 0 on reset */
struct Stepper {
	int step = 0;
	void clock(int length) {
		length = std::min(std::max(length, 2), 4);
		step = (step + 1) % length;
	}
	void reset() {
		step = 0;
	}
	/** A step saved with the patch, kept inside 0..3 */
	void restore(int s) {
		step = std::min(std::max(s, 0), 3);
	}
	int current(int length) const {
		return step % std::min(std::max(length, 2), 4);
	}
};

} // namespace shapers
} // namespace jxmp

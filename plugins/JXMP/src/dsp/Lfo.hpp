/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LFO shapes and rate. Behaviour follows VCV Fundamental's LFO (GPL-3.0-or-later).
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace jxmp {

enum LfoShape { LFO_SIN = 0, LFO_TRI, LFO_SAW, LFO_SQR };

/** Bipolar ±1 shape at `phase` (0..1). Sine, triangle and saw start at 0 going up. */
inline float lfoShape(LfoShape shape, float phase, float pw) {
	phase -= std::floor(phase);
	switch (shape) {
	case LFO_SIN:
		return std::sin(2.f * float(M_PI) * phase);
	case LFO_TRI:
		return 1.f - 4.f * std::min(std::fabs(phase - 0.25f), std::fabs(phase - 1.25f));
	case LFO_SAW: {
		float x = phase + 0.5f;
		x -= std::floor(x);
		return 2.f * x - 1.f;
	}
	default:
		return phase < pw ? 1.f : -1.f;
	}
}

/** Output shape: unipolar (0..1, starting at the bottom) or bipolar, optionally inverted. */
inline float lfoOut(LfoShape shape, float phase, float pw, bool unipolar, bool invert) {
	if (!unipolar)
		return invert ? -lfoShape(shape, phase, pw) : lfoShape(shape, phase, pw);
	// Unipolar: shift by a quarter cycle so the curve starts at its lowest point, then lift to 0..1
	const float p = shape == LFO_SQR ? phase : phase - 0.25f;
	float v = lfoShape(shape, p, pw);
	if (invert)
		v = -v;
	return (v + 1.f) * 0.5f;
}

/** Rate in Hz: 2^param, scaled by an external clock (default 2 Hz = no clock). */
inline float lfoFreq(float param, float clockFreq) {
	return clockFreq / 2.f * std::pow(2.f, param);
}

/** Advances the phase by freq·dt (at most half a cycle) and wraps it into 0..1. */
inline float lfoAdvance(float phase, float freq, float dt) {
	phase += std::min(freq * dt, 0.5f);
	return phase - std::floor(phase);
}

} // namespace jxmp

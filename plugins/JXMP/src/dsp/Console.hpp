/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-CONSOLE building blocks: fader law, constant-power pan, mute/solo, three-band EQ (RBJ biquads) and VU
 * ballistics. No Rack dependency, tested in tests/test_console.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace jxmp {
namespace console {

/** Fader position 0..1 to dB: off at 0, −60 dB just above it, 0 dB at 3/4 of the travel, +6 dB at the top. */
inline float faderDb(float pos) {
	if (pos <= 0.f)
		return -std::numeric_limits<float>::infinity();
	if (pos >= 0.75f)
		return std::min(pos, 1.f) * 24.f - 18.f;  // 0.75 → 0 dB, 1 → +6 dB
	const float t = 1.f - pos / 0.75f;
	return -60.f * t * t;
}

/** Inverse of faderDb. */
inline float faderPos(float db) {
	if (db <= -60.f)
		return 0.f;
	if (db >= 0.f)
		return std::min(0.75f + db / 24.f, 1.f);
	return 0.75f * (1.f - std::sqrt(-db / 60.f));
}

inline float dbToGain(float db) {
	return std::pow(10.f, db / 20.f);
}

inline float faderGain(float pos) {
	return pos <= 0.f ? 0.f : dbToGain(faderDb(pos));
}

/** Constant-power pan, −1 hard left .. +1 hard right. */
inline void panGains(float pan, float* left, float* right) {
	const float a = (std::min(std::max(pan, -1.f), 1.f) + 1.f) * (float) M_PI / 4.f;
	*left = std::cos(a);
	*right = std::sin(a);
}

/** A muted channel is silent; when any channel is soloed, only the soloed ones play. */
inline bool audible(bool mute, bool solo, bool anySolo) {
	return !mute && (!anySolo || solo);
}

/** Mid EQ frequency knob 0..1 → 200 Hz .. 5 kHz, logarithmic. */
inline float midFreq(float t) {
	return 200.f * std::pow(25.f, std::min(std::max(t, 0.f), 1.f));
}

/** Biquad in direct form II transposed, coefficients after the RBJ audio EQ cookbook (a0 normalised to 1). */
struct Biquad {
	double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
	double z1 = 0.0, z2 = 0.0;

	float process(float x) {
		const double y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return (float) y;
	}

	void set(double nb0, double nb1, double nb2, double a0, double na1, double na2) {
		b0 = nb0 / a0;
		b1 = nb1 / a0;
		b2 = nb2 / a0;
		a1 = na1 / a0;
		a2 = na2 / a0;
	}

	void setPeak(float fs, float f, float q, float db) {
		const double A = std::pow(10.0, db / 40.0), w = 2.0 * M_PI * f / fs;
		const double cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
		set(1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
	}

	/** Shelves with slope S = 1. */
	void setLowShelf(float fs, float f, float db) {
		const double A = std::pow(10.0, db / 40.0), w = 2.0 * M_PI * f / fs;
		const double cw = std::cos(w), k = 2.0 * std::sqrt(A) * std::sin(w) / 2.0 * std::sqrt(2.0);
		set(A * ((A + 1) - (A - 1) * cw + k), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - k),
		    (A + 1) + (A - 1) * cw + k, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - k);
	}

	void setHighShelf(float fs, float f, float db) {
		const double A = std::pow(10.0, db / 40.0), w = 2.0 * M_PI * f / fs;
		const double cw = std::cos(w), k = 2.0 * std::sqrt(A) * std::sin(w) / 2.0 * std::sqrt(2.0);
		set(A * ((A + 1) + (A - 1) * cw + k), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - k),
		    (A + 1) - (A - 1) * cw + k, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - k);
	}
};

/** Rectified-average VU meter. 0 VU is a 5 V sine (Rack's usual audio level); reaches ~90 % in 300 ms. */
struct Vu {
	float level = 0.f;

	void process(float x, float dt) {
		const float tau = 0.13f;
		level += (std::fabs(x) - level) * std::min(dt / tau, 1.f);
	}

	float vu() const {
		const float ref = 5.f * 2.f / (float) M_PI;
		return level > 0.f ? std::max(20.f * std::log10(level / ref), -60.f) : -60.f;
	}
};

/** Needle position 0..1 on a −20..+3 VU scale. Like a real meter it is linear in voltage: 0 VU sits at ~71 %. */
inline float vuNeedle(float vu) {
	return std::min(std::max(dbToGain(vu) / dbToGain(3.f), 0.f), 1.f);
}

} // namespace console
} // namespace jxmp

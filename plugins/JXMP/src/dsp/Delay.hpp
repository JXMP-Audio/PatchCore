/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace jxmp {

/** Delay time in seconds for a 0..1 knob: 1 ms .. 10 s, 100 ms in the middle. */
inline float delayTime(float knob01) {
	return 0.001f * std::pow(10000.f, std::min(std::max(knob01, 0.f), 1.f));
}

/** Feedback loop limit: untouched up to ±6 V, then bends softly towards ±10 V (like tape running hot), so a high
FEEDBACK builds up into a sustained, warm repeat instead of growing until the module output clips hard. */
inline float loopLimit(float x) {
	const float a = std::fabs(x);
	if (a <= 6.f)
		return x;
	const float y = 6.f + 4.f * std::tanh((a - 6.f) / 4.f);
	return x < 0.f ? -y : y;
}

/** Mono delay line with feedback, a tone filter in the repeats and a dry/wet mix.
The delay time glides towards a new value, so turning TIME bends the pitch like tape instead of clicking.
The feedback path has a DC blocker (~10 Hz) and the soft limit above, so neither DC nor the level piles up. */
struct DelayCore {
	std::vector<float> buf;
	size_t w = 0;
	float smoothed = -1.f;
	float lowState = 0.f, bassState = 0.f;
	/** DC blocker in the feedback path */
	float dcIn = 0.f, dcOut = 0.f;
	/** Wet signal (the repeats after the tone filter) of the last sample */
	float lastWet = 0.f;

	void init(size_t size) {
		buf.assign(std::max<size_t>(size, 4), 0.f);
		w = 0;
		smoothed = -1.f;
		lowState = bassState = 0.f;
		dcIn = dcOut = 0.f;
	}

	float read(float delaySamples) const {
		const float maxDelay = (float) buf.size() - 2.f;
		const float d = std::min(std::max(delaySamples, 1.f), maxDelay);
		float pos = (float) w - d;
		while (pos < 0.f)
			pos += (float) buf.size();
		const size_t i0 = (size_t) pos;
		const size_t i1 = (i0 + 1) % buf.size();
		const float frac = pos - (float) i0;
		return buf[i0] + (buf[i1] - buf[i0]) * frac;
	}

	/** `tone` −1 (dark, lowpass) .. 0 (neutral) .. +1 (thin, highpass). `mix` 0 = dry, 1 = wet. */
	float process(float in, float delaySamples, float feedback, float tone, float mix, float dt) {
		if (smoothed < 0.f)
			smoothed = delaySamples;
		else
			smoothed += (delaySamples - smoothed) * std::min(1.f, dt * 12.f);

		const float wet = read(smoothed);

		// Tone: crossfade towards a lowpass (≈1.5 kHz) or away from the bass (≈200 Hz)
		const float kLow = 1.f - std::exp(-2.f * float(M_PI) * 1500.f * dt);
		const float kBass = 1.f - std::exp(-2.f * float(M_PI) * 200.f * dt);
		lowState += (wet - lowState) * kLow;
		bassState += (wet - bassState) * kBass;
		float y = wet;
		if (tone < 0.f)
			y = wet + (lowState - wet) * std::min(-tone, 1.f);
		else if (tone > 0.f)
			y = wet - bassState * std::min(tone, 1.f);

		lastWet = y;
		const float fb = std::min(std::max(feedback, 0.f), 1.f);
		// DC blocker on the repeats that go round again (the wet output keeps them as they are)
		const float r = 1.f - 2.f * float(M_PI) * 10.f * dt;
		dcOut = 0.5f * (1.f + r) * (y - dcIn) + r * dcOut;  // gain 1 at the top, never above (100 % feedback stays)
		dcIn = y;
		const float back = loopLimit(in + fb * dcOut);
		buf[w] = std::isfinite(back) ? back : 0.f;
		w = (w + 1) % buf.size();
		return in + (y - in) * std::min(std::max(mix, 0.f), 1.f);
	}
};

} // namespace jxmp

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-CHR: chorus (two voices, one per side, a quarter of a cycle apart) and ensemble (three voices per side a
 * third apart plus a little fast vibrato, as string machines did). Modulated delay lines with linear
 * interpolation. No Rack dependency, tested in tests/test_chorus.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace jxmp {
namespace chorus {

struct DelayLine {
	std::vector<float> buf;
	int pos = 0;

	void resize(int n) {
		buf.assign(std::max(n, 4), 0.f);
		pos = 0;
	}
	void write(float x) {
		buf[pos] = x;
		pos = (pos + 1) % (int) buf.size();
	}
	/** The sample `delay` samples ago (fractional, at least 1) */
	float read(float delay) const {
		const int n = (int) buf.size();
		delay = std::min(std::max(delay, 1.f), (float) (n - 2));
		// the sample just written is 0 samples ago, at pos - 1
		const float p = pos - 1 - delay;
		const int i = (int) std::floor(p);
		const float f = p - i;
		const float a = buf[(i % n + n) % n], b = buf[((i + 1) % n + n) % n];
		return a + (b - a) * f;
	}
};

struct Chorus {
	float sampleRate = 0.f;
	DelayLine left, right;
	float phase = 0.f, vibrato = 0.f;
	float baseMs = -1.f;  // glides 7 <-> 10 ms when the mode changes (a jump would click)
	float ensembleMix = -1.f;  // 0 chorus … 1 ensemble, fades in 50 ms: the two modes sound different, a switch clicked

	void setSampleRate(float fs) {
		if (fs == sampleRate)
			return;
		sampleRate = fs;
		left.resize((int) (fs * 0.04f));
		right.resize((int) (fs * 0.04f));
	}

	/** rate Hz, depth 0..1, ensemble: three voices per side and vibrato. Returns the wet stereo signal. */
	void process(float inL, float inR, float rate, float depth, bool ensemble, float* outL, float* outR) {
		const float dt = 1.f / sampleRate;
		phase += rate * dt;
		phase -= std::floor(phase);
		vibrato += 6.f * dt;
		vibrato -= std::floor(vibrato);
		// A NaN would stay in the delay line for 40 ms
		left.write(std::isfinite(inL) ? inL : 0.f);
		right.write(std::isfinite(inR) ? inR : 0.f);
		const float twoPi = 2.f * float(M_PI);
		const float target = ensemble ? 10.f : 7.f, swingMs = 5.f * depth;
		baseMs = baseMs < 0.f ? target : baseMs + (target - baseMs) * std::min(1.f, dt / 0.05f);
		const float mixTarget = ensemble ? 1.f : 0.f;
		ensembleMix = ensembleMix < 0.f ? mixTarget
		                                : ensembleMix + std::max(-dt / 0.05f, std::min(dt / 0.05f, mixTarget - ensembleMix));
		const float vib = 0.15f * depth * std::sin(twoPi * vibrato) * ensembleMix;
		auto delaySamples = [&](float ph) {
			return (baseMs + swingMs * (0.5f + 0.5f * std::sin(twoPi * ph)) + vib) * 0.001f * sampleRate;
		};
		float cl = 0.f, cr = 0.f, el = 0.f, er = 0.f;
		if (ensembleMix < 1.f) {
			cl = left.read(delaySamples(phase));
			cr = right.read(delaySamples(phase + 0.25f));
		}
		if (ensembleMix > 0.f) {
			for (int v = 0; v < 3; ++v) {
				el += left.read(delaySamples(phase + v / 3.f));
				er += right.read(delaySamples(phase + v / 3.f + 1.f / 6.f));
			}
			el /= 3.f;
			er /= 3.f;
		}
		*outL = cl + (el - cl) * ensembleMix;
		*outR = cr + (er - cr) * ensembleMix;
	}
};

/** Equal-power dry/wet */
inline void mix(float dry, float wet, float amount, float* out) {
	*out = dry * std::cos(amount * float(M_PI) / 2) + wet * std::sin(amount * float(M_PI) / 2);
}

} // namespace chorus
} // namespace jxmp

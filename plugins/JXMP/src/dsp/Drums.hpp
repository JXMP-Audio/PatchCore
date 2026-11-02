/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-DRM voices in the style of analogue drum machines: a kick (sine with a pitch sweep), a snare (two tuned
 * bodies and noise), a hi-hat (six detuned squares, high-passed; open and closed, closed chokes open) and a clap
 * (noise bursts with a tail). Each: trigger(accent), process(dt, tune 0..1, decay 0..1, tone 0..1) → ±5 V
 * nominal. No Rack dependency, tested in tests/test_drums.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jxmp {
namespace drums {

/** Small deterministic noise source, -1..1 */
struct Noise {
	uint32_t state = 0x9e3779b9u;
	float next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return (state & 0xffffff) / float(0x800000) - 1.f;
	}
};

/** Exponential decay: multiplier per sample for `seconds` to fall to 1/1000 (−60 dB) */
inline float decayCoef(float seconds, float dt) {
	return std::exp(-6.9078f * dt / std::max(seconds, 1e-4f));
}

/** One-pole high-pass */
struct HighPass {
	float lastIn = 0.f, out = 0.f;
	float process(float x, float cutoff, float dt) {
		const float rc = 1.f / (2.f * float(M_PI) * cutoff);
		const float a = rc / (rc + dt);
		out = a * (out + x - lastIn);
		lastIn = x;
		return out;
	}
};

/** Two-pole band-pass (state-variable) */
struct BandPass {
	float low = 0.f, band = 0.f;
	float process(float x, float freq, float q, float dt) {
		const float f = std::min(2.f * std::sin(float(M_PI) * std::min(freq * dt, 0.24f)), 1.2f);
		low += f * band;
		const float high = x - low - band / q;
		band += f * high;
		return band;
	}
};

struct Kick {
	float phase = 0.f, amp = 0.f, pitchEnv = 0.f, gain = 1.f;

	void trigger(bool accent) {
		amp = 1.f;
		pitchEnv = 1.f;
		gain = accent ? 1.4f : 1.f;
	}

	/** tune: 35..90 Hz; decay: 0.08..1.2 s; tone: click and drive */
	float process(float dt, float tune, float decay, float tone) {
		const float base = 35.f + 55.f * tune;
		const float freq = base * (1.f + 3.f * pitchEnv);
		pitchEnv *= decayCoef(0.035f, dt);
		amp *= decayCoef(0.08f + 1.12f * decay * decay, dt);
		phase += freq * dt;
		phase -= std::floor(phase);
		const float s = std::sin(2.f * float(M_PI) * phase) * amp;
		const float drive = 1.f + 3.f * tone;
		return 5.f * gain * std::tanh(s * drive) / std::tanh(drive);
	}
};

struct Snare {
	float p1 = 0.f, p2 = 0.f, body = 0.f, snap = 0.f, gain = 1.f;
	Noise noise;
	HighPass hp;

	void trigger(bool accent) {
		body = snap = 1.f;
		gain = accent ? 1.4f : 1.f;
	}

	/** tune: body 150..300 Hz; decay: 0.05..0.6 s (noise); tone: body ↔ noise */
	float process(float dt, float tune, float decay, float tone) {
		const float f = 150.f + 150.f * tune;
		p1 += f * dt;
		p1 -= std::floor(p1);
		p2 += f * 1.47f * dt;
		p2 -= std::floor(p2);
		body *= decayCoef(0.09f, dt);
		snap *= decayCoef(0.05f + 0.55f * decay, dt);
		const float b = (std::sin(2.f * float(M_PI) * p1) + 0.5f * std::sin(2.f * float(M_PI) * p2)) * body;
		const float n = hp.process(noise.next(), 1200.f, dt) * snap;
		return 5.f * gain * std::max(-1.2f, std::min(1.2f, (1.f - tone) * b * 0.8f + (0.3f + tone) * n));
	}
};

struct Hat {
	float phases[6] = {};
	float amp = 0.f, decaySeconds = 0.05f, gain = 1.f;
	HighPass hp1, hp2;

	/** Closed hats are short and choke an open one; open hats ring for DECAY */
	void trigger(bool open, bool accent) {
		amp = 1.f;
		decaySeconds = open ? -1.f : 0.f;  // set from DECAY in process
		gain = accent ? 1.4f : 1.f;
	}

	/** tune: metal pitch; decay: open hat length 0.15..1.2 s (closed 0.03..0.09 s); tone: brightness */
	float process(float dt, float tune, float decay, float tone) {
		static const float ratios[6] = {1.f, 1.304f, 1.466f, 1.787f, 1.932f, 2.536f};
		const float base = 300.f + 400.f * tune;
		float sum = 0.f;
		for (int i = 0; i < 6; ++i) {
			phases[i] += base * ratios[i] * dt;
			phases[i] -= std::floor(phases[i]);
			sum += phases[i] < 0.5f ? 1.f : -1.f;
		}
		const float seconds = decaySeconds < 0.f ? 0.15f + 1.05f * decay : 0.03f + 0.06f * decay;
		amp *= decayCoef(seconds, dt);
		const float cutoff = 5000.f + 4000.f * tone;
		const float x = hp2.process(hp1.process(sum / 6.f, cutoff, dt), cutoff, dt);
		return 5.f * gain * std::max(-1.2f, std::min(1.2f, x * 2.5f * amp));
	}
};

struct Clap {
	float t = 10.f, tail = 0.f, gain = 1.f;
	Noise noise;
	BandPass bp;

	void trigger(bool accent) {
		t = 0.f;
		tail = 1.f;
		gain = accent ? 1.4f : 1.f;
	}

	/** tune: band 800..2000 Hz; decay: tail 0.1..0.8 s; tone: band width */
	float process(float dt, float tune, float decay, float tone) {
		t = std::min(t + dt, 10.f);  // stays where float steps are fine (the tail is long over by then)
		// Three short bursts 10 ms apart, then the tail
		float env = 0.f;
		for (int i = 0; i < 3; ++i) {
			const float s = t - i * 0.01f;
			if (s >= 0.f && s < 0.01f)
				env = std::max(env, 1.f - s / 0.01f);
		}
		if (t >= 0.03f) {
			tail *= decayCoef(0.1f + 0.7f * decay, dt);
			env = std::max(env, 0.7f * tail);
		}
		const float x = bp.process(noise.next(), 800.f + 1200.f * tune, 0.8f + 3.f * (1.f - tone), dt);
		return 5.f * gain * std::max(-1.2f, std::min(1.2f, x * 1.6f * env));
	}
};

} // namespace drums
} // namespace jxmp

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <algorithm>
#include <cmath>

namespace jxmp {

/** Monotonic 0..1 → 0..1. shape −1 = linear, 0 = Fundamental-like exponential approach, +1 = steeper. */
inline float curve(float x, float shape) {
	x = std::min(std::max(x, 0.f), 1.f);
	const float k = 4.f * (1.f + std::min(std::max(shape, -1.f), 1.f));
	if (k < 0.01f)
		return x;
	return (1.f - std::exp(-k * x)) / (1.f - std::exp(-k));
}

struct Envelope {
	enum Mode { ADSR, AD, LOOP };
	enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };

	Stage stage = IDLE;
	float value = 0.f;
	bool eoc = false;

	float process(float dt, bool gate, bool retrig, bool gateConnected, float a, float d, float s, float r, float shape, Mode mode) {
		eoc = false;
		const bool run = gate || (mode == LOOP && !gateConnected);
		if ((run && !lastRun) || retrig)
			start(ATTACK);
		lastRun = run;

		const float minTime = 1e-3f;
		switch (stage) {
		case ATTACK:
			advance(dt, std::max(a, minTime));
			value = from + (1.f - from) * curve(phase, shape);
			if (phase >= 1.f)
				start(DECAY);
			break;
		case DECAY: {
			const float target = mode == ADSR ? s : 0.f;
			advance(dt, std::max(d, minTime));
			value = from + (target - from) * curve(phase, shape);
			if (phase >= 1.f) {
				if (mode == ADSR)
					stage = SUSTAIN;
				else if (mode == LOOP && run) {
					eoc = true;
					start(ATTACK);
				}
				else {
					eoc = true;
					stage = IDLE;
					value = 0.f;
				}
			}
			break;
		}
		case SUSTAIN:
			value = s;
			break;
		case RELEASE:
			advance(dt, std::max(r, minTime));
			value = from * (1.f - curve(phase, shape));
			if (phase >= 1.f) {
				eoc = true;
				stage = IDLE;
				value = 0.f;
			}
			break;
		default:
			value = 0.f;
			break;
		}
		if (!run && mode == ADSR && (stage == ATTACK || stage == DECAY || stage == SUSTAIN))
			start(RELEASE);
		return value;
	}

private:
	float phase = 0.f;
	float from = 0.f;
	bool lastRun = false;

	void start(Stage st) {
		stage = st;
		phase = 0.f;
		from = value;
	}
	void advance(float dt, float time) {
		phase = std::min(phase + dt / time, 1.f);
	}
};

} // namespace jxmp

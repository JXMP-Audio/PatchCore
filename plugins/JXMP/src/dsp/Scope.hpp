/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SCP building blocks: stepped time and volt scales, a trigger with hysteresis, and the sweep capture
 * (after VCV Fundamental's Scope: one buffer, min/max per point, the picture holds without a trigger). No Rack dependency, tested in
 * tests/test_scope.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace jxmp {
namespace scope {

/** Seconds per division, the TIME/DIV knob steps */
static const float kTimeDiv[] = {0.0001f, 0.0002f, 0.0005f, 0.001f, 0.002f, 0.005f, 0.01f,
                                 0.02f,   0.05f,   0.1f,    0.2f,   0.5f,   1.f};
constexpr int kTimeSteps = sizeof(kTimeDiv) / sizeof(kTimeDiv[0]);
/** Volts per division, the VOLTS/DIV knob steps */
static const float kVoltDiv[] = {0.1f, 0.2f, 0.5f, 1.f, 2.f, 5.f};
constexpr int kVoltSteps = sizeof(kVoltDiv) / sizeof(kVoltDiv[0]);

/** Horizontal divisions across the screen, and points per sweep */
constexpr int kDivisions = 8;
constexpr int kPoints = 256;

inline float timeDiv(int step) {
	return kTimeDiv[std::min(std::max(step, 0), kTimeSteps - 1)];
}

inline float voltDiv(int step) {
	return kVoltDiv[std::min(std::max(step, 0), kVoltSteps - 1)];
}

/** Level crossing with hysteresis (0.05 V around the level), so noise on a slow edge fires once. */
struct Trigger {
	bool above = false;
	bool primed = false;

	/** True on the crossing in the chosen direction. */
	bool process(float x, float level, bool rising) {
		const float hyst = 0.05f;
		const bool wasAbove = above;
		if (x > level + hyst)
			above = true;
		else if (x < level - hyst)
			above = false;
		if (!primed) {
			primed = true;
			return false;
		}
		return rising ? (above && !wasAbove) : (!above && wasAbove);
	}
};

/**
 * The capture, as VCV Fundamental's Scope does it: one buffer of kPoints points that the display draws live.
 * A sweep starts on a trigger (or right away in free run) at the left edge and overwrites the buffer point by
 * point; without a trigger the buffer stays — the picture holds. Each point keeps the minimum and maximum of
 * the samples in its time slice, so slow time bases do not alias. `value` is the last sample of each point.
 */
struct Capture {
	float lo[2][kPoints] = {}, hi[2][kPoints] = {}, value[2][kPoints] = {};
	int sweeps = 0;
	int index = kPoints;  // kPoints = holding, waiting for a trigger
	float sinceTake = 0.f;
	float curLo[2] = {INFINITY, INFINITY}, curHi[2] = {-INFINITY, -INFINITY};

	bool running() const {
		return index < kPoints;
	}

	/** Stops the sweep (e.g. the time base changed): the next trigger starts a fresh one */
	void restart() {
		index = kPoints;
	}

	/** One audio sample. `freeRun`: sweeps back to back without a trigger (FREE mode, X–Y). */
	void process(float a, float b, bool trigger, float dt, float secondsPerDiv, bool freeRun = false) {
		const float x[2] = {a, b};
		if (!running()) {
			if (!(trigger || freeRun))
				return;
			index = 0;
			sinceTake = 0.f;
			for (int ch = 0; ch < 2; ++ch) {
				curLo[ch] = INFINITY;
				curHi[ch] = -INFINITY;
			}
		}
		const float step = secondsPerDiv * kDivisions / kPoints;
		for (int ch = 0; ch < 2; ++ch) {
			curLo[ch] = std::min(curLo[ch], x[ch]);
			curHi[ch] = std::max(curHi[ch], x[ch]);
		}
		sinceTake += dt;
		// The first point is the trigger sample; then one point per time step (at most one per sample)
		if (index > 0 && sinceTake < step)
			return;
		sinceTake = index > 0 ? std::min(sinceTake - step, step) : 0.f;
		for (int ch = 0; ch < 2; ++ch) {
			lo[ch][index] = curLo[ch];
			hi[ch][index] = curHi[ch];
			value[ch][index] = x[ch];
			curLo[ch] = INFINITY;
			curHi[ch] = -INFINITY;
		}
		if (++index >= kPoints)
			++sweeps;
	}
};

/** Screen position of a voltage in divisions from the centre: v / (V/div) + position (in divisions). */
inline float divisions(float volts, float voltsPerDiv, float positionDiv) {
	return volts / voltsPerDiv + positionDiv;
}

} // namespace scope
} // namespace jxmp

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
 * The capture, after VCV's Scope: a sweep starts on a trigger (or right away in free run); without a trigger
 * the picture holds. The display shows only complete sweeps (`shown*`, swapped in whole), so the trigger is
 * always in the middle and the screen never shows half a new sweep over half an old one. Each point keeps the minimum and maximum of the samples in its
 * time slice, so slow time bases do not alias. The trigger point is the middle of the sweep: the half before
 * it comes from a history that runs all the time. `value` holds the last sample of each point (for tests).
 */
struct Capture {
	static constexpr int kHalf = kPoints / 2;
	float lo[2][kPoints] = {}, hi[2][kPoints] = {}, value[2][kPoints] = {};
	/** The last complete sweep: what the display shows, swapped in whole, centred on its trigger */
	float shownLo[2][kPoints] = {}, shownHi[2][kPoints] = {}, shownValue[2][kPoints] = {};
	int sweeps = 0;
	int index = kPoints;  // kPoints = holding, waiting for a trigger

	float histLo[2][kHalf] = {}, histHi[2][kHalf] = {}, histValue[2][kHalf] = {};
	int historyPos = 0;
	float sinceTake = 0.f;
	float curLo[2] = {INFINITY, INFINITY}, curHi[2] = {-INFINITY, -INFINITY};

	bool running() const {
		return index < kPoints;
	}

	/** One audio sample. `freeRun`: sweeps back to back without a trigger (FREE mode, X–Y). */
	void process(float a, float b, bool trigger, float dt, float secondsPerDiv, bool freeRun = false) {
		const float step = secondsPerDiv * kDivisions / kPoints;
		const float x[2] = {a, b};
		for (int ch = 0; ch < 2; ++ch) {
			curLo[ch] = std::min(curLo[ch], x[ch]);
			curHi[ch] = std::max(curHi[ch], x[ch]);
		}
		sinceTake += dt;
		const bool take = sinceTake >= step;
		if (take)
			sinceTake = std::min(sinceTake - step, step);

		if (!running() && (trigger || freeRun)) {
			// The half before: oldest to newest from the history, then this sample in the middle
			for (int ch = 0; ch < 2; ++ch) {
				for (int i = 0; i < kHalf; ++i) {
					const int h = (historyPos + i) % kHalf;
					lo[ch][i] = histLo[ch][h];
					hi[ch][i] = histHi[ch][h];
					value[ch][i] = histValue[ch][h];
				}
				lo[ch][kHalf] = curLo[ch];  // the samples since the last point, this one included
				hi[ch][kHalf] = curHi[ch];
				value[ch][kHalf] = x[ch];
				curLo[ch] = INFINITY;
				curHi[ch] = -INFINITY;
			}
			index = kHalf + 1;
			sinceTake = 0.f;
			return;
		}
		if (!take)
			return;

		for (int ch = 0; ch < 2; ++ch) {
			histLo[ch][historyPos] = curLo[ch];
			histHi[ch][historyPos] = curHi[ch];
			histValue[ch][historyPos] = x[ch];
			if (running()) {
				lo[ch][index] = curLo[ch];
				hi[ch][index] = curHi[ch];
				value[ch][index] = x[ch];
			}
			curLo[ch] = INFINITY;
			curHi[ch] = -INFINITY;
		}
		historyPos = (historyPos + 1) % kHalf;
		if (running() && ++index >= kPoints) {
			std::copy(&lo[0][0], &lo[0][0] + 2 * kPoints, &shownLo[0][0]);
			std::copy(&hi[0][0], &hi[0][0] + 2 * kPoints, &shownHi[0][0]);
			std::copy(&value[0][0], &value[0][0] + 2 * kPoints, &shownValue[0][0]);
			++sweeps;
		}
	}
};

/** Screen position of a voltage in divisions from the centre: v / (V/div) + position (in divisions). */
inline float divisions(float volts, float voltsPerDiv, float positionDiv) {
	return volts / voltsPerDiv + positionDiv;
}

} // namespace scope
} // namespace jxmp

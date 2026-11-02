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

/** From this sweep length (seconds) on, the acquisition is shown while it runs (scan), as a DSO does */
constexpr float kScanSweep = 0.4f;

/**
 * Acquisition as on a digital scope (e.g. a Hantek DSO): the trigger point is the middle of the screen, the half
 * before it comes from a history that runs all the time. A finished acquisition is shown whole (`shown*`); on
 * slow time bases the running one is shown as it fills (`lo/hi/value` up to `index`). NORM keeps the last
 * picture without a trigger; AUTO acquires by itself when none comes (waiting 1.5 times the spacing of slow
 * triggers, up to 10 s). Each point keeps the minimum and maximum of its time slice (no aliasing).
 */
struct Capture {
	static constexpr int kHalf = kPoints / 2;
	float lo[2][kPoints] = {}, hi[2][kPoints] = {}, value[2][kPoints] = {};
	float shownLo[2][kPoints] = {}, shownHi[2][kPoints] = {}, shownValue[2][kPoints] = {};
	int sweeps = 0;
	int index = kPoints;  // kPoints = waiting for a trigger

	float histLo[2][kHalf] = {}, histHi[2][kHalf] = {}, histValue[2][kHalf] = {};
	int historyPos = 0;
	float sinceTake = 0.f;
	float curLo[2] = {INFINITY, INFINITY}, curHi[2] = {-INFINITY, -INFINITY};
	float waiting = 0.f, sinceTrigger = 0.f, triggerSpacing = 0.f;

	bool running() const {
		return index < kPoints;
	}

	/** Drops the running acquisition (e.g. the time base changed): the next trigger starts a fresh one */
	void restart() {
		index = kPoints;
	}

	float autoTimeout(float sweepTime) const {
		if (triggerSpacing > 0.f)
			return std::min(std::max(1.5f * triggerSpacing, 0.1f), 10.f);
		return std::min(2.f * sweepTime + 0.05f, 0.1f);
	}

	/** One audio sample. `freeRun`: back to back without a trigger (X–Y). `autoRun`: AUTO, else NORM. */
	void process(float a, float b, bool trigger, float dt, float secondsPerDiv, bool freeRun = false, bool autoRun = true) {
		const float x[2] = {a, b};
		const float sweepTime = secondsPerDiv * kDivisions;
		const float step = sweepTime / kPoints;
		sinceTrigger += dt;
		if (trigger) {
			triggerSpacing = sinceTrigger;
			sinceTrigger = 0.f;
		}
		else if (sinceTrigger > std::max(3.f, 2.f * triggerSpacing))
			triggerSpacing = 0.f;

		for (int ch = 0; ch < 2; ++ch) {
			curLo[ch] = std::min(curLo[ch], x[ch]);
			curHi[ch] = std::max(curHi[ch], x[ch]);
		}
		sinceTake += dt;
		const bool take = sinceTake >= step;
		if (take)
			sinceTake = std::min(sinceTake - step, step);

		if (!running()) {
			waiting += dt;
			if (trigger || freeRun || (autoRun && waiting > autoTimeout(sweepTime))) {
				// The half before the trigger from the history (oldest first), the trigger sample in the middle
				for (int ch = 0; ch < 2; ++ch) {
					for (int i = 0; i < kHalf; ++i) {
						const int h = (historyPos + i) % kHalf;
						lo[ch][i] = histLo[ch][h];
						hi[ch][i] = histHi[ch][h];
						value[ch][i] = histValue[ch][h];
					}
					lo[ch][kHalf] = curLo[ch];
					hi[ch][kHalf] = curHi[ch];
					value[ch][kHalf] = x[ch];
				}
				index = kHalf + 1;
				waiting = 0.f;
				sinceTake = 0.f;
				pushHistory(x);
				return;
			}
		}
		if (!take)
			return;
		if (running()) {
			for (int ch = 0; ch < 2; ++ch) {
				lo[ch][index] = curLo[ch];
				hi[ch][index] = curHi[ch];
				value[ch][index] = x[ch];
			}
			if (++index >= kPoints) {
				std::copy(&lo[0][0], &lo[0][0] + 2 * kPoints, &shownLo[0][0]);
				std::copy(&hi[0][0], &hi[0][0] + 2 * kPoints, &shownHi[0][0]);
				std::copy(&value[0][0], &value[0][0] + 2 * kPoints, &shownValue[0][0]);
				++sweeps;
			}
		}
		pushHistory(x);
	}

	/** The point just finished goes into the history (it runs all the time, also during an acquisition) */
	void pushHistory(const float* x) {
		for (int ch = 0; ch < 2; ++ch) {
			histLo[ch][historyPos] = curLo[ch];
			histHi[ch][historyPos] = curHi[ch];
			histValue[ch][historyPos] = x[ch];
			curLo[ch] = INFINITY;
			curHi[ch] = -INFINITY;
		}
		historyPos = (historyPos + 1) % kHalf;
	}
};

/** Screen position of a voltage in divisions from the centre: v / (V/div) + position (in divisions). */
inline float divisions(float volts, float voltsPerDiv, float positionDiv) {
	return volts / voltsPerDiv + positionDiv;
}

} // namespace scope
} // namespace jxmp

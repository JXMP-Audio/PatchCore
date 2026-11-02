/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SCP building blocks: stepped time and volt scales, a trigger with hysteresis, and the sweep capture with
 * an automatic free-run when no trigger comes (as AUTO on a lab scope). No Rack dependency, tested in
 * tests/test_scope.cpp.
 */

#pragma once

#include <algorithm>

namespace jxmp {
namespace scope {

/** Seconds per division, the TIME/DIV knob steps */
static const float kTimeDiv[] = {0.0001f, 0.0002f, 0.0005f, 0.001f, 0.002f, 0.005f, 0.01f,
                                 0.02f,   0.05f,   0.1f,    0.2f,   0.5f,   1.f};
constexpr int kTimeSteps = sizeof(kTimeDiv) / sizeof(kTimeDiv[0]);
/** Volts per division, the VOLTS/DIV knob steps */
static const float kVoltDiv[] = {0.1f, 0.2f, 0.5f, 1.f, 2.f, 5.f};
constexpr int kVoltSteps = sizeof(kVoltDiv) / sizeof(kVoltDiv[0]);

/** AUTO starts a sweep by itself after this long without a trigger (seconds) */
constexpr float kAutoTimeout = 0.15f;

/** From this sweep length (seconds) on, the display draws the sweep while it runs (the beam moves visibly) */
constexpr float kLiveSweep = 0.1f;

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
 * One sweep of both channels: starts on a trigger. AUTO also starts by itself when no trigger came for a while,
 * so a steady voltage still shows: normally after two sweep lengths (at most 0.15 s); while triggers keep coming
 * (a slow LFO) it waits 1.5 times their spacing (up to 3 s), so the picture stands still instead of drifting.
 * NORM starts only on a trigger. The trigger point is the middle of the sweep: the half before it comes from a
 * history that is kept all the time., then takes kPoints samples spread over
 * kDivisions × time/div. A finished sweep is copied to `shown` and `sweeps` counts up, which the display reads.
 */
struct Capture {
	float buffer[2][kPoints] = {};
	float shown[2][kPoints] = {};
	int sweeps = 0;

	int index = kPoints;      // kPoints = waiting for a trigger
	float sinceTake = 0.f;    // seconds since the last point
	float waiting = 0.f;      // seconds without a trigger
	float sinceTrigger = 0.f; // seconds since the last trigger, running or not
	float triggerSpacing = 0.f;  // seconds between the last two triggers (0 = none seen lately)

	/** How long AUTO waits for a trigger before it starts a sweep by itself */
	float autoTimeout(float sweepTime) const {
		if (triggerSpacing > 0.f)
			return std::min(std::max(1.5f * triggerSpacing, kAutoTimeout), 3.f);
		return std::min(2.f * sweepTime + 0.05f, kAutoTimeout);
	}

	bool running() const {
		return index < kPoints;
	}

	/** The half sweep before the trigger, kept all the time, so the trigger point sits in the middle of the screen */
	static constexpr int kHalf = kPoints / 2;
	float history[2][kHalf] = {};
	int historyPos = 0;

	/** One audio sample. `freeRun` = take sweeps back to back, no trigger (X–Y mode). `autoRun` = AUTO: start
	without a trigger after a while; off = NORM. */
	void process(float a, float b, bool trigger, float dt, float secondsPerDiv, bool freeRun = false, bool autoRun = true) {
		const float sweepTime = secondsPerDiv * kDivisions;
		const float step = sweepTime / kPoints;
		// Spacing of the triggers (also those during a sweep); forgotten when they stop for 3 s
		sinceTrigger += dt;
		if (trigger) {
			triggerSpacing = sinceTrigger;
			sinceTrigger = 0.f;
		}
		else if (sinceTrigger > 3.f)
			triggerSpacing = 0.f;

		// One point per step of the time base; long steps fall behind on a short dt: at most one per sample
		sinceTake += dt;
		bool take = false;
		if (sinceTake >= step) {
			sinceTake = std::min(sinceTake - step, step);
			take = true;
		}

		// The history runs all the time, also during a sweep: a trigger right after a sweep (a slow LFO) must
		// find the half before it, not samples from before the previous sweep
		if (take) {
			history[0][historyPos] = a;
			history[1][historyPos] = b;
			historyPos = (historyPos + 1) % kHalf;
		}

		if (!running()) {
			waiting += dt;
			if (!(trigger || freeRun || (autoRun && waiting > autoTimeout(sweepTime))))
				return;
			// Start: the remembered half first (oldest to newest; the newest is this sample when it was just
			// taken), the trigger sample in the middle
			for (int ch = 0; ch < 2; ++ch)
				for (int i = 0; i < kHalf; ++i)
					buffer[ch][i] = history[ch][(historyPos + i) % kHalf];
			buffer[0][kHalf] = a;
			buffer[1][kHalf] = b;
			index = kHalf + 1;
			waiting = 0.f;
			sinceTake = 0.f;
			return;
		}
		if (!take)
			return;
		buffer[0][index] = a;
		buffer[1][index] = b;
		if (++index >= kPoints) {
			std::copy(&buffer[0][0], &buffer[0][0] + kPoints, &shown[0][0]);
			std::copy(&buffer[1][0], &buffer[1][0] + kPoints, &shown[1][0]);
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

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
 * One sweep of both channels: starts on a trigger (AUTO also by itself after two sweep lengths without one, so
 * a steady voltage still shows; NORM only on a trigger), then takes kPoints samples spread over
 * kDivisions × time/div. A finished sweep is copied to `shown` and `sweeps` counts up, which the display reads.
 */
struct Capture {
	float buffer[2][kPoints] = {};
	float shown[2][kPoints] = {};
	int sweeps = 0;

	int index = kPoints;      // kPoints = waiting for a trigger
	float sinceTake = 0.f;    // seconds since the last point
	float waiting = 0.f;      // seconds without a trigger

	bool running() const {
		return index < kPoints;
	}

	/** One audio sample. `freeRun` = take sweeps back to back, no trigger (X–Y mode). `autoRun` = AUTO: start
	without a trigger after a while; off = NORM. */
	void process(float a, float b, bool trigger, float dt, float secondsPerDiv, bool freeRun = false, bool autoRun = true) {
		const float sweepTime = secondsPerDiv * kDivisions;
		const float step = sweepTime / kPoints;
		if (!running()) {
			waiting += dt;
			if (trigger || freeRun || (autoRun && waiting > 2.f * sweepTime + 0.05f)) {
				index = 0;
				waiting = 0.f;
				sinceTake = step;  // the first point is the trigger sample
			}
			else
				return;
		}
		sinceTake += dt;
		if (sinceTake < step)
			return;
		sinceTake -= step;
		// Long time steps fall far behind on a short dt: never more than one point per sample
		sinceTake = std::min(sinceTake, step);
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

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

/** Phosphor persistence: a written point fades to dark over about this many seconds (P31-like green) */
constexpr float kPersistence = 0.25f;

/**
 * The beam of an analogue scope: a sweep starts on a trigger at the left edge (AUTO also by itself when no
 * trigger comes) and writes point by point at the real speed. Each point remembers when it was written
 * (`stamp`, in `time` seconds); the display lights it by its age, as the phosphor fades. Each point keeps the
 * minimum and maximum of its time slice (no aliasing). AUTO waits for slow triggers: while they keep coming it
 * waits 1.5 times their spacing (up to 10 s) before running free.
 */
struct Capture {
	float lo[2][kPoints] = {}, hi[2][kPoints] = {}, value[2][kPoints] = {};
	double stamp[kPoints] = {};
	double time = 0.0;  // seconds since the start, for the stamps
	int sweeps = 0;
	int index = kPoints;  // kPoints = beam waiting at the left (retrace done)
	float sinceTake = 0.f;
	float curLo[2] = {INFINITY, INFINITY}, curHi[2] = {-INFINITY, -INFINITY};
	float waiting = 0.f, sinceTrigger = 0.f, triggerSpacing = 0.f;

	Capture() {
		for (double& s : stamp)
			s = -1e9;  // never written: dark
	}

	bool running() const {
		return index < kPoints;
	}

	/** Stops the sweep (e.g. the time base changed): the next trigger starts a fresh one */
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
		time += dt;
		const float x[2] = {a, b};
		const float sweepTime = secondsPerDiv * kDivisions;
		sinceTrigger += dt;
		if (trigger) {
			triggerSpacing = sinceTrigger;
			sinceTrigger = 0.f;
		}
		else if (sinceTrigger > std::max(3.f, 2.f * triggerSpacing))
			triggerSpacing = 0.f;  // the triggers stopped: AUTO runs free again soon

		if (!running()) {
			waiting += dt;
			if (!(trigger || freeRun || (autoRun && waiting > autoTimeout(sweepTime))))
				return;
			index = 0;
			waiting = 0.f;
			sinceTake = 0.f;
			for (int ch = 0; ch < 2; ++ch) {
				curLo[ch] = INFINITY;
				curHi[ch] = -INFINITY;
			}
		}
		const float step = sweepTime / kPoints;
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
		stamp[index] = time;
		if (++index >= kPoints)
			++sweeps;
	}

	/** Brightness of a point now: 1 when just written, fading with the phosphor */
	float glow(int i) const {
		const double age = time - stamp[i];
		return age < 0.0 ? 0.f : (float) std::exp(-age / kPersistence);
	}
};

/** Screen position of a voltage in divisions from the centre: v / (V/div) + position (in divisions). */
inline float divisions(float volts, float voltsPerDiv, float positionDiv) {
	return volts / voltsPerDiv + positionDiv;
}

} // namespace scope
} // namespace jxmp

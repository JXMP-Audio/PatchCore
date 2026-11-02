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
#include <atomic>
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


/** Coarse memory for redrawing at once when the time base changes: 1 kHz, 10 s */
constexpr int kCoarseRate = 1000;
constexpr int kCoarse = 10 * kCoarseRate;

/** Points before the trigger: always half the screen, so the trigger point is the centre of the graticule */
inline int pretrigger(float) {
	return kPoints / 2;
}

/**
 * Acquisition as on a digital scope (e.g. a Hantek DSO): the trigger point is in the middle of the screen, the
 * part before it comes from a history that runs all the time. Only
 * finished acquisitions are shown, whole (`shown()`, double-buffered). NORM keeps the
 * last picture without a trigger; AUTO acquires by itself when none comes (waiting 1.5 times the spacing of
 * slow triggers, up to 10 s). Each point keeps the minimum and maximum of its time slice (no aliasing). A 1 kHz
 * memory of the last 10 s lets rebuild() redraw a slower time base at once.
 */
struct Capture {
	static constexpr int kHalf = kPoints / 2;
	float lo[2][kPoints] = {}, hi[2][kPoints] = {}, value[2][kPoints] = {};
	/** Two pictures: the audio thread writes the back one and then flips `front`, so the display never draws a
	picture that is being copied (half old, half new) */
	struct Picture {
		float lo[2][kPoints] = {}, hi[2][kPoints] = {}, value[2][kPoints] = {};
		int pre = kPoints / 2;
		float secondsPerDiv = 0.f;  // the time base it was taken at
	};
	Picture pictures[2];
	std::atomic<int> front{0};
	const Picture& shown() const {
		return pictures[front.load(std::memory_order_acquire)];
	}
	int sweeps = 0;
	int index = kPoints;  // kPoints = waiting for a trigger
	int pre = kHalf;

	float histLo[2][kHalf] = {}, histHi[2][kHalf] = {}, histValue[2][kHalf] = {};
	int historyPos = 0;
	float sinceTake = 0.f;
	float curLo[2] = {INFINITY, INFINITY}, curHi[2] = {-INFINITY, -INFINITY};
	float waiting = 0.f, sinceTrigger = 0.f, triggerSpacing = 0.f;

	float coarseLo[2][kCoarse] = {}, coarseHi[2][kCoarse] = {}, coarseValue[2][kCoarse] = {};
	long coarseCount = 0;      // coarse samples written so far
	/** Coarse samples of the last triggers (a ring), so a redraw can use one with enough signal after it */
	static constexpr int kTriggers = 16;
	long triggers[kTriggers];
	int triggerCount = 0;
	float coarseSince = 0.f;
	float coarseCurLo[2] = {INFINITY, INFINITY}, coarseCurHi[2] = {-INFINITY, -INFINITY};

	bool running() const {
		return index < kPoints;
	}

	/** Drops the running acquisition: the next trigger starts a fresh one */
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
			triggers[triggerCount++ % kTriggers] = coarseCount;
		}
		else if (sinceTrigger > std::max(3.f, 2.f * triggerSpacing))
			triggerSpacing = 0.f;

		// The coarse memory
		for (int ch = 0; ch < 2; ++ch) {
			coarseCurLo[ch] = std::min(coarseCurLo[ch], x[ch]);
			coarseCurHi[ch] = std::max(coarseCurHi[ch], x[ch]);
		}
		coarseSince += dt;
		if (coarseSince >= 1.f / kCoarseRate) {
			coarseSince -= 1.f / kCoarseRate;
			const int k = (int) (coarseCount % kCoarse);
			for (int ch = 0; ch < 2; ++ch) {
				coarseLo[ch][k] = coarseCurLo[ch];
				coarseHi[ch][k] = coarseCurHi[ch];
				coarseValue[ch][k] = x[ch];
				coarseCurLo[ch] = INFINITY;
				coarseCurHi[ch] = -INFINITY;
			}
			++coarseCount;
		}

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
				// The part before the trigger from the history (oldest first), the trigger sample after it
				pre = pretrigger(secondsPerDiv);
				acquiringDiv = secondsPerDiv;
				for (int ch = 0; ch < 2; ++ch) {
					for (int i = 0; i < pre; ++i) {
						const int h = (historyPos - pre + i + 2 * kHalf) % kHalf;
						lo[ch][i] = histLo[ch][h];
						hi[ch][i] = histHi[ch][h];
						value[ch][i] = histValue[ch][h];
					}
					lo[ch][pre] = curLo[ch];
					hi[ch][pre] = curHi[ch];
					value[ch][pre] = x[ch];
				}
				index = pre + 1;
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
			if (++index >= kPoints)
				publish(lo, hi, value, pre, acquiringDiv);
		}
		pushHistory(x);
	}

	float acquiringDiv = 0.f;  // the time base of the running acquisition

	void publish(const float (*l)[kPoints], const float (*h)[kPoints], const float (*v)[kPoints], int trigAt, float div) {
		const int back = 1 - front.load(std::memory_order_relaxed);
		Picture& p = pictures[back];
		std::copy(&l[0][0], &l[0][0] + 2 * kPoints, &p.lo[0][0]);
		std::copy(&h[0][0], &h[0][0] + 2 * kPoints, &p.hi[0][0]);
		std::copy(&v[0][0], &v[0][0] + 2 * kPoints, &p.value[0][0]);
		p.pre = trigAt;
		p.secondsPerDiv = div;
		front.store(back, std::memory_order_release);
		++sweeps;
	}

	/**
	 * Redraws at once for a new time base from the coarse memory: around the last trigger when there is enough
	 * after it, else ending with the newest sample. Only for time bases whose points are at least one coarse
	 * sample long (faster ones get a fresh acquisition quickly anyway). Returns false when it could not.
	 */
	bool rebuild(float secondsPerDiv) {
		const double stepSamples = secondsPerDiv * kDivisions / kPoints * kCoarseRate;
		if (stepSamples < 1.0 || coarseCount == 0)
			return rescale(secondsPerDiv);
		const int p = pretrigger(secondsPerDiv);
		const double after = (kPoints - p) * stepSamples;
		// The newest trigger with enough signal after it (and before it, inside the memory)
		long trig = -1;
		for (int n = 1; n <= std::min(triggerCount, kTriggers); ++n) {
			const long t = triggers[(triggerCount - n) % kTriggers];
			if (t + after <= coarseCount && coarseCount - (t - p * stepSamples) < kCoarse) {
				trig = t;
				break;
			}
		}
		if (trig < 0)
			return rescale(secondsPerDiv);  // no usable trigger in the memory
		const double t0 = trig - p * stepSamples;  // coarse sample of point 0
		float l[2][kPoints], h[2][kPoints], v[2][kPoints];
		for (int i = 0; i < kPoints; ++i) {
			const long a = (long) std::floor(t0 + i * stepSamples), b = std::max(a + 1, (long) std::floor(t0 + (i + 1) * stepSamples));
			for (int ch = 0; ch < 2; ++ch) {
				l[ch][i] = INFINITY;
				h[ch][i] = -INFINITY;
				v[ch][i] = 0.f;
			}
			for (long s = a; s < b; ++s) {
				if (s < 0 || s >= coarseCount || coarseCount - s > kCoarse)
					continue;
				const int k = (int) (s % kCoarse);
				for (int ch = 0; ch < 2; ++ch) {
					l[ch][i] = std::min(l[ch][i], coarseLo[ch][k]);
					h[ch][i] = std::max(h[ch][i], coarseHi[ch][k]);
					v[ch][i] = coarseValue[ch][k];
				}
			}
		}
		publish(l, h, v, p, secondsPerDiv);
		restart();
		return true;
	}

	/**
	 * The last picture at a new time base, as a stopped DSO zooms: around its trigger point, stretched or
	 * squeezed; where it has no data the screen stays empty. Until the next acquisition replaces it.
	 */
	bool rescale(float secondsPerDiv) {
		const Picture& old = shown();
		if (old.secondsPerDiv <= 0.f)
			return false;
		const double ratio = (double) secondsPerDiv / old.secondsPerDiv;  // old points per new point
		const int p = pretrigger(secondsPerDiv);
		float l[2][kPoints], h[2][kPoints], v[2][kPoints];
		for (int i = 0; i < kPoints; ++i) {
			const double a = old.pre + (i - p) * ratio, b = old.pre + (i + 1 - p) * ratio;
			const int ia = (int) std::floor(a), ib = std::max(ia + 1, (int) std::ceil(b));
			for (int ch = 0; ch < 2; ++ch) {
				l[ch][i] = INFINITY;
				h[ch][i] = -INFINITY;
				v[ch][i] = 0.f;
				if (ratio < 1.0) {
					// Zoomed in: interpolate between the two old points around this one
					if (a < 0.0 || a > kPoints - 1)
						continue;
					const int k = std::min((int) a, kPoints - 2);
					const float f = (float) (a - k);
					const float y = old.value[ch][k] + (old.value[ch][k + 1] - old.value[ch][k]) * f;
					l[ch][i] = h[ch][i] = v[ch][i] = y;
					continue;
				}
				for (int k = std::max(ia, 0); k < std::min(ib, kPoints); ++k) {
					l[ch][i] = std::min(l[ch][i], old.lo[ch][k]);
					h[ch][i] = std::max(h[ch][i], old.hi[ch][k]);
					v[ch][i] = old.value[ch][k];
				}
			}
		}
		publish(l, h, v, p, secondsPerDiv);
		restart();
		return true;
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

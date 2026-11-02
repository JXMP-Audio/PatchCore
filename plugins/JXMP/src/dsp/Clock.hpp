/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-CLK building blocks: the musical position in beats from external clock pulses (PPQN), gates of the
 * divisions and multiplications with swing. No Rack dependency, tested in tests/test_clock.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace jxmp {
namespace clock {

/**
 * Follows clock pulses (e.g. 24 per quarter note from Host MIDI): the position in beats moves on smoothly
 * between pulses at the measured rate and snaps to every pulse, so the outputs stay locked to the source.
 * Running while pulses come; stopped when none came for a while.
 */
struct PulseFollower {
	int ppqn = 24;
	long pulses = 0;           // pulses since the start
	double sinceLast = 0.0;    // seconds since the last pulse
	double period = 0.0;       // seconds between pulses (0 = not known yet)
	bool running = false;
	bool started = false;      // true for the sample the clock starts (for RESET)

	void reset() {
		pulses = 0;
		sinceLast = 0.0;
		period = 0.0;
		running = false;
	}

	void process(bool pulse, double dt) {
		started = false;
		sinceLast += dt;
		if (pulse) {
			if (!running) {
				running = true;
				started = true;
				pulses = 0;
			}
			else {
				// Smooth the period a little, but follow tempo changes quickly
				period = period > 0.0 ? period * 0.5 + sinceLast * 0.5 : sinceLast;
				++pulses;
			}
			sinceLast = 0.0;
		}
		// Stopped: no pulse for four periods (or 2.5 s before the tempo is known: 1 PPQN at 30 BPM is 2 s)
		const double timeout = period > 0.0 ? std::max(4.0 * period, 0.05) : 2.5;
		if (running && sinceLast > timeout)
			reset();
	}

	/** Position in beats: whole pulses plus the part of the current pulse, never past the next pulse. */
	double beats() const {
		if (!running)
			return 0.0;
		const double part = period > 0.0 ? std::min(sinceLast / period, 0.999) : 0.0;
		return (pulses + part) / ppqn;
	}

	double bpm() const {
		return period > 0.0 ? 60.0 / (period * ppqn) : 0.0;
	}
};

/**
 * Gate of an output that ticks every `period` beats (0.25 = sixteenths, 1 = beats, 4 = bars in 4/4): high for
 * the first half of each period. `swing` (0.5 straight … 0.75) delays every second tick, where the second of
 * a pair lands at `swing` of the pair; only for `swung` outputs.
 */
inline bool gate(double beats, double period, float swing, bool swung) {
	if (beats < 0.0 || period <= 0.0)
		return false;
	const double n = std::floor(beats / period + 1e-9);
	const double local = beats - n * period;
	const bool odd = ((long long) n & 1) != 0;
	const double offset = swung && odd ? (std::min(std::max((double) swing, 0.5), 0.75) - 0.5) * 2.0 * period : 0.0;
	return local >= offset && local < offset + period * 0.5;
}

/** Periods in beats of the outputs, in panel order: ×4 (sixteenths), ×2 (eighths), beat, /2, /4, /8, /16. */
static const double kPeriods[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
constexpr int kOutputs = sizeof(kPeriods) / sizeof(kPeriods[0]);

/** Swing applies to the subdivisions only (×4, ×2) */
inline bool swungOutput(int index) {
	return index < 2;
}

} // namespace clock
} // namespace jxmp

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL recording into a slot, no Rack dependency. The UI thread arms it (the buffers are made then, never on the
 * audio thread), starts and stops it with OK or cancels it with MENU; with REC GATE patched the audio thread starts
 * on the gate and stops when it falls. ±5 V is full scale. Only L (or only R) patched: mono.
 *
 * Threads: the state is atomic; the audio thread marks `busy` around each write, so after stop / cancel the UI waits
 * for that one sample to finish before it touches the buffers (both use sequentially consistent atomics).
 */

#pragma once

#include "Sampler.hpp"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace jxmp {
namespace sampler {

struct Recorder {
	enum State { IDLE, ARMED, RECORDING, DONE };
	std::atomic<int> state{IDLE};
	std::atomic<bool> busy{false};
	std::atomic<size_t> frames{0};
	std::atomic<bool> both{false};  // L and R were both patched while recording: stereo
	std::atomic<float> peakL{0.f}, peakR{0.f};
	std::vector<float> l, r;
	size_t maxFrames = 0;
	float rate = 48000.f;
	std::atomic<bool> byGate{false};  // the recording was started by REC GATE (its fall ends it)
	bool gateHigh = false;            // audio thread only

	// ---- UI thread ----
	void arm(float sampleRate, float maxSeconds) {
		waitIdle();
		rate = sampleRate;
		maxFrames = (size_t) std::max(1.f, sampleRate * maxSeconds);
		l.assign(maxFrames, 0.f);
		r.assign(maxFrames, 0.f);
		frames = 0;
		both = false;
		byGate = false;
		state = ARMED;
	}
	/** OK while armed (no REC GATE patched): starts */
	bool start() {
		int s = ARMED;
		return state.compare_exchange_strong(s, (int) RECORDING);
	}
	/** OK while recording: stops; the take is then ready */
	void stop() {
		int s = RECORDING;
		state.compare_exchange_strong(s, (int) DONE);
		waitWrite();
	}
	/** MENU: throws the recording away */
	void cancel() {
		state = IDLE;
		waitWrite();
		release();
	}
	/** The finished recording (state DONE) into `out`; false when it is shorter than 16 frames. Back to IDLE. */
	bool take(SampleData& out) {
		if (state != DONE)
			return false;
		waitWrite();
		const size_t n = frames;
		out = SampleData();
		out.rate = rate;
		out.stereo = both;
		out.l.assign(l.begin(), l.begin() + n);
		if (out.stereo)
			out.r.assign(r.begin(), r.begin() + n);
		release();
		state = IDLE;
		return n >= 16;
	}
	float takePeakL() { return peakL.exchange(0.f); }
	float takePeakR() { return peakR.exchange(0.f); }

	// ---- audio thread ----
	/** One sample of the REC inputs (volts) and which of them are patched */
	void process(float inL, float inR, bool lOn, bool rOn, bool gateOn, float gate) {
		const int s0 = state.load();  // acquires the buffers arm() made
		if (s0 == IDLE || s0 == DONE) {
			gateHigh = false;
			return;
		}
		const float vl = (lOn ? inL : inR) * 0.2f, vr = (rOn ? inR : inL) * 0.2f;
		if (std::fabs(vl) > peakL.load(std::memory_order_relaxed))
			peakL.store(std::fabs(vl), std::memory_order_relaxed);
		if (std::fabs(vr) > peakR.load(std::memory_order_relaxed))
			peakR.store(std::fabs(vr), std::memory_order_relaxed);
		// REC GATE (Schmitt 0.1 / 1 V): starts an armed recording, its fall ends one it started
		const bool wasHigh = gateHigh;
		gateHigh = gateOn && (gateHigh ? gate > 0.1f : gate >= 1.f);
		if (gateOn && gateHigh && !wasHigh) {
			int s = ARMED;
			if (state.compare_exchange_strong(s, (int) RECORDING))
				byGate = true;
		}
		if (byGate && !gateHigh) {
			int s = RECORDING;
			state.compare_exchange_strong(s, (int) DONE);
		}
		busy = true;
		if (state == RECORDING) {
			const size_t n = frames.load(std::memory_order_relaxed);
			if (n < maxFrames) {
				l[n] = vl;
				r[n] = vr;
				if (lOn && rOn)
					both.store(true, std::memory_order_relaxed);
				frames.store(n + 1, std::memory_order_release);
			}
			if (n + 1 >= maxFrames) {
				int s = RECORDING;
				state.compare_exchange_strong(s, (int) DONE);
			}
		}
		busy = false;
	}

private:
	void waitWrite() {
		while (busy)
			std::this_thread::yield();
	}
	void waitIdle() { waitWrite(); }
	void release() {
		std::vector<float>().swap(l);
		std::vector<float>().swap(r);
		frames = 0;
		maxFrames = 0;
	}
};

} // namespace sampler
} // namespace jxmp

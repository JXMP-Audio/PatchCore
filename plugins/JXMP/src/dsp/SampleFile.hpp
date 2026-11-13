/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL: WAV decoding with dr_wav (compiled once in plugins/Fundamental/src/dr_wav.c). Any bit depth or float,
 * mono or stereo (more channels: the first two), the file's own rate, at most 60 s.
 */

#pragma once

#include "Sampler.hpp"
#include "../../../Fundamental/src/dr_wav.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace jxmp {
namespace sampler {

constexpr float kMaxSeconds = 60.f;

/** Decodes a WAV file held in memory into `out`. On failure `out` stays as it was and `error` says why
 * ("NOT A WAV FILE", "EMPTY FILE"). `cut` is set when the file was longer than 60 s and was shortened. */
inline bool decodeWav(const uint8_t* bytes, size_t size, SampleData& out, std::string& error, bool& cut) {
	cut = false;
	drwav w;
	if (!bytes || size == 0 || !drwav_init_memory(&w, bytes, size, nullptr)) {
		error = "NOT A WAV FILE";
		return false;
	}
	const unsigned channels = w.channels;
	const float rate = (float) w.sampleRate;
	if (channels == 0 || rate <= 0.f) {
		drwav_uninit(&w);
		error = "NOT A WAV FILE";
		return false;
	}
	// The header may claim more than the file holds (a broken or hostile file): never trust it for allocations.
	// Read in small chunks and keep only the first two channels.
	uint64_t frames = w.totalPCMFrameCount;
	const uint64_t bits = std::max<uint64_t>(1, w.bitsPerSample);
	const uint64_t fit = (uint64_t) size * 8 / (channels * bits) + 1;
	frames = std::min(frames, fit);
	const uint64_t maxFrames = (uint64_t) (kMaxSeconds * rate);
	if (frames > maxFrames) {
		frames = maxFrames;
		cut = true;
	}
	SampleData d;
	d.rate = rate;
	d.stereo = channels >= 2;
	try {
		d.l.reserve((size_t) frames);
		if (d.stereo)
			d.r.reserve((size_t) frames);
		const uint64_t chunk = 4096;
		std::vector<float> buf((size_t) (chunk * channels));
		uint64_t done = 0;
		while (done < frames) {
			const uint64_t got = drwav_read_pcm_frames_f32(&w, std::min(chunk, frames - done), buf.data());
			if (got == 0)
				break;
			for (uint64_t k = 0; k < got; ++k) {
				d.l.push_back(buf[(size_t) (k * channels)]);
				if (d.stereo)
					d.r.push_back(buf[(size_t) (k * channels + 1)]);
			}
			done += got;
		}
	}
	catch (std::exception&) {
		drwav_uninit(&w);
		error = "FILE TOO BIG";
		cut = false;
		return false;
	}
	drwav_uninit(&w);
	if (d.l.empty()) {
		error = "EMPTY FILE";
		cut = false;
		return false;
	}
	// A header that lied about the length: the cut was not a real one
	if (cut && d.frames() < maxFrames)
		cut = false;
	out = std::move(d);
	error.clear();
	return true;
}

} // namespace sampler
} // namespace jxmp

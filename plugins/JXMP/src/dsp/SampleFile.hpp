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
#include <cmath>
#include <cstring>
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

/** A 32-bit float WAV (mono or stereo, the sample's rate) of `d`: what is kept in the patch when the file had to be cut,
 * so the patch holds no more than plays. Little-endian, as WAV is. */
inline std::vector<uint8_t> encodeWav(const SampleData& d) {
	const uint32_t channels = d.stereo && d.r.size() == d.l.size() ? 2 : 1;
	const uint32_t frames = (uint32_t) d.frames();
	const uint32_t dataBytes = frames * channels * 4;
	std::vector<uint8_t> out;
	out.reserve(44 + dataBytes);
	auto u32 = [&](uint32_t v) {
		for (int k = 0; k < 4; ++k)
			out.push_back((uint8_t) (v >> (8 * k)));
	};
	auto u16 = [&](uint16_t v) {
		out.push_back((uint8_t) v);
		out.push_back((uint8_t) (v >> 8));
	};
	auto tag = [&](const char* t) { out.insert(out.end(), t, t + 4); };
	const uint32_t rate = (uint32_t) std::lround(d.rate);
	tag("RIFF");
	u32(36 + dataBytes);
	tag("WAVE");
	tag("fmt ");
	u32(16);
	u16(3);  // IEEE float
	u16((uint16_t) channels);
	u32(rate);
	u32(rate * channels * 4);
	u16((uint16_t) (channels * 4));
	u16(32);
	tag("data");
	u32(dataBytes);
	for (uint32_t k = 0; k < frames; ++k)
		for (uint32_t c = 0; c < channels; ++c) {
			const float v = c ? d.r[k] : d.l[k];
			uint32_t bits;
			std::memcpy(&bits, &v, 4);
			u32(bits);
		}
	return out;
}

} // namespace sampler
} // namespace jxmp

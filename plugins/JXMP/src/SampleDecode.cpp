/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL formats (see dsp/SampleDecode.hpp). The decoders are compiled here with internal linkage, so they never
 * clash with other copies in PatchCore (Carla has its own dr_mp3 and dr_flac).
 * dep/: dr_flac 0.12.31 and dr_mp3 0.6.26 (David Reid, public domain / MIT-0, copied from Carla), stb_vorbis 1.22
 * (Sean Barrett, public domain / MIT, nothings/stb commit 13e2e1344957).
 */

#include "dsp/SampleDecode.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DRFLAC_API static
#include "dep/dr_flac.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DRMP3_API static
#include "dep/dr_mp3.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
// stb_vorbis has no static option and its functions are extern "C": renamed, so no other copy can clash with them
#define stb_vorbis_close jxmp_stb_vorbis_close
#define stb_vorbis_decode_memory jxmp_stb_vorbis_decode_memory
#define stb_vorbis_get_comment jxmp_stb_vorbis_get_comment
#define stb_vorbis_get_error jxmp_stb_vorbis_get_error
#define stb_vorbis_get_file_offset jxmp_stb_vorbis_get_file_offset
#define stb_vorbis_get_frame_float jxmp_stb_vorbis_get_frame_float
#define stb_vorbis_get_frame_short jxmp_stb_vorbis_get_frame_short
#define stb_vorbis_get_frame_short_interleaved jxmp_stb_vorbis_get_frame_short_interleaved
#define stb_vorbis_get_info jxmp_stb_vorbis_get_info
#define stb_vorbis_get_playback_sample_offset jxmp_stb_vorbis_get_playback_sample_offset
#define stb_vorbis_get_sample_offset jxmp_stb_vorbis_get_sample_offset
#define stb_vorbis_get_samples_float jxmp_stb_vorbis_get_samples_float
#define stb_vorbis_get_samples_float_interleaved jxmp_stb_vorbis_get_samples_float_interleaved
#define stb_vorbis_get_samples_short jxmp_stb_vorbis_get_samples_short
#define stb_vorbis_get_samples_short_interleaved jxmp_stb_vorbis_get_samples_short_interleaved
#define stb_vorbis_open_memory jxmp_stb_vorbis_open_memory
#define stb_vorbis_seek jxmp_stb_vorbis_seek
#define stb_vorbis_seek_frame jxmp_stb_vorbis_seek_frame
#define stb_vorbis_seek_start jxmp_stb_vorbis_seek_start
#define stb_vorbis_stream_length_in_samples jxmp_stb_vorbis_stream_length_in_samples
#define stb_vorbis_stream_length_in_seconds jxmp_stb_vorbis_stream_length_in_seconds
#include "dep/stb_vorbis.c"
#undef L
#undef R
#undef C

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace jxmp {
namespace sampler {

AudioKind audioKind(const uint8_t* b, size_t n) {
	if (!b || n < 4)
		return KIND_NONE;
	if (n >= 12 && (std::memcmp(b, "RIFF", 4) == 0 || std::memcmp(b, "RIFX", 4) == 0 || std::memcmp(b, "RF64", 4) == 0) &&
	    std::memcmp(b + 8, "WAVE", 4) == 0)
		return KIND_WAV;
	if (std::memcmp(b, "fLaC", 4) == 0)
		return KIND_FLAC;
	if (std::memcmp(b, "OggS", 4) == 0)
		return KIND_OGG;
	// MP3: an ID3 tag, or straight away a frame (11 sync bits)
	if (std::memcmp(b, "ID3", 3) == 0 || (b[0] == 0xFF && (b[1] & 0xE0) == 0xE0))
		return KIND_MP3;
	return KIND_NONE;
}

namespace {

/** Collects interleaved float frames into a SampleData: the first two channels, at most 60 s */
struct Collect {
	SampleData d;
	unsigned channels = 1;
	size_t maxFrames = 0;
	bool cut = false;

	void start(unsigned ch, float rate) {
		channels = ch;
		d.rate = rate;
		d.stereo = ch >= 2;
		maxFrames = (size_t) (kMaxSeconds * rate);
	}
	/** Adds `n` frames; false once 60 s are full */
	bool add(const float* x, size_t n) {
		for (size_t k = 0; k < n; ++k) {
			if (d.l.size() >= maxFrames) {
				cut = true;
				return false;
			}
			d.l.push_back(x[k * channels]);
			if (d.stereo)
				d.r.push_back(x[k * channels + 1]);
		}
		return true;
	}
};

const size_t kChunk = 4096;

bool flac(const uint8_t* b, size_t n, Collect& c) {
	drflac* f = drflac_open_memory(b, n, nullptr);
	if (!f)
		return false;
	if (f->channels == 0 || f->sampleRate == 0) {
		drflac_close(f);
		return false;
	}
	c.start(f->channels, (float) f->sampleRate);
	std::vector<float> buf(kChunk * f->channels);
	for (;;) {
		const drflac_uint64 got = drflac_read_pcm_frames_f32(f, kChunk, buf.data());
		if (got == 0 || !c.add(buf.data(), (size_t) got))
			break;
	}
	drflac_close(f);
	return true;
}

/** How much of an MP3's decoded output is not the sound (gapless trimming): dr_mp3 also decodes the Xing / Info frame
 * (silence), the decoder lags by 529 samples, and the LAME tag says how much the encoder added before and after */
struct Mp3Trim {
	size_t start = 529, end = 0;
};

Mp3Trim mp3Trim(const uint8_t* b, size_t n) {
	Mp3Trim t;
	size_t p = 0;
	// ID3v2 tag: 10 bytes, a syncsafe size, 10 more with a footer
	if (n >= 10 && std::memcmp(b, "ID3", 3) == 0) {
		p = 10 + (((size_t) (b[6] & 0x7f) << 21) | ((size_t) (b[7] & 0x7f) << 14) | ((size_t) (b[8] & 0x7f) << 7) |
		          (size_t) (b[9] & 0x7f));
		if (b[5] & 0x10)
			p += 10;
	}
	if (p + 4 > n || b[p] != 0xFF || (b[p + 1] & 0xE0) != 0xE0 || ((b[p + 1] >> 1) & 3) != 1)
		return t;  // not a Layer III frame first: only the decoder lag
	const bool mpeg1 = ((b[p + 1] >> 3) & 3) == 3, mono = ((b[p + 3] >> 6) & 3) == 3;
	const size_t spf = mpeg1 ? 1152 : 576;
	size_t x = p + 4 + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
	if (x + 8 > n || (std::memcmp(b + x, "Xing", 4) != 0 && std::memcmp(b + x, "Info", 4) != 0))
		return t;
	t.start += spf;  // the info frame itself
	const uint8_t flags = b[x + 7];
	x += 8 + ((flags & 1) ? 4 : 0) + ((flags & 2) ? 4 : 0) + ((flags & 4) ? 100 : 0) + ((flags & 8) ? 4 : 0);
	// LAME extension: a 9-byte encoder name, then at +21 the 12-bit delay and 12-bit padding
	if (x + 24 > n)
		return t;
	const size_t delay = ((size_t) b[x + 21] << 4) | (b[x + 22] >> 4), pad = ((size_t) (b[x + 22] & 0x0f) << 8) | b[x + 23];
	if (delay > 3000 || pad > 3000)
		return t;  // not a real LAME tag
	t.start += delay;
	t.end = pad > 529 ? pad - 529 : 0;
	return t;
}

bool mp3(const uint8_t* b, size_t n, Collect& c) {
	drmp3 m;
	if (!drmp3_init_memory(&m, b, n, nullptr))
		return false;
	if (m.channels == 0 || m.sampleRate == 0) {
		drmp3_uninit(&m);
		return false;
	}
	c.start(m.channels, (float) m.sampleRate);
	// Room for what the trim below takes off the start (info frame, encoder and decoder delay): 60 s of sound after it
	const size_t limit = c.maxFrames;
	c.maxFrames += 1152 + 4096 + 529;
	std::vector<float> buf(kChunk * m.channels);
	for (;;) {
		const drmp3_uint64 got = drmp3_read_pcm_frames_f32(&m, kChunk, buf.data());
		if (got == 0 || !c.add(buf.data(), (size_t) got))
			break;
	}
	drmp3_uninit(&m);
	const Mp3Trim t = mp3Trim(b, n);
	const size_t total = c.d.l.size();
	if (t.start < total) {
		// The padding at the end only when the whole file was read
		const size_t sound = total - t.start - (!c.cut && t.start + t.end < total ? t.end : 0);
		const size_t keep = std::min(sound, limit);
		c.cut = c.cut || keep < sound;
		c.d.l.erase(c.d.l.begin(), c.d.l.begin() + t.start);
		c.d.l.resize(keep);
		if (c.d.stereo) {
			c.d.r.erase(c.d.r.begin(), c.d.r.begin() + t.start);
			c.d.r.resize(keep);
		}
	}
	return true;
}

bool ogg(const uint8_t* b, size_t n, Collect& c) {
	if (n > (size_t) 0x7fffffff)
		return false;
	int err = 0;
	stb_vorbis* v = stb_vorbis_open_memory(b, (int) n, &err, nullptr);
	if (!v)
		return false;
	const stb_vorbis_info info = stb_vorbis_get_info(v);
	if (info.channels <= 0 || info.sample_rate == 0) {
		stb_vorbis_close(v);
		return false;
	}
	c.start((unsigned) info.channels, (float) info.sample_rate);
	std::vector<float> buf(kChunk * info.channels);
	for (;;) {
		const int got = stb_vorbis_get_samples_float_interleaved(v, info.channels, buf.data(), (int) buf.size());
		if (got <= 0 || !c.add(buf.data(), (size_t) got))
			break;
	}
	stb_vorbis_close(v);
	return true;
}

} // namespace

bool decodeAudio(const uint8_t* bytes, size_t size, SampleData& out, std::string& error, bool& cut) {
	cut = false;
	const AudioKind kind = audioKind(bytes, size);
	if (kind == KIND_NONE) {
		error = "NOT A SOUND FILE";
		return false;
	}
	if (kind == KIND_WAV)
		return decodeWav(bytes, size, out, error, cut);
	Collect c;
	bool ok = false;
	try {
		ok = kind == KIND_FLAC ? flac(bytes, size, c) : kind == KIND_MP3 ? mp3(bytes, size, c) : ogg(bytes, size, c);
	}
	catch (std::exception&) {
		error = "FILE TOO BIG";
		return false;
	}
	if (!ok) {
		error = "BROKEN FILE";
		return false;
	}
	if (c.d.l.empty()) {
		error = "EMPTY FILE";
		return false;
	}
	cut = c.cut;
	out = std::move(c.d);
	error.clear();
	return true;
}

} // namespace sampler
} // namespace jxmp

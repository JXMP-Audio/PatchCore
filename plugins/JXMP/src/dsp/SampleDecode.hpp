/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL formats: WAV (dr_wav), FLAC (dr_flac), MP3 (dr_mp3) and OGG Vorbis (stb_vorbis), told apart by their first
 * bytes. Every format decodes like WAV does: float, mono or stereo (more channels: the first two), the file's rate,
 * at most 60 s. Implemented in SampleDecode.cpp.
 */

#pragma once

#include "SampleFile.hpp"

#include <cstdint>
#include <string>

namespace jxmp {
namespace sampler {

enum AudioKind { KIND_NONE, KIND_WAV, KIND_FLAC, KIND_MP3, KIND_OGG };

/** What the first bytes say the file is */
AudioKind audioKind(const uint8_t* bytes, size_t size);

/** Decodes any of the four formats held in memory into `out`. On failure `out` stays as it was and `error` says why
 * ("NOT A SOUND FILE", "BROKEN FILE", "EMPTY FILE"). `cut` is set when the sound was longer than 60 s. */
bool decodeAudio(const uint8_t* bytes, size_t size, SampleData& out, std::string& error, bool& cut);

} // namespace sampler
} // namespace jxmp

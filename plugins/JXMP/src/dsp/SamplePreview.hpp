/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL browser preview (audio thread): the highlighted file once, whole, at its own pitch. A new file first fades
 * the old one out (the voice's 2 ms fade) and then starts from its beginning; none stops it. The samples are handed
 * over and kept by the UI, so nothing is ever freed here.
 */

#pragma once

#include "Sampler.hpp"

#include <memory>

namespace jxmp {
namespace sampler {

struct PreviewPlayer {
	std::shared_ptr<const SampleData> live, next;
	bool hasNext = false;
	Voice voice;
	SlotSettings set;

	/** Play `d` next (none = stop) */
	void request(std::shared_ptr<const SampleData> d) {
		next = d;
		hasNext = true;
		if (voice.active())
			voice.fadeOut();
	}

	/** One sample, before the slot's level and pan (sample units: ±1) */
	void process(float engineRate, float& l, float& r) {
		l = r = 0.f;
		if (hasNext && !voice.active()) {
			live = next;  // the old one: the UI still holds it
			next.reset();
			hasNext = false;
			voice = Voice();
			if (live) {
				set = defaults(live->frames());  // one shot, whole, no loop, no envelopes
				voice.trigger();
			}
		}
		if (!live)
			return;
		voice.process(*live, set, live->rate / engineRate, 1.f / engineRate, l, r);
		if (!live->stereo)
			r = l;
	}

	bool playing() const { return live && voice.active(); }
	bool stereo() const { return live && live->stereo; }
	/** Where it plays (frames of the file); negative when silent */
	double position() const { return playing() ? voice.position() : -1.0; }
};

} // namespace sampler
} // namespace jxmp

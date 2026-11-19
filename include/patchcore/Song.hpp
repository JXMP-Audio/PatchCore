/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Editor's song (JX-EDIT): 8 tracks of notes on one timeline. The player turns a range of the transport into MIDI
 * note on / off, each track on its own MIDI channel, which Cardinal's Host MIDI modules turn into cables (their channel,
 * polyphony and mode settings decide which tracks and how). No Rack dependency (tested).
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pcsong {

constexpr int kTracks = 8;
constexpr int64_t kPpq = 960;  // ticks per beat (a quarter note)

/** The transport's tempo range */
constexpr double kMinBpm = 20.0, kMaxBpm = 999.0;

/** The tempo to use after loading a song: the saved one (kept in range), or `current` when the patch has none
 *  (saved before the tempo was stored) or a broken one */
inline double savedTempo(bool present, double saved, double current) {
	if (!present || !(saved == saved))
		return current;
	return saved < kMinBpm ? kMinBpm : saved > kMaxBpm ? kMaxBpm : saved;
}

struct Note {
	int64_t start;   // ticks from the song start
	int64_t length;  // ticks, at least 1
	int pitch;       // MIDI note 0..127, 60 = C4
	int vel;         // 1..127

	int64_t end() const {
		return start + std::max<int64_t>(1, length);
	}

	bool operator==(const Note& o) const {
		return start == o.start && length == o.length && pitch == o.pitch && vel == o.vel;
	}
};

struct Track {
	std::string name;
	bool mute = false, solo = false;
	int channel = 0;  // the MIDI channel it plays on, 0..15 (several tracks may share one)
	std::vector<Note> notes;  // sorted by start

	/** Keeps the notes sorted by start (a later note at the same start goes after the others) */
	void add(const Note& n) {
		const auto at = std::upper_bound(notes.begin(), notes.end(), n.start,
		                                 [](int64_t s, const Note& o) { return s < o.start; });
		notes.insert(at, n);
	}

	void sort() {
		std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
	}

	bool operator==(const Track& o) const {
		return name == o.name && mute == o.mute && solo == o.solo && channel == o.channel && notes == o.notes;
	}
};

struct Song {
	Track tracks[kTracks];
	// Per MIDI channel, written into the Host MIDI module on that channel: polyphony channels 1..16 and the mode
	// (0 rotate, 1 reuse, 2 reset: Host MIDI's numbers). The tracks on one channel share them.
	int poly[16];
	int polyMode[16];

	Song() {
		for (int k = 0; k < kTracks; ++k) {
			tracks[k].name = "TRACK " + std::to_string(k + 1);
			tracks[k].channel = k;
		}
		for (int c = 0; c < 16; ++c) {
			poly[c] = 1;
			polyMode[c] = 0;
		}
	}

	bool operator==(const Song& o) const {
		for (int k = 0; k < kTracks; ++k)
			if (!(tracks[k] == o.tracks[k]))
				return false;
		for (int c = 0; c < 16; ++c)
			if (poly[c] != o.poly[c] || polyMode[c] != o.polyMode[c])
				return false;
		return true;
	}
	bool operator!=(const Song& o) const {
		return !(*this == o);
	}

	bool anySolo() const {
		for (const Track& t : tracks)
			if (t.solo)
				return true;
		return false;
	}

	/** A muted track is silent; with any track on SOLO only the soloed ones (not muted) sound */
	bool audible(int k) const {
		const Track& t = tracks[k];
		if (t.mute)
			return false;
		return !anySolo() || t.solo;
	}

	/** Where the last note ends (0 for an empty song) */
	int64_t lastTick() const {
		int64_t last = 0;
		for (const Track& t : tracks)
			for (const Note& n : t.notes)
				last = std::max(last, n.end());
		return last;
	}
};

/** Plays a song on the audio thread, part by part. No allocation in render(). */
struct SongPlayer {
	static constexpr int kMaxHeld = 128;
	static constexpr int kMaxEvents = 256;

	struct Held {
		double end;  // tick
		uint8_t track, channel, pitch;  // the note off goes to the channel of its note on
	};
	Held held[kMaxHeld];
	int heldCount = 0;
	/** Every note ends this many frames early (at most half of it), and after a jump the notes start this late: a mono
	 * Host MIDI then sees its gate fall between two notes that touch, so the envelope starts again on each one (with
	 * the end and the next start on one frame the gate stayed high and only the first note had its attack). 0 = off. */
	uint32_t gapFrames = 0;
	double expected = -1.0;  // the tick where the next part should start; anything else is a jump

	struct Event {
		uint32_t frame;
		uint8_t status, d1, d2;
		uint16_t order;  // note offs before note ons on the same frame, otherwise as generated
	};
	Event events[kMaxEvents];

	/** Renders `frames` frames from tick `t0` at `framesPerTick`; `emit(frame, status, data1, data2)` gets the MIDI
	 * events sorted by frame. A part that does not continue the previous one (loop, a click on the ruler, stop) ends
	 * the held notes on its first frame; a note that started before `t0` does not sound (as in a DAW). */
	template <typename Emit>
	void render(const Song* const song, const double t0, const uint32_t frames, const double framesPerTick, const bool playing,
	            Emit&& emit) {
		int count = 0;
		auto push = [&](uint32_t frame, uint8_t status, uint8_t d1, uint8_t d2) {
			if (count < kMaxEvents) {
				events[count] = Event{frame, status, d1, d2, (uint16_t) count};
				++count;
			}
		};
		auto lastFrame = [&]() { return frames > 0 ? frames - 1 : 0u; };
		auto frameOf = [&](double tick) {
			const double f = std::ceil((tick - t0) * framesPerTick - 1e-9);
			return (uint32_t) std::min<double>(std::max(0.0, f), lastFrame());
		};

		const bool jump = expected >= 0.0 && std::fabs(t0 - expected) > 1e-6;
		const bool endedHeld = heldCount > 0;
		if (song == nullptr || !playing || jump) {
			for (int k = 0; k < heldCount; ++k)
				push(0, (uint8_t)(0x80 | held[k].channel), held[k].pitch, 0);
			heldCount = 0;
		}
		// Notes start a little late right after held notes were ended at a jump (see gapFrames)
		const uint32_t firstOn = jump && endedHeld ? std::min(gapFrames, lastFrame()) : 0u;
		const double gap = framesPerTick > 0.0 ? gapFrames / framesPerTick : 0.0;
		auto offTick = [&](const Note& n) {
			const double e = (double) n.end();
			return std::max((double) n.start + (e - (double) n.start) * 0.5, e - gap);
		};
		if (song == nullptr || !playing || frames == 0 || framesPerTick <= 0.0) {
			expected = -1.0;
			flush(count, emit);
			return;
		}
		const double t1 = t0 + frames / framesPerTick;

		// Ends due in this part (and every note of a track that went silent)
		for (int k = 0; k < heldCount;) {
			const Held& h = held[k];
			const bool silent = !song->audible(h.track);
			if (silent || h.end < t1) {
				push(silent ? 0 : frameOf(h.end), (uint8_t)(0x80 | h.channel), h.pitch, 0);
				held[k] = held[--heldCount];
			}
			else
				++k;
		}
		// Starts in [t0, t1)
		for (int tr = 0; tr < kTracks; ++tr) {
			if (!song->audible(tr))
				continue;
			const std::vector<Note>& notes = song->tracks[tr].notes;
			const uint8_t ch = (uint8_t)(song->tracks[tr].channel & 0x0F);
			auto it = std::lower_bound(notes.begin(), notes.end(), t0, [](const Note& n, double s) { return (double) n.start < s; });
			for (; it != notes.end() && (double) it->start < t1; ++it) {
				const uint8_t pitch = (uint8_t) std::max(0, std::min(127, it->pitch));
				const uint32_t on = std::max(frameOf((double) it->start), firstOn);
				const double end = offTick(*it);
				// A note is only started when its end is sure: room for both events in this block, and for the
				// note in the held list when it ends in a later block (otherwise it would never get its note-off)
				if (count + 2 > kMaxEvents || (end >= t1 && heldCount >= kMaxHeld))
					continue;
				push(on, (uint8_t)(0x90 | ch), pitch, (uint8_t) std::max(1, std::min(127, it->vel)));
				// The end after its own start (never before it on the same frame)
				if (end < t1 && frameOf(end) > on)
					push(frameOf(end), (uint8_t)(0x80 | ch), pitch, 0);
				else if (end < t1 && on + 1 <= lastFrame())
					push(on + 1, (uint8_t)(0x80 | ch), pitch, 0);
				else if (heldCount < kMaxHeld)
					held[heldCount++] = Held{std::max(end, t1), (uint8_t) tr, ch, pitch};
			}
		}
		expected = t1;
		flush(count, emit);
	}

	template <typename Emit>
	void flush(const int count, Emit& emit) {
		std::sort(events, events + count, [](const Event& a, const Event& b) {
			if (a.frame != b.frame)
				return a.frame < b.frame;
			const bool aOff = (a.status & 0xF0) == 0x80, bOff = (b.status & 0xF0) == 0x80;
			if (aOff != bOff)
				return aOff;
			return a.order < b.order;
		});
		for (int k = 0; k < count; ++k)
			emit(events[k].frame, events[k].status, events[k].d1, events[k].d2);
	}
};

/** The Editor's note preview: the UI asks for one sounding note (packed in one int, 0 = none), the audio thread turns
 * each change into a note off of the old one and a note on of the new one. */
struct NotePreview {
	static int32_t pack(int channel, int pitch, int vel) {
		return 0x40000000 | (channel & 15) << 16 | (std::max(0, std::min(127, pitch))) << 8 | std::max(1, std::min(127, vel));
	}
	int32_t playing = 0;

	/** `emit(status, data1, data2)` */
	template <typename Emit>
	void render(const int32_t wanted, Emit&& emit) {
		if (wanted == playing)
			return;
		if (playing)
			emit((uint8_t)(0x80 | ((playing >> 16) & 15)), (uint8_t)((playing >> 8) & 127), (uint8_t) 0);
		if (wanted)
			emit((uint8_t)(0x90 | ((wanted >> 16) & 15)), (uint8_t)((wanted >> 8) & 127), (uint8_t)(wanted & 127));
		playing = wanted;
	}
};

/** Keyboard notes from the audio thread to the Editor (one writer, one reader, no locks). A full ring drops the newest. */
struct RecordRing {
	static constexpr uint32_t kSize = 1024;
	struct Ev {
		double tick;
		uint8_t track, status, d1, d2;
	};
	Ev buf[kSize];
	std::atomic<uint32_t> w{0}, r{0};

	bool push(const Ev& e) {
		const uint32_t wi = w.load(std::memory_order_relaxed);
		if (wi - r.load(std::memory_order_acquire) >= kSize)
			return false;
		buf[wi % kSize] = e;
		w.store(wi + 1, std::memory_order_release);
		return true;
	}

	bool pop(Ev& e) {
		const uint32_t ri = r.load(std::memory_order_relaxed);
		if (ri == w.load(std::memory_order_acquire))
			return false;
		e = buf[ri % kSize];
		r.store(ri + 1, std::memory_order_release);
		return true;
	}
};

/** UI thread: pairs the recorded note ons and offs into notes of the song (overdub: added to what is there) */
struct Recorder {
	struct Open {
		bool on = false;
		double start = 0.0;
		int vel = 0;
	};
	Open open[kTracks][128];

	void close(Song& song, int track, int pitch, double end) {
		Open& o = open[track][pitch];
		if (!o.on)
			return;
		o.on = false;
		const int64_t start = (int64_t) std::floor(o.start);
		song.tracks[track].add(Note{start, std::max<int64_t>(1, (int64_t) std::llround(end - o.start)), pitch, o.vel});
	}

	/** `loopEnd`: where a note held over the loop end ends (the transport jumped back meanwhile) */
	void feed(const RecordRing::Ev& e, Song& song, const double loopEnd) {
		if (e.track >= kTracks || e.d1 > 127)
			return;
		const uint8_t type = e.status & 0xF0;
		Open& o = open[e.track][e.d1];
		if (type == 0x90 && e.d2 > 0) {
			if (o.on)
				close(song, e.track, e.d1, e.tick);
			o.on = true;
			o.start = e.tick;
			o.vel = e.d2;
		}
		else if (type == 0x80 || type == 0x90) {
			close(song, e.track, e.d1, e.tick < (o.on ? o.start : 0.0) ? loopEnd : e.tick);
		}
	}

	/** Recording stopped at `tick`: the held notes end there */
	void finish(const double tick, Song& song) {
		for (int t = 0; t < kTracks; ++t)
			for (int p = 0; p < 128; ++p)
				if (open[t][p].on)
					close(song, t, p, std::max(tick, open[t][p].start + 1.0));
	}
};

/** The song the audio thread plays. The UI publishes immutable snapshots; a replaced one is freed only a second later,
 * so the audio thread never reads freed memory (it holds a snapshot for one audio block at most). */
struct SongStore {
	std::atomic<const Song*> live{nullptr};
	struct Retired {
		const Song* song;
		double since;
	};
	std::vector<Retired> retired;

	~SongStore() {
		delete live.load();
		for (const Retired& r : retired)
			delete r.song;
	}

	const Song* current() const {
		return live.load(std::memory_order_acquire);
	}

	/** The snapshot the audio thread is reading now (a hazard pointer): never freed while it is set */
	std::atomic<const Song*> reading{nullptr};

	/** Audio thread: holds the played snapshot for one block */
	struct Reader {
		SongStore& store;
		const Song* song;
		explicit Reader(SongStore& s) : store(s) {
			do {
				song = store.live.load();
				store.reading.store(song);
			} while (song != store.live.load());
		}
		~Reader() {
			store.reading.store(nullptr);
		}
	};

	/** UI thread: `song` (new, owned from now on, or nullptr) becomes the played one; `now` in seconds */
	void publish(const Song* const song, const double now) {
		const Song* const old = live.exchange(song, std::memory_order_acq_rel);
		if (old != nullptr)
			retired.push_back(Retired{old, now});
		collect(now);
	}

	/** UI thread: frees snapshots replaced more than a second ago */
	void collect(const double now) {
		for (size_t k = 0; k < retired.size();) {
			if (now - retired[k].since > 1.0 && retired[k].song != reading.load()) {
				delete retired[k].song;
				retired[k] = retired.back();
				retired.pop_back();
			}
			else
				++k;
		}
	}

	size_t retiredCount() const {
		return retired.size();
	}
};

} // namespace pcsong

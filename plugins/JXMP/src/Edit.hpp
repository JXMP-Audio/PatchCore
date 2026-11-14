/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-EDIT: enables PatchCore's Editor (F4) and keeps its song in the patch. Each track plays on its own MIDI channel
 * (several may share one) into the rack's MIDI input, so Cardinal's Host MIDI modules on those channels turn the tracks
 * into cables; polyphony and mode are kept per channel and synced both ways with its Host MIDI. Standalone only.
 */

#pragma once

#include "JXMPWidget.hpp"
#include "plugincontext.hpp"
#include <patchcore/Song.hpp>

#include <deque>
#include <memory>

namespace jxedit {

namespace sg = ::pcsong;

const NVGcolor kEditColour = nvgRGB(0xd6, 0x4f, 0x8f);
const NVGcolor kTrackColours[sg::kTracks] = {
    nvgRGB(0xe8, 0x48, 0x3c), nvgRGB(0xff, 0x8a, 0x1c), nvgRGB(0xe8, 0xc5, 0x3a), nvgRGB(0x5d, 0xd1, 0x6a),
    nvgRGB(0x3f, 0xc1, 0xb0), nvgRGB(0x4a, 0x90, 0xe8), nvgRGB(0x9a, 0x6c, 0xe8), nvgRGB(0xd6, 0x4f, 0x8f),
};

struct Edit : engine::Module {
	/** The editable song (UI thread). The audio thread plays published copies of it. */
	sg::Song song;
	int selected = 0;
	sg::Recorder recorder;
	bool wasRecording = false;
	std::deque<sg::Song> undo;
	/** The view of the Editor, kept with the patch */
	double zoom = 0.12;  // px per tick
	double scroll = 0.0;  // ticks at the left edge
	int topPitch = 84;
	int overviewBars = 32;  // how much the overview shows at least (Ctrl+wheel over it)
	int64_t grid = sg::kPpq / 4;
	/** A new note is as long as the last one drawn, stretched or clicked (0 = one grid step) */
	int64_t drawLength = 0;

	Edit();
	~Edit();

	/** How many tracks play on MIDI channel `c` */
	int tracksOn(int c) const;
	/** The project's editor: the first one added. Others stay inactive. */
	static std::atomic<Edit*>& owner();
	bool active() const;
	static bool standalone();

	CardinalPluginContext* context() const;
	/** The audio thread plays the current state of `song` from now on; the keyboard plays the selected track */
	void publish();
	/** Before a change of the song: it becomes a step for Ctrl+Z once the song really differs from it (a click that
	 * changes nothing leaves no step) */
	void remember();
	/** The step remembered last goes onto the undo list if the song changed since; `settle` = the gesture is over,
	 * an unchanged one is dropped */
	void commit(bool settle);
	bool canUndo() const;
	bool undoStep();
	std::unique_ptr<sg::Song> pending;
	/** The transport position in ticks */
	double playTick() const;
	double sampleRate() const;
	/** UI thread, every frame: takes the recorded keyboard notes into the song */
	void collectRecording();
	/** UI thread: writes each track's polyphony and mode into the Host MIDI modules on its channel and notes which
	 * tracks have one. `force` = now, otherwise twice a second (a Host MIDI added or its channel changed). */
	void syncHostMidi(bool force);
	/** MUTE and SOLO as on the mixer: one of them per track (switching one on switches the other off) */
	void toggleMute(int k);
	void toggleSolo(int k);
	/** MUTE LED: 1 lit (muted by the user), blinking while another track's solo silences it, else 0 */
	float muteLight(int k) const;
	/** Per MIDI channel: a Host MIDI on it is in the rack */
	bool hostMidi[16] = {};
	/** Per MIDI channel: its Host MIDI is in MPE mode (set there; the Editor leaves the mode alone until MODE is
	 * clicked) */
	bool hostMpe[16] = {};
	/** Per MIDI channel: polyphony / mode were just changed in the Editor, so they go into the Host MIDI even over MPE */
	bool editorWins[16] = {};
	/** How many Host MIDI modules listen on "All" channels (they play every track) */
	int hostMidiAll = 0;
	/** Per MIDI channel, what the song and its Host MIDI last agreed on (-1 = not yet): a side that differs from it was
	 * changed by the user and wins, so polyphony and mode can be set in either place */
	int agreedPoly[16];
	int agreedMode[16];
	/** UI thread: the keyboard plays the selected track while the Editor is open or REC is on, else as it sends */
	void routeKeyboard(bool editorOpen);
	double lastSync = -1.0;

	void onAdd(const AddEvent& e) override;
	void onRemove(const RemoveEvent& e) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;
};

/** The first `n` letters of UTF-8 text (never half a letter: names with accents) */
inline std::string utf8Cut(const std::string& s, size_t n) {
	size_t i = 0, count = 0;
	while (i < s.size() && count < n) {
		++count;
		++i;
		while (i < s.size() && (s[i] & 0xC0) == 0x80)
			++i;
	}
	return s.substr(0, i);
}

/** The Editor's content over the rack area (EditView.cpp) */
widget::Widget* createEditPane(Edit* module);

} // namespace jxedit

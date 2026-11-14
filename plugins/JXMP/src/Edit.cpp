/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-EDIT: the module behind PatchCore's Editor. See Edit.hpp.
 */

#include "Edit.hpp"

// Local UI tests (src/patchcore/tests, not in git): built in only where that file exists
#if __has_include("../../../src/patchcore/tests/edit_uitest.inc")
#define JXEDIT_UITEST 1
namespace jxedit {
namespace uitest {
void step(Edit* edit);
}
}
#endif
#include <patchcore/Editor.hpp>
#include <patchcore/Mixer.hpp>
#include <system.hpp>

namespace rack {
bool isStandalone();
}

namespace jxedit {

namespace st = jxmp::style;

Edit::Edit() {
	config(0, 0, 0, 0);
	for (int c = 0; c < 16; ++c)
		agreedPoly[c] = agreedMode[c] = -1;
	Edit* expected = nullptr;
	owner().compare_exchange_strong(expected, this);
}

Edit::~Edit() {
	Edit* expected = this;
	owner().compare_exchange_strong(expected, nullptr);
}

std::atomic<Edit*>& Edit::owner() {
	static std::atomic<Edit*> o(nullptr);
	return o;
}

bool Edit::active() const {
	return owner().load() == this && standalone();
}

bool Edit::standalone() {
	return rack::isStandalone();
}

CardinalPluginContext* Edit::context() const {
	return static_cast<CardinalPluginContext*>(APP);
}

double Edit::sampleRate() const {
	return APP && APP->engine ? std::max(1.f, APP->engine->getSampleRate()) : 48000.0;
}

double Edit::playTick() const {
	CardinalPluginContext* const c = context();
	if (!c)
		return 0.0;
	const double framesPerTick = sampleRate() * 60.0 / std::max(1.0, (double) c->internalTransportBeatsPerMinute) / sg::kPpq;
	return c->internalTransportFrame / framesPerTick;
}

void Edit::publish() {
	CardinalPluginContext* const c = context();
	if (!c || !active())
		return;
	c->editorSong.publish(new sg::Song(song), system::getTime());
}

void Edit::routeKeyboard(const bool editorOpen) {
	CardinalPluginContext* const c = context();
	if (!c || !active())
		return;
	const int k = math::clamp(selected, 0, sg::kTracks - 1);
	c->editorKeyboardChannel = song.tracks[k].channel & 0x0F;
	c->editorKeyboardTrack = editorOpen || c->internalTransportRecord ? k : -1;
	// The Editor closed while a note was heard: silence it
	if (!editorOpen)
		c->editorPreview = 0;
}

int Edit::tracksOn(const int c) const {
	int n = 0;
	for (const sg::Track& t : song.tracks)
		n += t.channel == c;
	return n;
}

void Edit::remember() {
	commit(true);
	pending.reset(new sg::Song(song));
}

void Edit::commit(const bool settle) {
	if (!pending)
		return;
	if (*pending != song) {
		undo.push_back(*pending);
		while (undo.size() > 100)
			undo.pop_front();
		pending.reset();
	}
	else if (settle)
		pending.reset();
}

bool Edit::canUndo() const {
	return !undo.empty() || (pending && *pending != song);
}

bool Edit::undoStep() {
	commit(true);
	if (undo.empty())
		return false;
	song = undo.back();
	undo.pop_back();
	publish();
	return true;
}

void Edit::collectRecording() {
	CardinalPluginContext* const c = context();
	if (!c || !active())
		return;
	const double loopEnd = c->internalTransportLoop ? c->internalTransportLoopEnd * sg::kPpq : 1e18;
	const bool recording = c->internalTransportRecord && c->internalTransportPlaying;
	// One undo step per take
	if (recording && !wasRecording)
		remember();
	sg::RecordRing::Ev e;
	bool changed = false;
	while (c->editorRecord.pop(e)) {
		recorder.feed(e, song, loopEnd);
		changed = true;
	}
	if (wasRecording && !recording) {
		recorder.finish(playTick(), song);
		changed = true;
	}
	wasRecording = recording;
	if (changed)
		publish();
}

void Edit::syncHostMidi(const bool force) {
	if (!active() || !APP || !APP->engine)
		return;
	const double now = system::getTime();
	if (!force && now - lastSync < 0.5)
		return;
	lastSync = now;
	bool found[16] = {}, mpe[16] = {};
	int all = 0;
	for (const int64_t id : APP->engine->getModuleIds()) {
		engine::Module* const hm = APP->engine->getModule(id);
		if (!hm || !hm->model || !hm->model->plugin || hm->model->slug != "HostMIDI" || hm->model->plugin->slug != "Cardinal")
			continue;
		json_t* const dataJ = hm->dataToJson();
		if (!dataJ)
			continue;
		// "inputChannel": 0 = all channels (every track: left alone), 1..16
		const int ch = (int) json_integer_value(json_object_get(dataJ, "inputChannel"));
		if (ch == 0)
			++all;
		if (ch >= 1 && ch <= 16) {
			const int c = ch - 1;
			const int hmPoly = math::clamp((int) json_integer_value(json_object_get(dataJ, "channels")), 1, 16);
			const int hmMode = (int) json_integer_value(json_object_get(dataJ, "polyMode"));
			const bool hmMpe = hmMode == 3;  // MPE: chosen in the Host MIDI, left alone from here
			// The first Host MIDI of a channel with a track decides what the song takes over, unless the user just
			// changed it in the Editor (then the Editor wins)
			if (!found[c] && tracksOn(c) > 0 && !editorWins[c]) {
				bool take = false;
				if (agreedPoly[c] >= 0)
					// Changed in the Host MIDI (its menu) while the song kept the agreed value
					take = song.poly[c] == agreedPoly[c] && song.polyMode[c] == agreedMode[c]
					       && (hmPoly != agreedPoly[c] || (!hmMpe && hmMode != agreedMode[c]));
				else
					// Nothing agreed yet (a track just moved onto this channel, or the Host MIDI is new): one already
					// set up (not the default 1 voice, rotate) keeps its settings
					take = hmPoly != 1 || (hmMode != 0 && !hmMpe);
				if (take && (song.poly[c] != hmPoly || (!hmMpe && song.polyMode[c] != hmMode))) {
					song.poly[c] = hmPoly;
					if (hmMode >= 0 && hmMode <= 2)
						song.polyMode[c] = hmMode;
					publish();
				}
			}
			if (!found[c])
				mpe[c] = hmMpe;
			found[c] = true;
			// Only the channels a track plays on are set from here
			if (tracksOn(c) > 0) {
				const int poly = math::clamp(song.poly[c], 1, 16), mode = math::clamp(song.polyMode[c], 0, 2);
				agreedPoly[c] = poly;
				agreedMode[c] = mode;
				const bool keepMode = hmMpe && !editorWins[c];
				if (hmPoly != poly || (!keepMode && hmMode != mode)) {
					// The whole data back with the values changed (a missing key would reset some of its settings)
					json_object_set_new(dataJ, "channels", json_integer(poly));
					if (!keepMode)
						json_object_set_new(dataJ, "polyMode", json_integer(mode));
					hm->dataFromJson(dataJ);
				}
			}
			else {
				agreedPoly[c] = agreedMode[c] = -1;
			}
		}
		json_decref(dataJ);
	}
	hostMidiAll = all;
	for (int c = 0; c < 16; ++c) {
		hostMidi[c] = found[c];
		hostMpe[c] = found[c] && mpe[c] && !editorWins[c];
		editorWins[c] = false;
		// No Host MIDI on the channel: a new one takes the song's settings
		if (!found[c])
			agreedPoly[c] = agreedMode[c] = -1;
	}
}

void Edit::toggleMute(const int k) {
	remember();
	sg::Track& t = song.tracks[k];
	t.mute = !t.mute;
	if (t.mute)
		t.solo = false;
	publish();
}

void Edit::toggleSolo(const int k) {
	remember();
	sg::Track& t = song.tracks[k];
	t.solo = !t.solo;
	if (t.solo)
		t.mute = false;
	publish();
}

float Edit::muteLight(const int k) const {
	const sg::Track& t = song.tracks[k];
	if (t.mute)
		return 1.f;
	if (song.anySolo() && !t.solo)
		return std::fmod(system::getTime() * 2.0, 1.0) < 0.5 ? 0.85f : 0.f;
	return 0.f;
}

void Edit::onAdd(const AddEvent& e) {
	engine::Module::onAdd(e);
	publish();
}

void Edit::onRemove(const RemoveEvent& e) {
	CardinalPluginContext* const c = context();
	if (c && owner().load() == this) {
		c->editorSong.publish(nullptr, system::getTime());
		c->editorKeyboardTrack = -1;
		c->internalTransportRecord = false;
	}
	engine::Module::onRemove(e);
}

json_t* Edit::dataToJson() {
	json_t* const rootJ = json_object();
	json_t* const tracksJ = json_array();
	for (const sg::Track& t : song.tracks) {
		json_t* const tJ = json_object();
		json_object_set_new(tJ, "name", json_string(t.name.c_str()));
		json_object_set_new(tJ, "mute", json_boolean(t.mute));
		json_object_set_new(tJ, "solo", json_boolean(t.solo));
		json_object_set_new(tJ, "channel", json_integer(t.channel + 1));
		// start, length, pitch, velocity per note
		json_t* const nJ = json_array();
		for (const sg::Note& n : t.notes) {
			json_array_append_new(nJ, json_integer(n.start));
			json_array_append_new(nJ, json_integer(n.length));
			json_array_append_new(nJ, json_integer(n.pitch));
			json_array_append_new(nJ, json_integer(n.vel));
		}
		json_object_set_new(tJ, "notes", nJ);
		json_array_append_new(tracksJ, tJ);
	}
	json_object_set_new(rootJ, "tracks", tracksJ);
	json_t* const polyJ = json_array();
	json_t* const modeJ = json_array();
	for (int c = 0; c < 16; ++c) {
		json_array_append_new(polyJ, json_integer(song.poly[c]));
		json_array_append_new(modeJ, json_integer(song.polyMode[c]));
	}
	json_object_set_new(rootJ, "poly", polyJ);
	json_object_set_new(rootJ, "polyMode", modeJ);
	json_object_set_new(rootJ, "selected", json_integer(selected));
	json_object_set_new(rootJ, "zoom", json_real(zoom));
	json_object_set_new(rootJ, "scroll", json_real(scroll));
	json_object_set_new(rootJ, "topPitch", json_integer(topPitch));
	json_object_set_new(rootJ, "overviewBars", json_integer(overviewBars));
	json_object_set_new(rootJ, "drawLength", json_integer(drawLength));
	json_object_set_new(rootJ, "grid", json_integer(grid));
	if (CardinalPluginContext* const c = context()) {
		json_object_set_new(rootJ, "loop", json_boolean(c->internalTransportLoop));
		json_object_set_new(rootJ, "loopStart", json_real(c->internalTransportLoopStart));
		json_object_set_new(rootJ, "loopEnd", json_real(c->internalTransportLoopEnd));
		json_object_set_new(rootJ, "click", json_boolean(c->internalTransportMetronome));
	}
	return rootJ;
}

void Edit::dataFromJson(json_t* const rootJ) {
	sg::Song loaded;
	if (json_t* const tracksJ = json_object_get(rootJ, "tracks")) {
		for (int k = 0; k < sg::kTracks && k < (int) json_array_size(tracksJ); ++k) {
			json_t* const tJ = json_array_get(tracksJ, k);
			sg::Track& t = loaded.tracks[k];
			if (json_t* const j = json_object_get(tJ, "name"))
				t.name = json_string_value(j) ? json_string_value(j) : t.name;
			t.mute = json_is_true(json_object_get(tJ, "mute"));
			t.solo = json_is_true(json_object_get(tJ, "solo"));
			if (json_t* const j = json_object_get(tJ, "channel"))
				t.channel = math::clamp((int) json_integer_value(j), 1, 16) - 1;
			// Earlier patches kept the polyphony on the track
			if (json_t* const j = json_object_get(tJ, "channels"))
				loaded.poly[t.channel] = math::clamp((int) json_integer_value(j), 1, 16);
			if (json_t* const j = json_object_get(tJ, "mode"))
				loaded.polyMode[t.channel] = math::clamp((int) json_integer_value(j), 0, 2);
			json_t* const nJ = json_object_get(tJ, "notes");
			const size_t n = json_array_size(nJ) / 4;
			t.notes.reserve(n);
			for (size_t i = 0; i < n; ++i) {
				sg::Note note;
				note.start = std::max<json_int_t>(0, json_integer_value(json_array_get(nJ, 4 * i + 0)));
				note.length = std::max<json_int_t>(1, json_integer_value(json_array_get(nJ, 4 * i + 1)));
				note.pitch = math::clamp((int) json_integer_value(json_array_get(nJ, 4 * i + 2)), 0, 127);
				note.vel = math::clamp((int) json_integer_value(json_array_get(nJ, 4 * i + 3)), 1, 127);
				t.notes.push_back(note);
			}
			t.sort();
		}
	}
	json_t* const polyJ = json_object_get(rootJ, "poly");
	json_t* const modeJ = json_object_get(rootJ, "polyMode");
	for (int c = 0; c < 16 && c < (int) json_array_size(polyJ); ++c)
		loaded.poly[c] = math::clamp((int) json_integer_value(json_array_get(polyJ, c)), 1, 16);
	for (int c = 0; c < 16 && c < (int) json_array_size(modeJ); ++c)
		loaded.polyMode[c] = math::clamp((int) json_integer_value(json_array_get(modeJ, c)), 0, 2);
	song = loaded;
	undo.clear();
	for (int c = 0; c < 16; ++c)
		agreedPoly[c] = agreedMode[c] = -1;
	selected = math::clamp((int) json_integer_value(json_object_get(rootJ, "selected")), 0, sg::kTracks - 1);
	if (json_t* const j = json_object_get(rootJ, "zoom"))
		zoom = std::min(2.0, std::max(0.005, json_number_value(j)));
	if (json_t* const j = json_object_get(rootJ, "scroll"))
		scroll = std::max(0.0, json_number_value(j));
	if (json_t* const j = json_object_get(rootJ, "overviewBars"))
		overviewBars = math::clamp((int) json_integer_value(j), 8, 512);
	if (json_t* const j = json_object_get(rootJ, "drawLength"))
		drawLength = std::max<int64_t>(0, json_integer_value(j));
	if (json_t* const j = json_object_get(rootJ, "topPitch"))
		topPitch = math::clamp((int) json_integer_value(j), 12, 127);
	if (json_t* const j = json_object_get(rootJ, "grid"))
		grid = std::min<int64_t>(4 * sg::kPpq, std::max<int64_t>(1, json_integer_value(j)));
	CardinalPluginContext* const c = context();
	if (c && owner().load() == this) {
		if (json_t* const j = json_object_get(rootJ, "loopStart"))
			c->internalTransportLoopStart = std::max(0.0, json_number_value(j));
		if (json_t* const j = json_object_get(rootJ, "loopEnd"))
			c->internalTransportLoopEnd = std::max(c->internalTransportLoopStart + 0.25, json_number_value(j));
		c->internalTransportLoop = json_is_true(json_object_get(rootJ, "loop"));
		c->internalTransportMetronome = json_is_true(json_object_get(rootJ, "click"));
	}
	publish();
}


// ---------------------------------------------------------------------------------------------------------
// Panel

/** Is a note of track `k` sounding at the transport position (the activity LED) */
static bool sounding(const Edit* m, int k, double tick) {
	CardinalPluginContext* const c = m->context();
	if (!c || !c->internalTransportPlaying || !m->song.audible(k))
		return false;
	for (const sg::Note& n : m->song.tracks[k].notes) {
		if (n.start > tick)
			break;
		if (n.end() > tick)
			return true;
	}
	return false;
}

/** One track row: LED, number and name on a VFD strip, MUTE and SOLO. A click on the name picks the track, a double
 * click picks it and opens the Editor. */
struct TrackRow : widget::OpaqueWidget {
	Edit* module = nullptr;
	int track = 0;
	math::Vec lastPress;  // a double click carries no position

	math::Rect muteRect() const {
		return math::Rect(box.size.x - mm2px(13.f), 0, mm2px(6.f), box.size.y);
	}
	math::Rect soloRect() const {
		return math::Rect(box.size.x - mm2px(6.5f), 0, mm2px(6.f), box.size.y);
	}

	void onButton(const ButtonEvent& e) override {
		// Ctrl+drag selects modules: the click goes on to the rack
		if ((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL)
			return;
		OpaqueWidget::onButton(e);
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		lastPress = e.pos;
		if (muteRect().contains(e.pos))
			module->toggleMute(track);
		else if (soloRect().contains(e.pos))
			module->toggleSolo(track);
		else {
			module->selected = track;
			module->publish();
		}
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (module && module->active() && !muteRect().contains(lastPress) && !soloRect().contains(lastPress)) {
			patchcore::showEditor(true);
			e.consume(this);
		}
	}

	void drawButton(NVGcontext* vg, math::Rect r, const char* legend, float b, NVGcolor lit) {
		const bool on = b > 0.4f;
		r = r.grow(math::Vec(-1.f, -1.5f));
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(r), 2.5f);
		nvgFillColor(vg, color::lerp(nvgRGB(0x3a, 0x3c, 0x42), lit, b));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.7f);
		nvgStrokeColor(vg, nvgRGB(0x0a, 0x0a, 0x0b));
		nvgStroke(vg);
		st::text(vg, r.getCenter(), legend, 8.f, on ? nvgRGB(0x1a, 0x12, 0x10) : st::legendDim(), st::FONT_LABEL,
		         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.4f);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const NVGcolor col = kTrackColours[track];
		const bool sel = module && module->selected == track;
		const std::string name = module ? module->song.tracks[track].name : string::f("TRACK %d", track + 1);
		// VFD strip
		const float stripW = box.size.x - mm2px(15.f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 1.f, stripW, box.size.y - 2.f, 2.f);
		nvgFillColor(vg, sel ? nvgRGB(0x12, 0x2a, 0x24) : nvgRGB(0x0b, 0x16, 0x14));
		nvgFill(vg);
		if (sel) {
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, color::alpha(col, 0.8f));
			nvgStroke(vg);
		}
		st::vfdText(vg, math::Vec(mm2px(4.2f), box.size.y / 2), string::f("%d", track + 1), 8.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		std::string shown = utf8Cut(name, 9);
		st::vfdText(vg, math::Vec(mm2px(7.f), box.size.y / 2), string::uppercase(shown), 8.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		// Its MIDI channel at the right end of the display, dimmer
		const int ch = module ? module->song.tracks[track].channel + 1 : track + 1;
		st::vfdText(vg, math::Vec(stripW - mm2px(1.2f), box.size.y / 2), string::f("%d", ch), 7.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
		            color::alpha(st::vfdLit(), 0.55f));
		const sg::Track* t = module ? &module->song.tracks[track] : nullptr;
		drawButton(vg, muteRect(), "M", module ? module->muteLight(track) : 0.f, nvgRGB(0xe8, 0x3a, 0x30));
		drawButton(vg, soloRect(), "S", t && t->solo ? 1.f : 0.f, nvgRGB(0xff, 0x8a, 0x1c));
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		// The LED in the colour of the track, lit while one of its notes sounds
		NVGcontext* const vg = args.vg;
		const bool on = module && sounding(module, track, module->playTick());
		const NVGcolor col = kTrackColours[track];
		const math::Vec c(mm2px(1.9f), box.size.y / 2);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, 2.6f);
		nvgFillColor(vg, on ? col : color::mult(col, 0.3f));
		nvgFill(vg);
		if (on) {
			nvgBeginPath(vg);
			nvgCircle(vg, c.x, c.y, 8.f);
			nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, 2.f, 8.f, color::alpha(col, 0.5f), color::alpha(col, 0.f)));
			nvgFill(vg);
		}
	}
};

/** The hint under the tracks, or why the Editor is off */
struct EditNotice : widget::TransparentWidget {
	Edit* module = nullptr;
	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const char* text = nullptr;
		bool warn = true;
		if (!Edit::standalone())
			text = "STANDALONE ONLY";
		else if (module && !module->active())
			text = "SECOND EDITOR - INACTIVE";
		else {
			text = "DOUBLE-CLICK OR F4 OPENS THE EDITOR";
			warn = false;
		}
		if (warn) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
			nvgFillColor(vg, nvgRGBA(0xc8, 0x30, 0x28, 0xe0));
			nvgFill(vg);
		}
		st::text(vg, box.size.div(2), text, 7.f, warn ? color::WHITE : st::legendDim(), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.5f);
	}
};

struct EditWidget : JXMPModuleWidget, patchcore::EditorProvider {
	Edit* edit;

	explicit EditWidget(Edit* const module) : edit(module) {
		setup(module, "JX-EDIT", kEditColour, 12);
		styled();
		const float w = 60.96f;
		section(3.f, 19.f, w - 6.f, 86.f, "TRACKS · MIDI CH");
		for (int k = 0; k < sg::kTracks; ++k) {
			TrackRow* const row = new TrackRow;
			row->module = module;
			row->track = k;
			row->box = math::Rect(mm2px(math::Vec(5.5f, 25.f + k * 9.6f)), mm2px(math::Vec(w - 11.f, 7.4f)));
			addChild(row);
		}
		EditNotice* const notice = new EditNotice;
		notice->module = module;
		notice->box = math::Rect(mm2px(math::Vec(4.f, 108.f)), mm2px(math::Vec(w - 8.f, 6.f)));
		addChild(notice);
	}

	void drawBackLegends(NVGcontext* const vg) override {
		st::text(vg, mm2px(math::Vec(30.48f, 40.f)), "NO JACKS", 8.f, decor->stripe, st::FONT_LABEL);
		const char* lines[] = {"EACH TRACK PLAYS ON ITS MIDI", "CHANNEL (SET IN THE EDITOR).", "A HOST MIDI ON IT PLAYS IT;",
		                       "POLYPHONY: EDITOR OR HOST MIDI."};
		for (int k = 0; k < 4; ++k)
			st::text(vg, mm2px(math::Vec(30.48f, 50.f + k * 5.f)), lines[k], 6.f, st::legendDim(), st::FONT_LABEL);
	}

	widget::Widget* createEditorView() override {
		return createEditPane(edit);
	}

	bool editorActive() override {
		return edit && edit->active();
	}

	void step() override {
		JXMPModuleWidget::step();
		if (edit && edit->active()) {
			edit->collectRecording();
			// A change since the last remembered step becomes an undo step (a drag: the state before it)
			edit->commit(false);
			edit->syncHostMidi(false);
			edit->routeKeyboard(patchcore::editorShown());
#ifdef JXEDIT_UITEST
			uitest::step(edit);
#endif
		}
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (editorActive()) {
			patchcore::showEditor(true);
			e.consume(this);
		}
	}
};

} // namespace jxedit


Model* modelJXMPEditor = createModel<jxedit::Edit, jxedit::EditWidget>("Editor");

// Only one editor per project
static const bool editorSingleton = [] {
	patchcore::registerSingletonModel(modelJXMPEditor, "jxmp-editor");
	return true;
}();

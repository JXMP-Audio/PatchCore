/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL: 16 sample slots in the spirit of the FastTracker 2 sample editor. Every slot holds one sound (WAV, FLAC, MP3
 * or OGG, chosen in the display's browser, the file dialog or dropped on the module); GATE n plays
 * slot n, V/OCT n its note (without a cable the slot plays at its original pitch). The front edits the slot chosen
 * with CH: start / end, loop off / forward / ping-pong, root note, fine tune, level, pan. Samples are saved inside
 * the patch (the module's patch storage folder, which Cardinal packs into the .vcv and into the DAW project).
 * The four buttons beside CH open a menu in the display: edits of the sample (crop, normalize, reverse, loop
 * crossfade, snap to zero), the view (zoom, seam) and loading / clearing; every edit can be undone.
 */

#include "JXMPWidget.hpp"
#include "dsp/Sampler.hpp"
#include "dsp/SampleFile.hpp"
#include "dsp/SampleEdit.hpp"
#include "dsp/SampleMenu.hpp"
#include "dsp/PitchDetect.hpp"
#include "dsp/SampleRecord.hpp"
#include "dsp/SampleDecode.hpp"
#include "dsp/SampleBrowser.hpp"
#include "dsp/SamplePreview.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace rack {
std::string homeDir();  // CardinalCommon.cpp
}

namespace {

namespace sm = jxmp::sampler;

/** The folder SAMPLE > BROWSE opens: the one used last (by any JX-SMPL, kept across restarts in the user folder),
 * else the home folder */
std::string browserFolderFile() { return asset::user("JXMP-smpl-browser.txt"); }

std::string browserFolder() {
	try {
		const std::string f = browserFolderFile();
		if (system::isFile(f)) {
			const std::vector<uint8_t> b = system::readFile(f);
			const std::string p(b.begin(), b.end());
			if (!p.empty() && system::isDirectory(p))
				return p;
		}
	}
	catch (std::exception&) {
	}
	const std::string home = rack::homeDir();
	return home.empty() ? std::string("/") : home;
}

void setBrowserFolder(const std::string& dir) {
	try {
		system::writeFile(browserFolderFile(), std::vector<uint8_t>(dir.begin(), dir.end()));
	}
	catch (std::exception& e) {
		WARN("JX-SMPL: cannot remember the folder: %s", e.what());
	}
}

constexpr int kSlots = 16;
const NVGcolor kColour = nvgRGB(0xff, 0x5c, 0x8a);

/** Root note shown as FastTracker 2 writes it ("C-4") */
struct NoteQuantity : engine::ParamQuantity {
	std::string getDisplayValueString() override { return sm::noteName((int) std::round(getValue())); }
};

struct Smpl : engine::Module {
	enum ParamIds {
		CH_PARAM,
		LOAD_PARAM,
		PLAY_PARAM,
		// The edit knobs: they always show and change the selected slot
		MODE_PARAM,
		START_PARAM,
		END_PARAM,
		LOOP_PARAM,
		LOOP_START_PARAM,
		LOOP_END_PARAM,
		ROOT_PARAM,
		FINE_PARAM,
		LEVEL_PARAM,
		PAN_PARAM,
		// The menu buttons (LOAD above stays for old patches, it has no button any more)
		MENU_PARAM,
		UP_PARAM,
		DOWN_PARAM,
		OK_PARAM,
		NUM_PARAMS
	};
	enum InputIds { ENUMS(GATE_INPUT, kSlots), ENUMS(VOCT_INPUT, kSlots), REC_L_INPUT, REC_R_INPUT, REC_GATE_INPUT, NUM_INPUTS };
	enum OutputIds { ENUMS(OUT_OUTPUT, kSlots), MIX_L_OUTPUT, MIX_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { ENUMS(SLOT_LIGHT, kSlots), NUM_LIGHTS };

	static constexpr int kEdit = PAN_PARAM - MODE_PARAM + 1;

	/** What the audio thread plays */
	struct AudioSlot {
		std::shared_ptr<const sm::SampleData> live, fading;
		sm::SlotSettings set, fadeSet;
		sm::Voice voice, fadeVoice;
		float ratio = 1.f, fadeRatio = 1.f;
		dsp::SchmittTrigger gate;
		bool high = false;
		// For the display (read on the UI thread)
		std::atomic<double> shownPos{-1.0};
		std::atomic<bool> shownActive{false};
		std::atomic<float> shownVolT{-1.f}, shownPanT{-1.f};  // where the envelopes are (seconds), -1 = silent
	};
	AudioSlot audio[kSlots];

	/** A new sample (or only new settings) waiting for the audio thread */
	struct Pending {
		bool has = false, settingsOnly = false;
		std::shared_ptr<const sm::SampleData> data;
		sm::SlotSettings set;
	};
	std::mutex pendingMutex;
	Pending pending[kSlots];
	std::atomic<bool> anyPending{false};
	/** Counts process() calls: when it stands still the engine is paused (the display then hides the playhead) */
	std::atomic<uint32_t> ticks{0};

	/** UI side of every slot (the UI thread only). `set` is the slot's settings, written under pendingMutex. */
	struct UiSlot {
		std::shared_ptr<const sm::SampleData> data;
		sm::SlotSettings set;
		std::string name, message;
		double messageTime = 0.0;
		std::vector<float> overMin, overMax;
	};
	UiSlot ui[kSlots];
	/** Samples replaced on the UI thread, kept until the audio thread has let go of them (freed in the widget) */
	std::vector<std::shared_ptr<const sm::SampleData>> retired;
	/** Settings read from the patch, applied when the samples are read in onAdd() */
	sm::SlotSettings saved[kSlots];
	bool hasSaved[kSlots] = {};
	bool added = false;

	/** The browser's preview: the highlighted file plays through slot `previewSlot` (its level, pan, OUT / MIX).
	 * Handed over under pendingMutex; the UI keeps the sample alive (its retired list), never the audio thread. */
	struct PendingPreview {
		bool has = false;
		std::shared_ptr<const sm::SampleData> data;
		int slot = 0;
	};
	PendingPreview pendingPreview;
	sm::PreviewPlayer preview;  // audio thread
	int previewSlot = 0;        // audio thread
	std::atomic<double> previewPos{-1.0};

	/** The menu in the display (UI thread) */
	sm::Menu menu;
	// Recording into a slot (SAMPLE > RECORD): the slot it goes to
	sm::Recorder rec;
	int recSlot = 0;
	bool recording() const {
		const int s = rec.state;
		return s == sm::Recorder::ARMED || s == sm::Recorder::RECORDING;
	}

	// Edit knobs ↔ selected slot (UI thread)
	int lastSel = -1;
	std::atomic<bool> refreshEdit{true};  // set from onAdd / undo too
	float lastWritten[kEdit] = {};

	Smpl() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CH_PARAM, 0.f, kSlots - 1, 0.f, "Slot", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configButton(LOAD_PARAM, "Load a sound (WAV, FLAC, MP3, OGG) into the selected slot");
		configButton(MENU_PARAM, "Menu (open, back, close)");
		configButton(UP_PARAM, "Menu up");
		configButton(DOWN_PARAM, "Menu down");
		configButton(OK_PARAM, "Menu OK");
		configButton(PLAY_PARAM, "Play the selected slot (while held)");
		configSwitch(MODE_PARAM, 0.f, 1.f, 1.f, "Mode", {"Gate (plays while the gate is high)", "One-shot (plays to the end)"});
		configParam(START_PARAM, 0.f, 1.f, 0.f, "Start", " %", 0.f, 100.f);
		configParam(END_PARAM, 0.f, 1.f, 1.f, "End", " %", 0.f, 100.f);
		configSwitch(LOOP_PARAM, 0.f, 2.f, 2.f, "Loop (gate mode)", {"Ping-pong", "Forward", "Off"});
		configParam(LOOP_START_PARAM, 0.f, 1.f, 0.f, "Loop start", " %", 0.f, 100.f);
		configParam(LOOP_END_PARAM, 0.f, 1.f, 1.f, "Loop end", " %", 0.f, 100.f);
		configParam<NoteQuantity>(ROOT_PARAM, 12.f, 119.f, 60.f, "Root note (the note the sample sounds at)")->snapEnabled = true;
		configParam(FINE_PARAM, -100.f, 100.f, 0.f, "Fine tune", " cents");
		configParam(LEVEL_PARAM, 0.f, 1.f, 1.f, "Level", " %", 0.f, 100.f);
		configParam(PAN_PARAM, -1.f, 1.f, 0.f, "Pan (in the mix)", " %", 0.f, 100.f);
		for (int i = 0; i < kSlots; ++i) {
			configInput(GATE_INPUT + i, string::f("Gate %d", i + 1))->description =
			    "A rising edge plays the slot from START; in gate mode it plays while the gate is high.";
			configInput(VOCT_INPUT + i, string::f("V/OCT %d", i + 1))->description =
			    "The note to play (0 V = C-4; the slot's root note sounds at its original pitch). Without a cable the slot plays at its original pitch.";
			configOutput(OUT_OUTPUT + i, string::f("Out %d", i + 1))->description =
			    "The slot alone, mono, about ±5 V. Patched, the slot leaves the MIX.";
			configLight(SLOT_LIGHT + i, string::f("Slot %d playing", i + 1));
		}
		configOutput(MIX_L_OUTPUT, "Mix left")->description = "Every slot whose own OUT is not patched, panned.";
		configOutput(MIX_R_OUTPUT, "Mix right")->description = "Every slot whose own OUT is not patched, panned.";
		configInput(REC_L_INPUT, "Record left")->description = "Menu > SAMPLE > RECORD records this into the selected slot; "
		                                                       "±5 V is full scale. Only one side patched: mono.";
		configInput(REC_R_INPUT, "Record right")->description = "With L also patched the recording is stereo.";
		configInput(REC_GATE_INPUT, "Record gate")->description =
		    "After RECORD (armed): records while the gate is high. Without RECORD it does nothing.";
	}

	int selected() { return math::clamp((int) std::round(params[CH_PARAM].getValue()), 0, kSlots - 1); }

	// ---- audio thread ----

	void takePending() {
		std::unique_lock<std::mutex> lock(pendingMutex, std::try_to_lock);
		if (!lock.owns_lock())
			return;
		for (int i = 0; i < kSlots; ++i) {
			Pending& p = pending[i];
			if (!p.has)
				continue;
			AudioSlot& a = audio[i];
			if (p.settingsOnly) {
				a.set = p.set;
				p.has = p.settingsOnly = false;
				continue;
			}
			// The old sound fades out over 2 ms on its own sample, the new one starts silent
			if (a.live && a.voice.active()) {
				a.fading = a.live;
				a.fadeVoice = a.voice;
				a.fadeSet = a.set;
				a.fadeRatio = a.ratio;
				a.fadeVoice.fadeOut();
			}
			a.voice = sm::Voice();
			a.live = p.data;
			a.set = p.set;
			p.has = false;
			p.data.reset();  // the UI still holds it: never freed here
		}
		if (pendingPreview.has) {
			previewSlot = pendingPreview.slot;
			preview.request(pendingPreview.data);
			pendingPreview.has = false;
			pendingPreview.data.reset();  // the UI still holds it
		}
		anyPending.store(false, std::memory_order_release);
	}

	sm::Mode modeOf(float v) const { return v > 0.5f ? sm::ONE_SHOT : sm::GATE; }
	float modeValue(sm::Mode m) const { return m == sm::ONE_SHOT ? 1.f : 0.f; }
	sm::Loop loopOf(float v) const { return (sm::Loop) (2 - math::clamp((int) std::round(v), 0, 2)); }
	float loopValue(sm::Loop l) const { return (float) (2 - (int) l); }

	/** Shows the slot's settings on the edit knobs */
	void writeEdit(const sm::SlotSettings& s, size_t frames) {
		const float n = frames ? (float) frames : 1.f;
		const float values[kEdit] = {modeValue(s.mode), frames ? s.start / n : 0.f, frames ? s.end / n : 1.f, loopValue(s.loop),
		                             frames ? s.loopStart / n : 0.f, frames ? s.loopEnd / n : 1.f, (float) s.root, s.fine, s.level, s.pan};
		for (int k = 0; k < kEdit; ++k) {
			params[MODE_PARAM + k].setValue(values[k]);
			lastWritten[k] = params[MODE_PARAM + k].getValue();
		}
	}

	/** Puts value `v` of edit knob `param` into `s`; returns the point it moved (sm::Point) or -1 */
	int applyEdit(sm::SlotSettings& s, int param, float v, size_t frames) const {
		auto frame = [&](float t) { return (size_t) std::lround(math::clamp(t, 0.f, 1.f) * frames); };
		switch (param) {
		case MODE_PARAM: s.mode = modeOf(v); break;
		case START_PARAM: s.start = frame(v); return sm::P_START;
		case END_PARAM: s.end = frame(v); return sm::P_END;
		case LOOP_PARAM: s.loop = loopOf(v); break;
		case LOOP_START_PARAM: s.loopStart = frame(v); return sm::P_LOOP_START;
		case LOOP_END_PARAM: s.loopEnd = frame(v); return sm::P_LOOP_END;
		case ROOT_PARAM: s.root = (int) std::round(v); break;
		case FINE_PARAM: s.fine = v; break;
		case LEVEL_PARAM: s.level = v; break;
		case PAN_PARAM: s.pan = v; break;
		}
		return -1;
	}

	size_t framesOf(int i) const { return ui[i].data ? ui[i].data->frames() : 0; }

	/** Slot i's settings with edit knob `param` at `v` (for undo) */
	sm::SlotSettings settingsWith(int i, int param, float v) {
		sm::SlotSettings c = currentSettings(i);
		const int moved = applyEdit(c, param, v, framesOf(i));
		if (framesOf(i))
			sm::constrain(c, framesOf(i), moved < 0 ? sm::P_START : moved);
		return c;
	}

	/** UI thread, every frame: turned edit knobs go into the selected slot, the slot's values onto the knobs when CH
	 * changes. Here and not in process(): the UI owns the settings (no race with the display), and knobs work while
	 * the engine is paused. */
	void syncEdit() {
		const int sel = selected();
		const size_t frames = framesOf(sel);
		sm::SlotSettings cur = currentSettings(sel);
		if (sel != lastSel || refreshEdit) {
			writeEdit(cur, frames);
			lastSel = sel;
			refreshEdit = false;
			return;
		}
		bool changed = false;
		int moved = -1;
		for (int k = 0; k < kEdit; ++k) {
			const float v = params[MODE_PARAM + k].getValue();
			if (v == lastWritten[k])
				continue;
			changed = true;
			const int m = applyEdit(cur, MODE_PARAM + k, v, frames);
			if (m >= 0)
				moved = m;
		}
		if (!changed)
			return;
		const bool loopMoved = moved == sm::P_LOOP_START || moved == sm::P_LOOP_END;
		if (loopMoved && !(cur.mode == sm::GATE && cur.loop != sm::LOOP_OFF))
			message(sel, "LOOP: GATE + FWD/PING");
		if (frames) {
			const sm::SlotSettings asked = cur;
			sm::constrain(cur, frames, moved < 0 ? sm::P_START : moved);
			// Only points that constrain() moved go back onto their knobs: rewriting every knob with its value rounded
			// to a frame would undo small drags on short samples
			const float n = (float) frames;
			const size_t was[4] = {asked.start, asked.end, asked.loopStart, asked.loopEnd};
			const size_t now[4] = {cur.start, cur.end, cur.loopStart, cur.loopEnd};
			const int ids[4] = {START_PARAM, END_PARAM, LOOP_START_PARAM, LOOP_END_PARAM};
			for (int k = 0; k < 4; ++k)
				if (was[k] != now[k])
					params[ids[k]].setValue(now[k] / n);
			// A loop point pushed against START / END: it cannot go further that way
			if (loopMoved && was[moved] != now[moved])
				message(sel, "LOOP STAYS BETWEEN S AND E");
		}
		for (int k = 0; k < kEdit; ++k)
			lastWritten[k] = params[MODE_PARAM + k].getValue();
		postSettings(sel, cur);
	}

	void process(const ProcessArgs& args) override {
		rec.process(inputs[REC_L_INPUT].getVoltage(), inputs[REC_R_INPUT].getVoltage(), inputs[REC_L_INPUT].isConnected(),
		            inputs[REC_R_INPUT].isConnected(), inputs[REC_GATE_INPUT].isConnected(), inputs[REC_GATE_INPUT].getVoltage());
		ticks.fetch_add(1, std::memory_order_relaxed);
		if (anyPending.load(std::memory_order_acquire))
			takePending();
		const int sel = selected();
		const bool play = params[PLAY_PARAM].getValue() > 0.5f;
		sm::MixBus mix;
		for (int i = 0; i < kSlots; ++i) {
			AudioSlot& a = audio[i];
			a.gate.process(inputs[GATE_INPUT + i].getVoltage(), 0.1f, 1.f);
			const bool high = a.gate.isHigh() || (play && i == sel);
			if (high && !a.high)
				a.voice.trigger();
			else if (!high && a.high)
				a.voice.release();
			a.high = high;

			const bool ownOut = outputs[OUT_OUTPUT + i].isConnected();
			float out = 0.f;
			// The old sound of a replaced sample fades out at its own level and pan
			if (a.fading) {
				float fl, fr;
				a.fadeVoice.process(*a.fading, a.fadeSet, a.fadeRatio, args.sampleTime, fl, fr);
				const bool st = a.fading->stereo;
				out += (st ? 0.5f * (fl + fr) : fl) * 5.f * a.fadeSet.level;
				mix.add(fl * 5.f, fr * 5.f, st, a.fadeSet.level, sm::envPan(a.fadeSet.pan, a.fadeVoice.panValue), ownOut);
				if (!a.fadeVoice.active())
					a.fading.reset();  // the UI's retired list still holds it: never freed here
			}
			if (a.live) {
				a.ratio = sm::pitchRatio(a.live->rate, args.sampleRate, inputs[VOCT_INPUT + i].isConnected(),
				                         inputs[VOCT_INPUT + i].getVoltage(), a.set.root, a.set.fine);
				float vl, vr;
				a.voice.process(*a.live, a.set, a.ratio, args.sampleTime, vl, vr);
				const bool st = a.live->stereo;
				out += (st ? 0.5f * (vl + vr) : vl) * 5.f * a.set.level;
				mix.add(vl * 5.f, vr * 5.f, st, a.set.level, sm::envPan(a.set.pan, a.voice.panValue), ownOut);
				a.shownPos.store(a.voice.position(), std::memory_order_relaxed);
				a.shownVolT.store(a.voice.volTime(), std::memory_order_relaxed);
				a.shownPanT.store(a.voice.panTime(), std::memory_order_relaxed);
			}
			else {
				a.shownPos.store(-1.0, std::memory_order_relaxed);
				a.shownVolT.store(-1.f, std::memory_order_relaxed);
				a.shownPanT.store(-1.f, std::memory_order_relaxed);
			}
			const bool active = a.voice.active() || (a.fading && a.fadeVoice.active());
			a.shownActive.store(active, std::memory_order_relaxed);
			// The browser's preview through this slot
			bool previewing = false;
			if (i == previewSlot) {
				float pl, pr;
				preview.process(args.sampleRate, pl, pr);
				if (preview.live) {
					const bool st = preview.stereo();
					out += (st ? 0.5f * (pl + pr) : pl) * 5.f * a.set.level;
					mix.add(pl * 5.f, pr * 5.f, st, a.set.level, a.set.pan, ownOut);
					previewing = preview.playing();
				}
				previewPos.store(preview.position(), std::memory_order_relaxed);
			}
			outputs[OUT_OUTPUT + i].setVoltage(out);
			lights[SLOT_LIGHT + i].setBrightness(active || previewing ? 1.f : 0.f);
		}
		outputs[MIX_L_OUTPUT].setVoltage(sm::softLimit10(mix.L));
		outputs[MIX_R_OUTPUT].setVoltage(sm::softLimit10(mix.R));
	}

	// ---- UI thread ----

	/** A new sample (or none) with its settings for slot i */
	void post(int i, std::shared_ptr<const sm::SampleData> data, const sm::SlotSettings& set) {
		std::lock_guard<std::mutex> lock(pendingMutex);
		ui[i].set = set;
		pending[i].has = true;
		pending[i].settingsOnly = false;
		pending[i].data = data;
		pending[i].set = set;
		anyPending.store(true, std::memory_order_release);
	}

	/** New settings for slot i, same sample: the sound goes on with them */
	void postSettings(int i, const sm::SlotSettings& set) {
		std::lock_guard<std::mutex> lock(pendingMutex);
		ui[i].set = set;
		if (!pending[i].has)
			pending[i].settingsOnly = true;  // else a new sample is waiting: it takes these settings along
		pending[i].has = true;
		pending[i].set = set;
		anyPending.store(true, std::memory_order_release);
	}

	/** The browser's preview: `data` through slot i (none = stop). The caller keeps `data` (and retires it). */
	void postPreview(int i, std::shared_ptr<const sm::SampleData> data) {
		std::lock_guard<std::mutex> lock(pendingMutex);
		pendingPreview.has = true;
		pendingPreview.data = data;
		pendingPreview.slot = i;
		anyPending.store(true, std::memory_order_release);
	}

	/** Frees replaced samples the audio thread no longer holds (UI thread) */
	void pruneRetired() {
		retired.erase(std::remove_if(retired.begin(), retired.end(),
		                             [](const std::shared_ptr<const sm::SampleData>& p) { return p.use_count() == 1; }),
		              retired.end());
	}

	void message(int i, const std::string& text) {
		ui[i].message = text;
		ui[i].messageTime = system::getTime();
	}

	std::string storagePath(int i) {
		return system::join(createPatchStorageDirectory(), string::f("slot%02d.wav", i + 1));
	}

	/** Min / max of the sample in kOverParts parts for the display (a zoomed view reads the frames themselves) */
	static constexpr size_t kOverParts = 4096;
	static void overview(const sm::SampleData& d, std::vector<float>& lo, std::vector<float>& hi) {
		const size_t parts = kOverParts, n = d.frames();
		lo.assign(parts, 0.f);
		hi.assign(parts, 0.f);
		for (size_t p = 0; p < parts && n; ++p) {
			const size_t a = p * n / parts, b = std::max(a + 1, (p + 1) * n / parts);
			float mn = 1e9f, mx = -1e9f;
			for (size_t k = a; k < b && k < n; ++k) {
				const float v = d.stereo ? 0.5f * (d.l[k] + d.r[k]) : d.l[k];
				mn = std::min(mn, v);
				mx = std::max(mx, v);
			}
			lo[p] = mn;
			hi[p] = mx;
		}
	}

	/** Installs decoded bytes into slot i. `set` = settings to apply (from the patch), or defaults when null.
	 * `wasCut` (if given) says whether the file was longer than 60 s and was shortened. */
	bool install(int i, const std::vector<uint8_t>& bytes, const std::string& name, const sm::SlotSettings* set,
	             bool* wasCut = nullptr) {
		pruneRetired();
		auto data = std::make_shared<sm::SampleData>();
		std::string error;
		bool cut = false;
		if (!sm::decodeAudio(bytes.data(), bytes.size(), *data, error, cut)) {
			message(i, error);
			return false;
		}
		const bool had = (bool) ui[i].data;
		if (ui[i].data)
			retired.push_back(ui[i].data);
		ui[i].data = data;
		ui[i].name = name;
		overview(*data, ui[i].overMin, ui[i].overMax);
		sm::SlotSettings s = sm::defaults(data->frames());
		if (set) {
			s = *set;
			sm::constrain(s, data->frames(), sm::P_START);
		}
		else if (had) {
			// A new file in a slot keeps the slot's envelopes and fadeout (as an FT2 instrument does)
			const sm::SlotSettings old = currentSettings(i);
			s.volEnv = old.volEnv;
			s.panEnv = old.panEnv;
			s.fadeout = old.fadeout;
		}
		post(i, data, s);
		refreshEdit = true;
		message(i, cut ? "CUT TO 60 S" : "");
		if (wasCut)
			*wasCut = cut;
		return true;
	}

	/** Loads a file from the computer into slot i and keeps a copy in the patch (the file's own bytes: the format is
	 * told by its content when the patch is opened). False when nothing changed. */
	bool loadFile(int i, const std::string& path) {
		std::vector<uint8_t> bytes;
		if (!system::isFile(path)) {
			message(i, "NOT A FILE");
			return false;
		}
		try {
			bytes = system::readFile(path);
		}
		catch (std::exception& e) {
			message(i, "CANNOT READ FILE");
			return false;
		}
		std::string name = string::uppercase(system::getStem(path));
		if (name.size() > 24)
			name = name.substr(0, 24);
		bool cut = false;
		if (!install(i, bytes, name, nullptr, &cut))
			return false;
		try {
			// A file cut to 60 s: the patch keeps only what plays
			if (cut)
				system::writeFile(storagePath(i), sm::encodeWav(*ui[i].data));
			else
				system::writeFile(storagePath(i), bytes);
		}
		catch (std::exception& e) {
			WARN("JX-SMPL: cannot save slot %d into the patch: %s", i + 1, e.what());
		}
		return true;
	}

	void clearSlot(int i) {
		pruneRetired();
		if (ui[i].data)
			retired.push_back(ui[i].data);
		ui[i] = UiSlot();
		post(i, nullptr, sm::SlotSettings());
		refreshEdit = true;
		// The file stays until the patch is saved, so Undo of Initialize can bring the sample back
	}

	/** Puts an edited sample (or none) into slot i with its settings and name, and keeps it in the patch: the edits
	 * and their undo. The sample goes into the patch storage as a 32-bit float WAV. */
	void replaceSample(int i, std::shared_ptr<const sm::SampleData> data, const sm::SlotSettings& set, const std::string& name) {
		if (!data) {
			clearSlot(i);
			ui[i].name = name;
			return;
		}
		pruneRetired();
		if (ui[i].data)
			retired.push_back(ui[i].data);
		ui[i].data = data;
		ui[i].name = name;
		overview(*data, ui[i].overMin, ui[i].overMax);
		post(i, data, set);
		refreshEdit = true;
		try {
			system::writeFile(storagePath(i), sm::encodeWav(*data));
		}
		catch (std::exception& e) {
			WARN("JX-SMPL: cannot save slot %d into the patch: %s", i + 1, e.what());
		}
	}

	std::string slotFile(int i) { return system::join(getPatchStorageDirectory(), string::f("slot%02d.wav", i + 1)); }

	/** Reads slot i from the patch storage with `set` (or defaults); false when there is no file */
	bool installFromStorage(int i, const sm::SlotSettings* set) {
		const std::string path = slotFile(i);
		if (!system::isFile(path))
			return false;
		try {
			const std::string name = ui[i].name.empty() ? string::f("SLOT %d", i + 1) : ui[i].name;
			install(i, system::readFile(path), name, set);
		}
		catch (std::exception& ex) {
			message(i, "CANNOT READ FILE");
		}
		return true;
	}

	void onSave(const SaveEvent& e) override {
		Module::onSave(e);
		for (int i = 0; i < kSlots; ++i)
			if (!ui[i].data) {
				try {
					system::remove(slotFile(i));
				}
				catch (std::exception&) {
				}
			}
	}

	void onAdd(const AddEvent& e) override {
		Module::onAdd(e);
		added = true;
		for (int i = 0; i < kSlots; ++i)
			if (!installFromStorage(i, hasSaved[i] ? &saved[i] : nullptr) && !ui[i].name.empty())
				message(i, "SAMPLE MISSING");
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < kSlots; ++i)
			clearSlot(i);
		refreshEdit = true;
	}

	/** Slot i's settings (the audio thread gets them a moment later) */
	sm::SlotSettings currentSettings(int i) {
		std::lock_guard<std::mutex> lock(pendingMutex);
		return ui[i].set;
	}

	static json_t* envToJson(const sm::Envelope& e) {
		json_t* o = json_object();
		json_object_set_new(o, "on", json_boolean(e.on));
		json_t* pts = json_array();
		for (int k = 0; k < e.n; ++k) {
			json_t* p = json_array();
			json_array_append_new(p, json_real(e.t[k]));
			json_array_append_new(p, json_real(e.v[k]));
			json_array_append_new(pts, p);
		}
		json_object_set_new(o, "points", pts);
		json_object_set_new(o, "sustain", json_integer(e.sus));
		json_object_set_new(o, "loopStart", json_integer(e.ls));
		json_object_set_new(o, "loopEnd", json_integer(e.le));
		return o;
	}
	static sm::Envelope envFromJson(json_t* o, const sm::Envelope& def) {
		if (!json_is_object(o))
			return def;
		sm::Envelope e;
		e.on = json_is_true(json_object_get(o, "on"));
		json_t* pts = json_object_get(o, "points");
		e.n = 0;
		for (size_t k = 0; k < json_array_size(pts) && e.n < sm::Envelope::kMax; ++k) {
			json_t* p = json_array_get(pts, k);
			e.t[e.n] = (float) json_number_value(json_array_get(p, 0));
			e.v[e.n] = (float) json_number_value(json_array_get(p, 1));
			++e.n;
		}
		if (e.n < 2)
			return def;
		auto index = [&](const char* key) {
			json_t* v = json_object_get(o, key);
			return v ? (int) json_integer_value(v) : -1;
		};
		e.sus = index("sustain");
		e.ls = index("loopStart");
		e.le = index("loopEnd");
		sm::envSanitize(e);
		return e;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* slots = json_array();
		for (int i = 0; i < kSlots; ++i) {
			json_t* s = json_object();
			const sm::SlotSettings set = currentSettings(i);
			json_object_set_new(s, "name", json_string(ui[i].name.c_str()));
			if (ui[i].data) {
				json_object_set_new(s, "start", json_integer((json_int_t) set.start));
				json_object_set_new(s, "end", json_integer((json_int_t) set.end));
				json_object_set_new(s, "loopStart", json_integer((json_int_t) set.loopStart));
				json_object_set_new(s, "loopEnd", json_integer((json_int_t) set.loopEnd));
				json_object_set_new(s, "root", json_integer(set.root));
				json_object_set_new(s, "fine", json_real(set.fine));
				json_object_set_new(s, "level", json_real(set.level));
				json_object_set_new(s, "pan", json_real(set.pan));
				json_object_set_new(s, "mode", json_integer(set.mode));
				json_object_set_new(s, "loop", json_integer(set.loop));
				json_object_set_new(s, "volEnv", envToJson(set.volEnv));
				json_object_set_new(s, "panEnv", envToJson(set.panEnv));
				json_object_set_new(s, "fadeout", json_real(set.fadeout));
			}
			json_array_append_new(slots, s);
		}
		json_object_set_new(root, "slots", slots);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* slots = json_object_get(root, "slots");
		for (int i = 0; i < kSlots; ++i) {
			json_t* s = json_array_get(slots, i);
			hasSaved[i] = false;
			if (!s)
				continue;
			const char* const name = json_string_value(json_object_get(s, "name"));
			if (!added || !ui[i].data)
				ui[i].name = name ? name : "";
			if (!json_object_get(s, "end"))
				continue;
			auto integer = [&](const char* key, json_int_t def) {
				json_t* v = json_object_get(s, key);
				return v ? json_integer_value(v) : def;
			};
			auto real = [&](const char* key, double def) {
				json_t* v = json_object_get(s, key);
				return v ? json_number_value(v) : def;
			};
			sm::SlotSettings& t = saved[i];
			t.start = (size_t) std::max<json_int_t>(0, integer("start", 0));
			t.end = (size_t) std::max<json_int_t>(0, integer("end", 0));
			t.loopStart = (size_t) std::max<json_int_t>(0, integer("loopStart", 0));
			t.loopEnd = (size_t) std::max<json_int_t>(0, integer("loopEnd", 0));
			t.root = math::clamp((int) integer("root", 60), 12, 119);
			t.fine = math::clamp((float) real("fine", 0.0), -100.f, 100.f);
			t.level = math::clamp((float) real("level", 1.0), 0.f, 1.f);
			t.pan = math::clamp((float) real("pan", 0.0), -1.f, 1.f);
			t.mode = integer("mode", 0) ? sm::GATE : sm::ONE_SHOT;
			t.loop = (sm::Loop) math::clamp((int) integer("loop", 0), 0, 2);
			// Envelopes (patches from before them: the defaults, off)
			t.volEnv = envFromJson(json_object_get(s, "volEnv"), sm::defaultVolEnv());
			t.panEnv = envFromJson(json_object_get(s, "panEnv"), sm::defaultPanEnv());
			t.fadeout = math::clamp((float) real("fadeout", 0.0), 0.f, 10.f);
			hasSaved[i] = true;
			// A preset or undo on a module that is already running: apply the settings at once; a slot emptied by
			// Initialize or Clear (file still in the storage until a save) gets its sample back
			if (added && ui[i].data) {
				sm::SlotSettings c = t;
				sm::constrain(c, ui[i].data->frames(), sm::P_START);
				postSettings(i, c);
			}
			else if (added) {
				installFromStorage(i, &t);
			}
		}
		refreshEdit = true;
	}
};

/** Undo / redo of a change to one slot's settings: selects that slot and puts its settings back */
struct SlotChange : history::ModuleAction {
	int slot = 0;
	sm::SlotSettings before, after;
	void apply(const sm::SlotSettings& s) {
		Smpl* const m = dynamic_cast<Smpl*>(APP->engine->getModule(moduleId));
		if (!m)
			return;
		m->params[Smpl::CH_PARAM].setValue((float) slot);
		m->postSettings(slot, s);
		m->refreshEdit = true;
	}
	void undo() override { apply(before); }
	void redo() override { apply(after); }
};

inline double dclamp(double v, double lo, double hi) { return std::min(std::max(v, lo), hi); }

/** Applies new settings to slot i with an undo step (envelope edits, snap, detect root) */
inline void changeSlot(Smpl* m, int i, const sm::SlotSettings& before, const sm::SlotSettings& after, const char* name) {
	SlotChange* const h = new SlotChange;
	h->name = name;
	h->moduleId = m->id;
	h->slot = i;
	h->before = before;
	h->after = after;
	APP->history->push(h);
	m->postSettings(i, after);
	m->refreshEdit = true;
}

inline std::string panName(float v) {
	return v > 0.52f ? string::f("L%d", (int) std::round((v - 0.5f) * 200.f))
	                 : v < 0.48f ? string::f("R%d", (int) std::round((0.5f - v) * 200.f)) : std::string("C");
}

/** Undo / redo of an edit that changed a slot's sample (crop, normalize, reverse, crossfade, clear): puts the sample,
 * its settings and name back, and the file in the patch with them */
struct SampleSwap : history::ModuleAction {
	struct State {
		std::shared_ptr<const sm::SampleData> data;
		sm::SlotSettings set;
		std::string name;
	};
	int slot = 0;
	State before, after;
	void apply(const State& s) {
		Smpl* const m = dynamic_cast<Smpl*>(APP->engine->getModule(moduleId));
		if (!m)
			return;
		m->params[Smpl::CH_PARAM].setValue((float) slot);
		m->replaceSample(slot, s.data, s.set, s.name);
	}
	void undo() override { apply(before); }
	void redo() override { apply(after); }
};

/** Loads a file into slot i with an undo step (browser, dialog, drop); its folder is where BROWSE opens next */
inline bool loadWithUndo(Smpl* m, int i, const std::string& path) {
	const SampleSwap::State before = {m->ui[i].data, m->currentSettings(i), m->ui[i].name};
	if (!m->loadFile(i, path))
		return false;
	SampleSwap* const h = new SampleSwap;
	h->name = "JX-SMPL load";
	h->moduleId = m->id;
	h->slot = i;
	h->before = before;
	h->after = {m->ui[i].data, m->currentSettings(i), m->ui[i].name};
	APP->history->push(h);
	setBrowserFolder(system::getDirectory(path));
	return true;
}

/** The VFD of JX-SMPL: the selected slot's name, root note, mode and loop; its waveform with START / END handles below
 * and the loop flags above (drag them); a strip of the 16 slots (click one to select it). The wheel zooms around the
 * pointer, dragging the wave pans, a double click shows the whole sample. The menu (MENU button) fills the window. */
struct SmplDisplay : widget::OpaqueWidget {
	Smpl* module = nullptr;
	enum Handle { H_NONE = -1, H_START, H_END, H_LOOP_START, H_LOOP_END, H_PAN, H_ENV };
	int dragging = H_NONE;
	uint32_t lastTicks = 0;
	double lastTickTime = 0.0;
	float dragX = 0.f, dragStartX = 0.f, oldValue = 0.f;
	// The view: frames view0..view1 of the selected slot's sample; the whole sample again when the slot or the
	// sample's length changes
	double view0 = 0.0, view1 = 1.0, panView0 = 0.0;
	bool seam = false;
	// The envelope view: 0 = the wave, 1 = volume, 2 = pan; the selected point; the time span shown (fixed while a
	// point is dragged)
	int envView = 0, selPt = -1;
	float envSpan = 1.f, dragY = 0.f;
	sm::SlotSettings envBefore;
	math::Vec lastPress;
	// Recording: the loudest value of every 1/500 s recorded so far, the frames read into it, the meters
	std::vector<float> recOver;
	size_t recSeen = 0;
	float meter[2] = {}, hold[2] = {};
	bool clip = false;
	// The browser (SAMPLE > BROWSE): one folder's list; the highlighted file, once the selection has rested 0.15 s,
	// is decoded for its picture and plays through the selected slot
	enum { kBrRows = 4 };
	bool browsing = false;
	sm::Browser br;
	double brChanged = 0.0;
	std::string brTried, brInfo;
	std::shared_ptr<const sm::SampleData> brData;
	std::vector<float> brMin, brMax;

	void brList(const std::string& path, const std::string& select = std::string()) {
		std::vector<sm::BrowserEntry> raw;
		try {
			for (const std::string& e : system::getEntries(path))
				raw.push_back(sm::BrowserEntry(system::getFilename(e), system::isDirectory(e)));
		}
		catch (std::exception&) {
		}
		br.open(path, raw, select);
		br.follow(kBrRows);
		brMoved();
	}
	void openBrowser() {
		browsing = true;
		brList(browserFolder());
	}
	void closeBrowser() {
		browsing = false;
		brStop();
		setBrowserFolder(br.path);
	}
	/** The selection moved: the preview stops, the new file is read after a rest */
	void brMoved() {
		brChanged = system::getTime();
		brStop();
	}
	void brStop() {
		if (!module)
			return;
		module->postPreview(module->selected(), nullptr);
		if (brData)
			module->retired.push_back(brData);  // the audio thread may still hold it
		brData.reset();
		brTried.clear();
		brInfo.clear();
		brMin.clear();
		brMax.clear();
	}
	/** The highlighted file, decoded once the selection has rested, and its preview */
	void brStep() {
		const sm::BrowserEntry* e = br.selected();
		if (!browsing || !module || !e || e->dir || system::getTime() - brChanged < 0.15)
			return;
		const std::string path = sm::joinPath(br.path, e->name);
		if (path == brTried)
			return;
		brTried = path;
		std::vector<uint8_t> bytes;
		try {
			if (system::getFileSize(path) > (uint64_t) 200 << 20) {
				brInfo = "TOO BIG";
				return;
			}
			bytes = system::readFile(path);
		}
		catch (std::exception&) {
			brInfo = "CANNOT READ FILE";
			return;
		}
		auto d = std::make_shared<sm::SampleData>();
		std::string error;
		bool cut = false;
		if (!sm::decodeAudio(bytes.data(), bytes.size(), *d, error, cut)) {
			brInfo = error;
			return;
		}
		static const char* const kinds[] = {"", "WAV", "FLAC", "MP3", "OGG"};
		brInfo = std::string(kinds[sm::audioKind(bytes.data(), bytes.size())]) + (d->stereo ? "  ST" : "  MONO");
		Smpl::overview(*d, brMin, brMax);
		brData = d;
		module->postPreview(module->selected(), d);
	}
	/** OK: into a folder, one folder up, or the file into the selected slot */
	void brOk() {
		const sm::BrowserEntry* e = br.selected();
		if (!e || !module)
			return;
		if (e->name == "..")
			brList(sm::parentDir(br.path), sm::lastPart(br.path));
		else if (e->dir)
			brList(sm::joinPath(br.path, e->name));
		else {
			const std::string path = sm::joinPath(br.path, e->name);
			const int i = module->selected();
			closeBrowser();
			if (loadWithUndo(module, i, path) && module->ui[i].message.empty())
				module->message(i, "LOADED " + module->ui[i].name);
		}
	}
	void brMove(int d) {
		br.move(d, kBrRows);
		brMoved();
	}

	void step() override {
		brStep();
		OpaqueWidget::step();
	}

	void recReset() {
		recOver.clear();
		recSeen = 0;
		meter[0] = meter[1] = hold[0] = hold[1] = 0.f;
		clip = false;
	}
	int viewSel = -1;
	size_t viewFrames = 0;
	int menuTop = 0;

	const sm::SampleData* data() const {
		return module ? module->ui[module->selected()].data.get() : nullptr;
	}
	size_t frames() const {
		const sm::SampleData* d = data();
		return d ? d->frames() : 0;
	}

	void syncView() {
		if (!module)
			return;
		const int sel = module->selected();
		if (sel != viewSel || frames() != viewFrames) {
			if (sel != viewSel)
				selPt = -1;
			viewSel = sel;
			viewFrames = frames();
			whole();
		}
	}
	void whole() {
		view0 = 0.0;
		view1 = std::max<double>(1.0, (double) frames());
		seam = false;
	}
	void clampView() {
		const double n = std::max<double>(1.0, (double) frames());
		const double w = dclamp(view1 - view0, std::min(16.0, n), n);
		const double a = dclamp(view0, 0.0, n - w);
		view0 = a;
		view1 = a + w;
	}
	void zoomToLoop(const sm::SlotSettings& s) {
		const double m = std::max(8.0, (s.loopEnd - (double) s.loopStart) * 0.08);
		view0 = s.loopStart - m;
		view1 = s.loopEnd + m;
		clampView();
		seam = false;
	}
	double zoom() const { return frames() ? frames() / (view1 - view0) : 1.0; }

	const sm::Envelope& envOf(const sm::SlotSettings& s) const { return envView == 2 ? s.panEnv : s.volEnv; }
	sm::Envelope& envOf(sm::SlotSettings& s) const { return envView == 2 ? s.panEnv : s.volEnv; }
	static float spanOf(const sm::Envelope& e) { return std::max(1.f, e.t[e.n - 1] * 1.15f); }
	float xT(float t) const {
		const math::Rect wr = waveRect();
		return wr.pos.x + t / envSpan * wr.size.x;
	}
	float tX(float x) const {
		const math::Rect wr = waveRect();
		return (x - wr.pos.x) / wr.size.x * envSpan;
	}
	float yV(float v) const {
		const math::Rect wr = waveRect();
		return wr.pos.y + wr.size.y * (1.f - v);
	}
	float vY(float y) const {
		const math::Rect wr = waveRect();
		return math::clamp(1.f - (y - wr.pos.y) / wr.size.y, 0.f, 1.f);
	}
	int envPointAt(const sm::Envelope& e, math::Vec p) const {
		int best = -1;
		float bd = mm2px(2.2f);
		for (int k = 0; k < e.n; ++k) {
			const float d = p.minus(math::Vec(xT(e.t[k]), yV(e.v[k]))).norm();
			if (d < bd) {
				bd = d;
				best = k;
			}
		}
		return best;
	}
	void showEnv(int which) {
		envView = which;
		selPt = -1;
		seam = false;
	}

	math::Rect waveRect() const {
		return math::Rect(mm2px(math::Vec(3.f, 10.5f)), math::Vec(box.size.x - mm2px(6.f), box.size.y - mm2px(19.5f)));
	}
	math::Rect stripBox(int i) const {
		const float x0 = mm2px(4.f), w = (box.size.x - 2 * x0) / kSlots;
		return math::Rect(math::Vec(x0 + i * w + 1.f, box.size.y - mm2px(5.2f)), math::Vec(w - 2.f, mm2px(2.4f)));
	}
	float xOf(double f) const {
		const math::Rect wr = waveRect();
		return wr.pos.x + (float) ((f - view0) / (view1 - view0)) * wr.size.x;
	}
	double fOf(float x) const {
		const math::Rect wr = waveRect();
		return view0 + (x - wr.pos.x) / wr.size.x * (view1 - view0);
	}

	int paramOf(int h) const {
		static const int ids[4] = {Smpl::START_PARAM, Smpl::END_PARAM, Smpl::LOOP_START_PARAM, Smpl::LOOP_END_PARAM};
		return ids[h];
	}

	/** The wave of frames f0..f1 into x0..x0+w as one path (min / max per pixel column, or a line through the frames
	 * when zoomed in that far); `dots` marks single frames when they are far apart */
	void wavePath(NVGcontext* vg, const Smpl::UiSlot& u, double f0, double f1, float x0, float w, bool& dots) const {
		const sm::SampleData& d = *u.data;
		const math::Rect wr = waveRect();
		const float mid = wr.pos.y + wr.size.y / 2, amp = wr.size.y * 0.48f;
		const long n = (long) d.frames();
		const double span = f1 - f0;
		const int cols = std::max(1, (int) w);
		const double perCol = span / cols;
		dots = false;
		nvgBeginPath(vg);
		if (perCol > 1.5) {
			const double partFrames = (double) n / (double) u.overMin.size();
			for (int c = 0; c < cols; ++c) {
				const double a = f0 + c * perCol, b = a + perCol;
				float lo = 1e9f, hi = -1e9f;
				if (perCol >= 2.0 * partFrames && !u.overMin.empty()) {
					// Far out: the overview
					const long pa = std::max(0L, (long) (a / partFrames)), pb = std::min((long) u.overMin.size(), (long) std::ceil(b / partFrames));
					for (long p = pa; p < pb; ++p) {
						lo = std::min(lo, u.overMin[(size_t) p]);
						hi = std::max(hi, u.overMax[(size_t) p]);
					}
				}
				else {
					const long ka = std::max(0L, (long) a), kb = std::min(n, (long) std::ceil(b));
					for (long k = ka; k < kb; ++k) {
						const float v = sm::monoAt(d, (size_t) k);
						lo = std::min(lo, v);
						hi = std::max(hi, v);
					}
				}
				if (hi < lo)
					continue;
				const float x = x0 + c + 0.5f, y0 = mid - hi * amp, y1 = mid - lo * amp;
				nvgMoveTo(vg, x, y0);
				nvgLineTo(vg, x, std::max(y1, y0 + 0.6f));
			}
		}
		else {
			bool first = true;
			for (long k = (long) std::floor(f0) - 1; k <= (long) std::ceil(f1) + 1; ++k) {
				if (k < 0 || k >= n)
					continue;
				const float x = x0 + (float) ((k - f0) / span) * w, y = mid - sm::monoAt(d, (size_t) k) * amp;
				if (first)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
				first = false;
			}
			dots = w / span > 6.0;
		}
	}

	void frameDots(NVGcontext* vg, const Smpl::UiSlot& u, double f0, double f1, float x0, float w, NVGcolor c) const {
		const math::Rect wr = waveRect();
		const float mid = wr.pos.y + wr.size.y / 2, amp = wr.size.y * 0.48f;
		const long n = (long) u.data->frames();
		nvgBeginPath(vg);
		for (long k = (long) std::ceil(f0); k <= (long) std::floor(f1); ++k) {
			if (k < 0 || k >= n)
				continue;
			nvgCircle(vg, x0 + (float) ((k - f0) / (f1 - f0)) * w, mid - sm::monoAt(*u.data, (size_t) k) * amp, 1.3f);
		}
		nvgFillColor(vg, c);
		nvgFill(vg);
	}

	void draw(const DrawArgs& args) override {
		namespace st = jxmp::style;
		NVGcontext* const vg = args.vg;
		const NVGcolor lit = st::vfdTint(kColour);
		st::vfdWindow(vg, box.size, lit);
		if (module && module->menu.open)
			drawMenu(vg, lit);
		else if (module && browsing)
			drawBrowser(vg, lit);
		else if (module && module->recording())
			drawRec(vg, lit);
		else
			drawSlot(vg, lit);
	}

	/** Recording: ARMED / REC with the time, the wave as it grows, meters L / R (−48..0 dB, peak hold, CLIP) */
	void drawRec(NVGcontext* vg, NVGcolor lit) {
		namespace st = jxmp::style;
		auto dim = [&](float a) { return nvgRGBAf(lit.r, lit.g, lit.b, a); };
		sm::Recorder& r = module->rec;
		const bool stereo = module->inputs[Smpl::REC_L_INPUT].isConnected() && module->inputs[Smpl::REC_R_INPUT].isConnected();
		const bool gate = module->inputs[Smpl::REC_GATE_INPUT].isConnected();
		const bool armed = r.state == sm::Recorder::ARMED;
		const size_t n = r.frames.load(std::memory_order_acquire);
		const float secs = n / r.rate;
		// The wave so far
		const size_t per = std::max<size_t>(1, (size_t) (r.rate / 500.f));
		while (recSeen + per <= n) {
			float m = 0.f;
			for (size_t k = recSeen; k < recSeen + per; ++k)
				m = std::max(m, std::max(std::fabs(r.l[k]), std::fabs(r.r[k])));
			recOver.push_back(m);
			recSeen += per;
		}
		// Meters, falling back slowly
		const float pk[2] = {r.takePeakL(), r.takePeakR()};
		for (int c = 0; c < 2; ++c) {
			meter[c] = std::max(pk[c], meter[c] * 0.85f);
			hold[c] = std::max(pk[c], hold[c] - 0.01f);
			if (pk[c] > 1.f)
				clip = true;
		}
		const float line1 = mm2px(4.f), line2 = mm2px(8.f), left = mm2px(3.f), right = box.size.x - mm2px(3.f);
		st::vfdText(vg, math::Vec(left, line1), string::f("%02d", module->recSlot + 1), 13.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		st::vfdText(vg, math::Vec(mm2px(11.f), line1), "RECORD", 10.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		st::vfdText(vg, math::Vec(right, line1), string::f("%s  %g kHz", stereo ? "STEREO" : "MONO", std::round(r.rate / 100.f) / 10.f),
		            10.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, lit);
		const bool blink = std::fmod(system::getTime(), 0.8) < 0.4;
		std::string l2;
		if (armed)
			l2 = gate ? "ARMED  WAITING FOR REC GATE" : "ARMED  OK = START";
		else
			l2 = std::string(blink ? "* " : "  ") + string::f("REC %.1f S", secs) + (gate ? "" : "  OK = STOP");
		if (armed)
			st::vfdText(vg, math::Vec(left, line2), l2, 7.5f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		else
			st::text(vg, math::Vec(left, line2), l2, 7.5f, nvgRGBf(1.f, 1.f, 1.f), st::FONT_LABEL, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.8f);
		st::vfdLegend(vg, math::Vec(right, line2), "MENU = CANCEL", 6.5f, true, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.6f));

		const math::Rect wr = waveRect();
		const float wh = wr.size.y - mm2px(5.f), mid = wr.pos.y + wh / 2, amp = wh * 0.48f;
		// The zero line; a note on it breaks it, a little air on both sides of the text
		const char* const note = recOver.empty() && armed ? "NOTHING RECORDED YET   60 S MAX" : nullptr;
		const float cx = wr.pos.x + wr.size.x / 2;
		float gapL = cx, gapR = cx;
		if (note) {
			float half = 0.f;
			if (std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL)) {
				nvgFontFaceId(vg, f->handle);
				nvgFontSize(vg, 6.f);
				nvgTextLetterSpacing(vg, 0.6f);
				float bounds[4];
				half = nvgTextBounds(vg, 0.f, 0.f, note, nullptr, bounds) / 2.f;
			}
			gapL = cx - half - mm2px(1.2f);
			gapR = cx + half + mm2px(1.2f);
		}
		nvgBeginPath(vg);
		nvgMoveTo(vg, wr.pos.x, mid);
		nvgLineTo(vg, gapL, mid);
		nvgMoveTo(vg, gapR, mid);
		nvgLineTo(vg, wr.pos.x + wr.size.x, mid);
		nvgStrokeWidth(vg, 0.6f);
		nvgStrokeColor(vg, dim(0.18f));
		nvgStroke(vg);
		if (recOver.empty()) {
			if (note)
				st::vfdLegend(vg, math::Vec(cx, mid), note, 6.f, true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.45f));
		}
		else {
			const float span = std::max(5.f, secs * 1.1f), perSec = 500.f;
			const int cols = (int) (wr.size.x * secs / span);
			nvgBeginPath(vg);
			for (int c = 0; c < cols; ++c) {
				const size_t a = (size_t) (c / wr.size.x * span * perSec), b = (size_t) ((c + 1) / wr.size.x * span * perSec);
				float m = 0.f;
				for (size_t k = a; k < std::min(b + 1, recOver.size()); ++k)
					m = std::max(m, recOver[k]);
				m = std::min(m, 1.f);
				nvgMoveTo(vg, wr.pos.x + c + 0.5f, mid - m * amp);
				nvgLineTo(vg, wr.pos.x + c + 0.5f, mid + std::max(m * amp, 0.4f));
			}
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, lit);
			nvgStroke(vg);
			const float x = wr.pos.x + wr.size.x * secs / span;
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, wr.pos.y);
			nvgLineTo(vg, x, wr.pos.y + wh);
			nvgStrokeColor(vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgStroke(vg);
		}
		// Meters
		const float my = wr.pos.y + wr.size.y - mm2px(3.6f), mw = wr.size.x - mm2px(14.f), mx = wr.pos.x + mm2px(3.f);
		auto db = [](float v) { return math::clamp(1.f + 20.f * std::log10(std::max(v, 1e-5f)) / 48.f, 0.f, 1.f); };
		for (int c = 0; c < (stereo ? 2 : 1); ++c) {
			const float y = my + c * mm2px(2.2f), h = mm2px(1.4f);
			st::vfdLegend(vg, math::Vec(wr.pos.x, y + h / 2), c ? "R" : "L", 5.f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.6f));
			nvgBeginPath(vg);
			nvgRect(vg, mx, y, mw, h);
			nvgFillColor(vg, dim(0.12f));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, mx, y, mw * db(meter[c]), h);
			nvgFillColor(vg, dim(0.95f));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, mx + mw * db(hold[c]) - 1.f, y, 2.f, h);
			nvgFillColor(vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgFill(vg);
		}
		for (float d : {-24.f, -12.f, -6.f}) {
			const float x = mx + mw * (1.f + d / 48.f);
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, my - 1.f);
			nvgLineTo(vg, x, my + mm2px(3.8f));
			nvgStrokeWidth(vg, 0.6f);
			nvgStrokeColor(vg, dim(0.3f));
			nvgStroke(vg);
		}
		if (clip)
			st::text(vg, math::Vec(right, my + mm2px(1.6f)), "CLIP", 6.f, nvgRGBf(1.f, 1.f, 1.f), st::FONT_LABEL,
			         NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, 0.8f);
		else
			st::vfdLegend(vg, math::Vec(right, my + mm2px(1.6f)), "0 DB", 5.5f, true, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.5f));
	}

	/** Text cut in the middle until it fits `maxW` */
	std::string fit(NVGcontext* vg, const std::string& s, float size, float spacing, float maxW) {
		namespace st = jxmp::style;
		std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL);
		if (!f)
			return s;
		nvgFontFaceId(vg, f->handle);
		nvgFontSize(vg, size);
		nvgTextLetterSpacing(vg, spacing);
		float bounds[4];
		std::string t = s;
		for (size_t n = s.size(); n >= 5 && nvgTextBounds(vg, 0.f, 0.f, t.c_str(), nullptr, bounds) > maxW; --n)
			t = sm::shortenMiddle(s, n);
		return t;
	}

	/** The browser: the folder, its folders and sound files (the highlighted one inverted), below the highlighted
	 * file's wave, length and format with the preview's cursor */
	void drawBrowser(NVGcontext* vg, NVGcolor lit) {
		namespace st = jxmp::style;
		auto dim = [&](float a) { return nvgRGBAf(lit.r, lit.g, lit.b, a); };
		const float line1 = mm2px(4.f), left = mm2px(3.f), right = box.size.x - mm2px(3.f);
		const int count = (int) br.items.size();
		const std::string pos = count ? string::f("%d/%d", br.sel + 1, count) : std::string("0/0");
		st::vfdLegend(vg, math::Vec(right, line1), pos, 6.5f, true, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.6f));
		st::vfdText(vg, math::Vec(left, line1), fit(vg, string::uppercase(br.path), 10.f, 0.6f, right - left - mm2px(12.f)), 10.f,
		            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		auto hline = [&](float y, float a) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, left, y);
			nvgLineTo(vg, right, y);
			nvgStrokeWidth(vg, 0.7f);
			nvgStrokeColor(vg, dim(a));
			nvgStroke(vg);
		};
		hline(mm2px(6.6f), 0.35f);
		const float rowH = mm2px(3.9f), y0 = mm2px(7.4f), barR = right - mm2px(2.f);
		br.follow(kBrRows);
		if (!count)
			st::vfdLegend(vg, math::Vec(box.size.x / 2, y0 + 2 * rowH), "NO FOLDERS OR SOUNDS HERE", 7.f, true,
			              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.5f));
		for (int r = 0; r < kBrRows; ++r) {
			const int i = br.top + r;
			if (i >= count)
				break;
			const sm::BrowserEntry& e = br.items[i];
			const float y = y0 + r * rowH + rowH / 2;
			const bool on = i == br.sel;
			const NVGcolor textC = on ? nvgRGB(0x14, 0x08, 0x0c) : e.dir ? dim(0.75f) : lit;
			if (on) {
				nvgBeginPath(vg);
				nvgRect(vg, left, y - rowH / 2 + 0.8f, barR - left, rowH - 1.6f);
				nvgFillColor(vg, dim(0.95f));
				nvgFill(vg);
			}
			const std::string shown = e.name == ".." ? std::string("..  (UP)")
			                                         : string::uppercase(e.dir ? e.name : sm::stripExtension(e.name));
			const std::string ext = e.dir ? std::string() : string::uppercase(e.name.substr(e.name.find_last_of('.') + 1));
			const std::string name = fit(vg, shown, 8.f, 0.8f, barR - left - mm2px(12.f));
			if (on)
				st::text(vg, math::Vec(left + mm2px(2.f), y), name, 8.f, textC, st::FONT_LABEL, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.8f);
			else if (e.dir)
				st::vfdLegend(vg, math::Vec(left + mm2px(2.f), y), name, 8.f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, textC);
			else
				st::vfdText(vg, math::Vec(left + mm2px(2.f), y), name, 8.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
			const float ax = barR - mm2px(2.f);
			if (e.dir) {
				nvgBeginPath(vg);
				nvgMoveTo(vg, ax - 3.f, y - 3.f);
				nvgLineTo(vg, ax + 1.5f, y);
				nvgLineTo(vg, ax - 3.f, y + 3.f);
				nvgClosePath(vg);
				nvgFillColor(vg, textC);
				nvgFill(vg);
			}
			else if (on)
				st::text(vg, math::Vec(ax + 1.5f, y), ext, 6.f, textC, st::FONT_LABEL, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, 0.6f);
			else
				st::vfdLegend(vg, math::Vec(ax + 1.5f, y), ext, 6.f, true, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.7f));
		}
		if (count > kBrRows) {
			const float bh = kBrRows * rowH, bx = right - mm2px(0.6f);
			nvgBeginPath(vg);
			nvgRect(vg, bx, y0, 1.5f, bh);
			nvgFillColor(vg, dim(0.15f));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, bx - 0.5f, y0 + bh * br.top / count, 2.5f, bh * kBrRows / count);
			nvgFillColor(vg, dim(0.8f));
			nvgFill(vg);
		}
		// The highlighted entry below the list
		const float sy = y0 + kBrRows * rowH + mm2px(0.8f), sh = box.size.y - sy - mm2px(1.2f), mid = sy + sh / 2;
		hline(sy - mm2px(0.4f), 0.2f);
		const sm::BrowserEntry* e = br.selected();
		if (!e)
			return;
		if (e->dir) {
			st::vfdLegend(vg, math::Vec(left, mid), e->name == ".." ? "OK = ONE FOLDER UP" : "OK = OPEN FOLDER", 6.5f, true,
			              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.5f));
			return;
		}
		if (!brData) {
			// Not read yet (the selection is moving), or it could not be read
			st::vfdLegend(vg, math::Vec(left, mid), brInfo.empty() ? "..." : brInfo, 6.5f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
			              dim(brInfo.empty() ? 0.4f : 0.7f));
			return;
		}
		const float ww = right - left - mm2px(22.f);
		const int cols = std::max(1, (int) ww);
		const size_t parts = brMin.size();
		nvgBeginPath(vg);
		for (int c = 0; c < cols && parts; ++c) {
			const size_t a = (size_t) c * parts / cols, b = std::max(a + 1, (size_t) (c + 1) * parts / cols);
			float lo = 0.f, hi = 0.f;
			for (size_t k = a; k < b && k < parts; ++k) {
				lo = std::min(lo, brMin[k]);
				hi = std::max(hi, brMax[k]);
			}
			nvgMoveTo(vg, left + c + 0.5f, mid - std::min(hi, 1.f) * sh * 0.48f);
			nvgLineTo(vg, left + c + 0.5f, mid - std::max(lo, -1.f) * sh * 0.48f + 0.4f);
		}
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, dim(0.8f));
		nvgStroke(vg);
		const double p = module->previewPos.load(std::memory_order_relaxed);
		if (p >= 0.0 && brData->frames()) {
			const float x = left + ww * (float) (p / brData->frames());
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, sy);
			nvgLineTo(vg, x, sy + sh);
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgStroke(vg);
		}
		st::vfdText(vg, math::Vec(right, sy + sh * 0.28f), string::f("%.2f S", brData->frames() / brData->rate), 7.f,
		            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, lit);
		st::vfdLegend(vg, math::Vec(right, sy + sh * 0.78f), brInfo, 5.5f, true, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.6f));
	}

	void drawMenu(NVGcontext* vg, NVGcolor lit) {
		namespace st = jxmp::style;
		auto dim = [&](float a) { return nvgRGBAf(lit.r, lit.g, lit.b, a); };
		const sm::Menu& m = module->menu;
		const int sel = module->selected();
		const float line1 = mm2px(4.f), left = mm2px(3.f), right = box.size.x - mm2px(3.f);
		const std::string title = m.depth == 0 ? string::f("%02d  %s", sel + 1, module->ui[sel].name.empty() ? "EMPTY" : module->ui[sel].name.c_str())
		                                       : m.path();
		st::vfdText(vg, math::Vec(left, line1), title, 10.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		st::vfdLegend(vg, math::Vec(right, line1), m.depth ? "MENU = BACK" : "MENU = CLOSE", 6.5f, true,
		              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.6f));
		nvgBeginPath(vg);
		nvgMoveTo(vg, left, mm2px(6.6f));
		nvgLineTo(vg, right, mm2px(6.6f));
		nvgStrokeWidth(vg, 0.7f);
		nvgStrokeColor(vg, dim(0.35f));
		nvgStroke(vg);
		if (m.confirming) {
			st::vfdText(vg, math::Vec(box.size.x / 2, mm2px(15.f)), string::f("CLEAR SAMPLE %02d?", sel + 1), 10.f,
			            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, lit);
			st::vfdLegend(vg, math::Vec(box.size.x / 2, mm2px(21.f)), "OK = YES     MENU = NO", 7.5f, true,
			              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.75f));
			return;
		}
		// Four rows; the same gap of 3 VFD dots (the grid's pitch) below the title line and above the glass's lower edge
		const int rows = 4, count = m.count(), cur = m.selected();
		if (cur < menuTop)
			menuTop = cur;
		if (cur >= menuTop + rows)
			menuTop = cur - rows + 1;
		menuTop = math::clamp(menuTop, 0, std::max(0, count - rows));
		const float gap = 3.f * mm2px(0.55f), y0 = mm2px(6.6f) + gap;
		const float listEnd = box.size.y - st::vfdBezel(box.size) - gap, rowH = (listEnd - y0) / rows, barR = right - mm2px(2.f);
		for (int r = 0; r < rows; ++r) {
			const int i = menuTop + r;
			if (i >= count)
				break;
			const sm::MenuItem& it = m.current()[i];
			const float y = y0 + r * rowH + rowH / 2;
			const bool on = i == cur;
			// ON / OFF says what it is now and what OK does
			std::string name = it.name;
			if (it.action == sm::A_ENV_ONOFF) {
				const sm::SlotSettings ss = module->currentSettings(sel);
				const bool envOn = it.value > 0.5f ? ss.panEnv.on : ss.volEnv.on;
				name = envOn ? "ON  (OK = OFF)" : "OFF  (OK = ON)";
			}
			const NVGcolor textC = on ? nvgRGB(0x14, 0x08, 0x0c) : lit;
			if (on) {
				nvgBeginPath(vg);
				nvgRect(vg, left, y - rowH / 2 + 0.8f, barR - left, rowH - 1.6f);
				nvgFillColor(vg, dim(0.95f));
				nvgFill(vg);
				st::text(vg, math::Vec(left + mm2px(2.f), y), name, 8.5f, textC, st::FONT_LABEL, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.8f);
			}
			else
				st::vfdText(vg, math::Vec(left + mm2px(2.f), y), name, 8.5f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
			const float ax = barR - mm2px(2.f);
			if (it.sub) {
				// A folder: a small arrow on the right
				nvgBeginPath(vg);
				nvgMoveTo(vg, ax - 3.f, y - 3.5f);
				nvgLineTo(vg, ax + 2.f, y);
				nvgLineTo(vg, ax - 3.f, y + 3.5f);
				nvgClosePath(vg);
				nvgFillColor(vg, textC);
				nvgFill(vg);
			}
			if ((it.action == sm::A_XFADE && it.value == m.lastXfade) || (it.action == sm::A_FADE && it.value == m.fadeNow)) {
				// The crossfade length used last, the slot's fadeout
				nvgBeginPath(vg);
				nvgCircle(vg, ax, y, 2.f);
				nvgFillColor(vg, textC);
				nvgFill(vg);
			}
		}
		if (count > rows) {
			const float bh = listEnd - y0, bx = right - mm2px(0.6f);
			nvgBeginPath(vg);
			nvgRect(vg, bx, y0, 1.5f, bh);
			nvgFillColor(vg, dim(0.15f));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, bx - 0.5f, y0 + bh * menuTop / count, 2.5f, bh * rows / count);
			nvgFillColor(vg, dim(0.8f));
			nvgFill(vg);
		}
	}

	void drawSlot(NVGcontext* vg, NVGcolor lit) {
		namespace st = jxmp::style;
		auto dim = [&](float a) { return nvgRGBAf(lit.r, lit.g, lit.b, a); };
		syncView();
		const int sel = module ? module->selected() : 6;
		const Smpl::UiSlot* u = module ? &module->ui[sel] : nullptr;
		const bool has = u && u->data;
		const sm::SlotSettings set = has ? module->currentSettings(sel) : sm::SlotSettings();
		const float line1 = mm2px(4.f), line2 = mm2px(8.f);

		st::vfdText(vg, math::Vec(mm2px(3.f), line1), string::f("%02d", sel + 1), 13.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		const std::string name = u ? (has ? u->name : (u->name.empty() ? "EMPTY" : u->name)) : "GUITAR E2";
		st::vfdText(vg, math::Vec(mm2px(11.f), line1), name, 10.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		const bool env = has && envView != 0;
		if (env)
			st::vfdText(vg, math::Vec(box.size.x - mm2px(3.f), line1), envView == 1 ? "VOLUME ENV" : "PAN ENV", 10.f,
			            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, lit);
		else if (has || !module)
			st::vfdText(vg, math::Vec(box.size.x - mm2px(3.f), line1),
			            sm::noteName(module ? set.root : 40) + string::f("  %+03d", module ? (int) std::round(set.fine) : 0), 10.f,
			            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, lit);
		// Line 2: a fresh message, else the seam view / zoom / mode and loop on the left, rate and length on the right
		const bool fresh = u && !u->message.empty() && system::getTime() - u->messageTime < 4.0;
		const double z = zoom();
		if (fresh)
			st::vfdText(vg, math::Vec(mm2px(3.f), line2), u->message, 7.5f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		else if (has) {
			std::string mode;
			if (env)
				mode = envLine(set, sel);
			else if (seam)
				mode = "LOOP SEAM   END | START";
			else if (z > 1.05)
				mode = z < 10.0 ? string::f("ZOOM %.1fX", z) : string::f("ZOOM %dX", (int) std::round(z));
			else {
				mode = set.mode == sm::ONE_SHOT ? "ONE-SHOT" : "GATE";
				if (set.mode == sm::GATE)
					mode += set.loop == sm::LOOP_OFF ? "  LOOP OFF" : set.loop == sm::LOOP_FWD ? "  LOOP FWD" : "  LOOP PING";
				if (set.volEnv.on || set.panEnv.on)
					mode += std::string("  ENV") + (set.volEnv.on ? " V" : "") + (set.panEnv.on ? " P" : "");
			}
			st::vfdLegend(vg, math::Vec(mm2px(3.f), line2), mode, 7.5f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.75f));
		}
		if (env)
			st::vfdLegend(vg, math::Vec(box.size.x - mm2px(3.f), line2), pointLine(set), 7.5f, true,
			              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.75f));
		else if (has)
			st::vfdLegend(vg, math::Vec(box.size.x - mm2px(3.f), line2),
			              string::f("%.1f kHz  %.2f s", u->data->rate / 1000.f, u->data->frames() / u->data->rate), 7.5f, true,
			              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.75f));
		else if (module && !fresh)
			st::vfdLegend(vg, math::Vec(mm2px(3.f), line2), "MENU > SAMPLE > BROWSE, OR DROP A FILE", 7.5f, true,
			              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.75f));

		const math::Rect wr = waveRect();
		const float mid = wr.pos.y + wr.size.y / 2;
		if (envView != 2 || !has) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, wr.pos.x, mid);
			nvgLineTo(vg, wr.pos.x + wr.size.x, mid);
			nvgStrokeWidth(vg, 0.6f);
			nvgStrokeColor(vg, dim(0.18f));
			nvgStroke(vg);
		}
		if (has) {
			if (env)
				drawEnv(vg, lit, *u, set, sel);
			else if (seam)
				drawSeam(vg, lit, *u, set);
			else
				drawWave(vg, lit, *u, set, sel, z);
		}

		// Slot strip: filled = has a sample, bright = playing, white frame = selected
		for (int i = 0; i < kSlots; ++i) {
			const math::Rect b = stripBox(i);
			const bool loaded = module ? (bool) module->ui[i].data : (i < 9 || i == 12);
			const bool playing = module ? module->audio[i].shownActive.load(std::memory_order_relaxed) : (i == 0 || i == 6);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, RECT_ARGS(b), 1.f);
			if (loaded) {
				nvgFillColor(vg, playing ? lit : dim(0.4f));
				nvgFill(vg);
			}
			nvgStrokeWidth(vg, i == sel ? 1.3f : 0.7f);
			nvgStrokeColor(vg, i == sel ? nvgRGBf(1.f, 1.f, 1.f) : dim(0.4f));
			nvgStroke(vg);
			st::vfdLegend(vg, math::Vec(b.pos.x + b.size.x / 2, b.pos.y - mm2px(1.1f)), string::f("%d", i + 1), 5.f, true,
			              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, i == sel ? nvgRGBf(1.f, 1.f, 1.f) : dim(0.5f));
		}
	}

	/** Where the selected slot's envelope plays (seconds), or -1 */
	float playTime(int sel) const {
		if (!module || !module->audio[sel].shownActive.load(std::memory_order_relaxed))
			return -1.f;
		return (envView == 2 ? module->audio[sel].shownPanT : module->audio[sel].shownVolT).load(std::memory_order_relaxed);
	}

	/** Line 2 in the envelope view: ON/OFF, sustain, loop, fadeout; while playing the value it gives */
	std::string envLine(const sm::SlotSettings& set, int sel) const {
		const sm::Envelope& e = envOf(set);
		const float t = playTime(sel);
		if (t >= 0.f && e.on) {
			const float v = sm::envValue(e, t);
			return envView == 1 ? string::f("PLAYING  VOL %d %%", (int) std::round(v * 100.f)) : "PLAYING  PAN " + panName(v);
		}
		std::string l = e.on ? "ON" : "OFF";
		if (e.sus >= 0)
			l += string::f("  SUS %d", e.sus + 1);
		if (e.ls >= 0)
			l += string::f("  LOOP %d-%d", e.ls + 1, e.le + 1);
		if (envView == 1 && set.fadeout > 0.f)
			l += string::f("  FADE %g S", set.fadeout);
		return l;
	}

	/** Right of line 2: the selected point, or how many there are */
	std::string pointLine(const sm::SlotSettings& set) const {
		const sm::Envelope& e = envOf(set);
		if (selPt < 0 || selPt >= e.n)
			return string::f("%d POINTS", e.n);
		const float t = e.t[selPt], v = e.v[selPt];
		const std::string ts = t < 1.f ? string::f("%d MS", (int) std::round(t * 1000.f)) : string::f("%.2f S", t);
		return string::f("P%d  ", selPt + 1) + ts + "  " + (envView == 1 ? string::f("%d %%", (int) std::round(v * 100.f)) : panName(v));
	}

	/** The envelope as FT2 shows it: lines between square points, the sustain as a dashed line, the loop shaded; the
	 * wave faintly behind (from START, at the original pitch); a cursor where it plays */
	void drawEnv(NVGcontext* vg, NVGcolor lit, const Smpl::UiSlot& u, const sm::SlotSettings& set, int sel) {
		namespace st = jxmp::style;
		auto dim = [&](float a) { return nvgRGBAf(lit.r, lit.g, lit.b, a); };
		const sm::Envelope& e = envOf(set);
		if (dragging != H_ENV)
			envSpan = spanOf(e);
		const math::Rect wr = waveRect();
		const float A = e.on ? 1.f : 0.4f;
		nvgSave(vg);
		nvgIntersectScissor(vg, wr.pos.x, wr.pos.y - 2.f, wr.size.x, wr.size.y + 4.f);
		// The wave behind
		const sm::SampleData& d = *u.data;
		const float mid = yV(0.5f);
		nvgBeginPath(vg);
		for (int c = 0; c < (int) wr.size.x; c += 2) {
			const double f0 = set.start + tX(wr.pos.x + c) * d.rate, f1 = set.start + tX(wr.pos.x + c + 2) * d.rate;
			if (f0 >= set.end)
				break;
			float m = 0.f;
			const long a = (long) f0, b = std::min((long) set.end, (long) std::ceil(f1));
			const long step = std::max(1L, (b - a) / 32);
			for (long k = a; k < b; k += step)
				m = std::max(m, std::fabs(sm::monoAt(d, (size_t) k)));
			nvgRect(vg, wr.pos.x + c, mid - m * wr.size.y * 0.5f, 1.4f, std::max(0.5f, m * wr.size.y));
		}
		nvgFillColor(vg, dim(0.13f));
		nvgFill(vg);
		// Time grid
		const float stepT = envSpan > 3.f ? 1.f : envSpan > 1.2f ? 0.5f : 0.25f;
		for (float t = stepT; t < envSpan; t += stepT) {
			const float x = xT(t);
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, wr.pos.y);
			nvgLineTo(vg, x, wr.pos.y + wr.size.y);
			nvgStrokeWidth(vg, 0.6f);
			nvgStrokeColor(vg, dim(0.1f));
			nvgStroke(vg);
			st::vfdLegend(vg, math::Vec(x + 2.f, wr.pos.y + mm2px(0.9f)),
			              t < 1.f ? string::f("%d MS", (int) std::round(t * 1000.f)) : string::f("%g S", t), 4.5f, true,
			              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.45f));
		}
		if (envView == 2) {
			st::vfdLegend(vg, math::Vec(wr.pos.x + 2.f, wr.pos.y + mm2px(2.5f)), "L", 5.f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.5f));
			st::vfdLegend(vg, math::Vec(wr.pos.x + 2.f, wr.pos.y + wr.size.y - mm2px(1.f)), "R", 5.f, true,
			              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.5f));
		}
		auto vline = [&](float x, NVGcolor c) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, wr.pos.y);
			nvgLineTo(vg, x, wr.pos.y + wr.size.y);
			nvgStrokeWidth(vg, 0.8f);
			nvgStrokeColor(vg, c);
			nvgStroke(vg);
		};
		// Loop and sustain
		if (e.ls >= 0 && e.le >= 0) {
			const float a = xT(e.t[e.ls]), b = xT(e.t[e.le]);
			nvgBeginPath(vg);
			nvgRect(vg, a, wr.pos.y, b - a, wr.size.y);
			nvgFillColor(vg, dim(e.on ? 0.1f : 0.05f));
			nvgFill(vg);
			vline(a, dim(0.6f * A));
			vline(b, dim(0.6f * A));
			st::vfdLegend(vg, math::Vec((a + b) / 2, wr.pos.y + mm2px(2.5f)), "LOOP", 4.5f, true,
			              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.8f * A));
		}
		const NVGcolor pale = nvgRGBf(lit.r * 0.4f + 0.6f, lit.g * 0.4f + 0.6f, lit.b * 0.4f + 0.6f);
		if (e.sus >= 0) {
			const float x = xT(e.t[e.sus]);
			for (float y = wr.pos.y; y < wr.pos.y + wr.size.y; y += 6.f) {
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, y);
				nvgLineTo(vg, x, std::min(y + 3.f, wr.pos.y + wr.size.y));
				nvgStrokeWidth(vg, 0.9f);
				nvgStrokeColor(vg, pale);
				nvgStroke(vg);
			}
			st::vfdLegend(vg, math::Vec(x + 2.f, wr.pos.y + wr.size.y - mm2px(1.f)), "SUS", 4.5f, true,
			              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, pale);
		}
		// The curve, on to the right edge after the last point
		nvgBeginPath(vg);
		for (int k = 0; k < e.n; ++k) {
			if (k)
				nvgLineTo(vg, xT(e.t[k]), yV(e.v[k]));
			else
				nvgMoveTo(vg, xT(e.t[k]), yV(e.v[k]));
		}
		nvgLineTo(vg, wr.pos.x + wr.size.x, yV(e.v[e.n - 1]));
		nvgStrokeWidth(vg, 1.3f);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStrokeColor(vg, dim(A));
		nvgStroke(vg);
		for (int k = 0; k < e.n; ++k) {
			const float sz = k == selPt ? 5.f : 3.6f;
			nvgBeginPath(vg);
			nvgRect(vg, xT(e.t[k]) - sz / 2, yV(e.v[k]) - sz / 2, sz, sz);
			nvgFillColor(vg, k == selPt ? nvgRGBf(1.f, 1.f, 1.f) : e.on ? pale : dim(0.45f));
			nvgFill(vg);
		}
		// Where it plays
		const float t = playTime(sel);
		if (t >= 0.f && e.on) {
			const float x = xT(std::min(t, envSpan));
			vline(x, nvgRGBf(1.f, 1.f, 1.f));
			nvgBeginPath(vg);
			nvgCircle(vg, x, yV(sm::envValue(e, t)), 2.2f);
			nvgFillColor(vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgFill(vg);
		}
		nvgRestore(vg);
	}

	void drawWave(NVGcontext* vg, NVGcolor lit, const Smpl::UiSlot& u, const sm::SlotSettings& set, int sel, double z) {
		namespace st = jxmp::style;
		auto dim = [&](float a) { return nvgRGBAf(lit.r, lit.g, lit.b, a); };
		const math::Rect wr = waveRect();
		auto visible = [&](float x) { return x >= wr.pos.x - 1.f && x <= wr.pos.x + wr.size.x + 1.f; };
		nvgSave(vg);
		nvgScissor(vg, wr.pos.x - 1.f, wr.pos.y - mm2px(1.6f), wr.size.x + 2.f, wr.size.y + mm2px(3.2f));
		// The loop is always shown (and can be dragged); dimmed when it does not apply (ONE-SHOT or LOOP OFF)
		const bool loopOn = set.mode == sm::GATE && set.loop != sm::LOOP_OFF;
		const float loopA = loopOn ? 1.f : 0.35f;
		nvgBeginPath(vg);
		nvgRect(vg, xOf(set.loopStart), wr.pos.y, xOf(set.loopEnd) - xOf(set.loopStart), wr.size.y);
		nvgFillColor(vg, dim(0.12f * loopA));
		nvgFill(vg);
		// The wave: bright between START and END, only a faint trace under a dark veil outside
		bool dots = false;
		wavePath(vg, u, view0, view1, wr.pos.x, wr.size.x, dots);
		const float xs = math::clamp(xOf(set.start), wr.pos.x, wr.pos.x + wr.size.x);
		const float xe = math::clamp(xOf(set.end), wr.pos.x, wr.pos.x + wr.size.x);
		nvgStrokeWidth(vg, dots || z > 1.05 ? 1.1f : std::max(0.8f, 0.9f));
		for (int k = 0; k < 3; ++k) {
			const float x = k == 0 ? xs : k == 1 ? wr.pos.x : xe;
			const float w = k == 0 ? xe - xs : k == 1 ? xs - wr.pos.x : wr.pos.x + wr.size.x - xe;
			if (w <= 0.f)
				continue;
			nvgSave(vg);
			nvgIntersectScissor(vg, x, wr.pos.y - 2.f, w, wr.size.y + 4.f);
			nvgStrokeColor(vg, k == 0 ? lit : dim(0.14f));
			nvgStroke(vg);
			nvgRestore(vg);
		}
		if (dots) {
			const NVGcolor pale = nvgRGBf(lit.r * 0.4f + 0.6f, lit.g * 0.4f + 0.6f, lit.b * 0.4f + 0.6f);
			frameDots(vg, u, view0, view1, wr.pos.x, wr.size.x, pale);
		}
		for (int k = 0; k < 2; ++k) {
			const float x = k == 0 ? wr.pos.x : xe, w = k == 0 ? xs - wr.pos.x : wr.pos.x + wr.size.x - xe;
			if (w <= 0.f)
				continue;
			nvgBeginPath(vg);
			nvgRect(vg, x, wr.pos.y - 1.f, w, wr.size.y + 2.f);
			nvgFillColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.45f));
			nvgFill(vg);
		}
		// START / END lines with handles below
		const NVGcolor pale = nvgRGBf(lit.r * 0.4f + 0.6f, lit.g * 0.4f + 0.6f, lit.b * 0.4f + 0.6f);
		const char* const se[2] = {"S", "E"};
		const size_t sePos[2] = {set.start, set.end};
		for (int k = 0; k < 2; ++k) {
			const float x = xOf((double) sePos[k]);
			if (!visible(x))
				continue;
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, wr.pos.y);
			nvgLineTo(vg, x, wr.pos.y + wr.size.y);
			nvgStrokeWidth(vg, 0.8f);
			nvgStrokeColor(vg, pale);
			nvgStroke(vg);
			const float hy = wr.pos.y + wr.size.y;
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, hy);
			nvgLineTo(vg, x - 3.5f, hy + 4.f);
			nvgLineTo(vg, x + 3.5f, hy + 4.f);
			nvgClosePath(vg);
			nvgFillColor(vg, pale);
			nvgFill(vg);
			st::vfdLegend(vg, math::Vec(x + (k == 0 ? 2.5f : -2.5f), hy - 4.f), se[k], 6.f, true,
			              (k == 0 ? NVG_ALIGN_LEFT : NVG_ALIGN_RIGHT) | NVG_ALIGN_MIDDLE, pale);
		}
		// Loop flags at the top
		const size_t lp[2] = {set.loopStart, set.loopEnd};
		for (int k = 0; k < 2; ++k) {
			const float x = xOf((double) lp[k]);
			if (!visible(x))
				continue;
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, wr.pos.y);
			nvgLineTo(vg, x, wr.pos.y + wr.size.y);
			nvgStrokeWidth(vg, 0.7f);
			nvgStrokeColor(vg, dim(0.7f * loopA));
			nvgStroke(vg);
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, wr.pos.y);
			nvgLineTo(vg, x + (k ? -6.f : 6.f), wr.pos.y + 2.5f);
			nvgLineTo(vg, x, wr.pos.y + 5.f);
			nvgClosePath(vg);
			nvgFillColor(vg, dim(loopA));
			nvgFill(vg);
		}
		// Where the view is in the sample: a thin bar over the wave while zoomed
		if (z > 1.05) {
			const float y = wr.pos.y - mm2px(1.2f), n = (float) u.data->frames();
			nvgBeginPath(vg);
			nvgRect(vg, wr.pos.x, y, wr.size.x, 1.5f);
			nvgFillColor(vg, dim(0.18f));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, wr.pos.x + (float) (view0 / n) * wr.size.x, y - 0.5f,
			        std::max(3.f, (float) ((view1 - view0) / n) * wr.size.x), 2.5f);
			nvgFillColor(vg, dim(0.9f));
			nvgFill(vg);
		}
		// Playhead; hidden while the engine is paused (Space), so nothing but S marks where the sample begins
		const uint32_t ticks = module->ticks.load(std::memory_order_relaxed);
		const double now = system::getTime();
		if (ticks != lastTicks) {
			lastTicks = ticks;
			lastTickTime = now;
		}
		const bool running = now - lastTickTime < 0.1;
		const double pos = module->audio[sel].shownPos.load(std::memory_order_relaxed);
		if (pos >= 0.0 && running && visible(xOf(pos))) {
			const float x = xOf(pos);
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, wr.pos.y);
			nvgLineTo(vg, x, wr.pos.y + wr.size.y);
			nvgStrokeWidth(vg, 1.2f);
			nvgStrokeColor(vg, nvgRGBf(1.f, 1.f, 1.f));
			nvgStroke(vg);
		}
		nvgRestore(vg);
	}

	/** The loop seam: the end of the loop on the left, its start on the right, joined in the middle */
	void drawSeam(NVGcontext* vg, NVGcolor lit, const Smpl::UiSlot& u, const sm::SlotSettings& set) {
		namespace st = jxmp::style;
		auto dim = [&](float a) { return nvgRGBAf(lit.r, lit.g, lit.b, a); };
		const math::Rect wr = waveRect();
		const float half = wr.size.x / 2, cx = wr.pos.x + half;
		const double span = std::min(300.0, std::max(16.0, (double) set.loopEnd - set.loopStart));
		const NVGcolor pale = nvgRGBf(lit.r * 0.4f + 0.6f, lit.g * 0.4f + 0.6f, lit.b * 0.4f + 0.6f);
		nvgSave(vg);
		nvgScissor(vg, wr.pos.x, wr.pos.y - 2.f, wr.size.x, wr.size.y + 4.f);
		for (int k = 0; k < 2; ++k) {
			const double f0 = k == 0 ? set.loopEnd - span : set.loopStart;
			bool dots = false;
			wavePath(vg, u, f0, f0 + span, k == 0 ? wr.pos.x : cx, half, dots);
			nvgStrokeWidth(vg, 1.1f);
			nvgStrokeColor(vg, lit);
			nvgStroke(vg);
			if (dots)
				frameDots(vg, u, f0, f0 + span, k == 0 ? wr.pos.x : cx, half, pale);
		}
		nvgRestore(vg);
		nvgBeginPath(vg);
		nvgMoveTo(vg, cx, wr.pos.y);
		nvgLineTo(vg, cx, wr.pos.y + wr.size.y);
		nvgStrokeWidth(vg, 0.8f);
		nvgStrokeColor(vg, dim(0.7f));
		nvgStroke(vg);
		const float by = wr.pos.y + wr.size.y - 4.f;
		st::vfdLegend(vg, math::Vec(cx - 3.f, by), "LOOP END", 6.f, true, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, pale);
		st::vfdLegend(vg, math::Vec(cx + 3.f, by), "LOOP ST", 6.f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, pale);
		const bool clicks = sm::seamClicks(*u.data, set.loopStart, set.loopEnd);
		st::vfdLegend(vg, math::Vec(cx, wr.pos.y + 4.f), clicks ? "SEAM CLICKS" : "SEAM IS SMOOTH", 6.f, true,
		              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, clicks ? nvgRGBf(1.f, 1.f, 1.f) : dim(0.8f));
	}

	/** Which handle is near x (and in its zone: S / E below the middle, loop flags above) */
	int handleAt(math::Vec p) const {
		if (!module || !data() || seam || module->menu.open)
			return H_NONE;
		const int sel = module->selected();
		const sm::SlotSettings set = module->currentSettings(sel);
		const math::Rect wr = waveRect();
		const bool upper = p.y < wr.pos.y + wr.size.y / 2;
		int best = H_NONE;
		float bestD = mm2px(2.5f);
		const size_t pts[4] = {set.start, set.end, set.loopStart, set.loopEnd};
		for (int h = 0; h < 4; ++h) {
			const bool isLoop = h >= H_LOOP_START;
			// Loop flags in the upper half, START / END in the lower half
			if (isLoop != upper)
				continue;
			const float d = std::fabs(p.x - xOf((double) pts[h]));
			if (d < bestD) {
				bestD = d;
				best = h;
			}
		}
		return best;
	}

	bool overWave(math::Vec p) const {
		const math::Rect wr = waveRect();
		return wr.grow(math::Vec(0.f, mm2px(1.5f))).contains(p);
	}

	/** The browser row under `p`, or -1 */
	int brRowAt(math::Vec p) const {
		const float rowH = mm2px(3.9f), y0 = mm2px(7.4f);
		if (p.y < y0 || p.y >= y0 + kBrRows * rowH)
			return -1;
		const int i = br.top + (int) ((p.y - y0) / rowH);
		return i < (int) br.items.size() ? i : -1;
	}

	void onButton(const ButtonEvent& e) override {
		if (browsing && module && !module->menu.open) {
			// A click selects (and so previews) a row, a double click (onDoubleClick) opens or loads it
			if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
				lastPress = e.pos;
				const int i = brRowAt(e.pos);
				if (i >= 0 && i != br.sel) {
					br.select(i, kBrRows);
					brMoved();
				}
				e.consume(this);
			}
			return;
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS || !module || module->menu.open || module->recording())
			return OpaqueWidget::onButton(e);
		for (int i = 0; i < kSlots; ++i) {
			math::Rect b = stripBox(i);
			b = b.grow(math::Vec(1.f, mm2px(2.f)));
			if (b.contains(e.pos)) {
				engine::ParamQuantity* const pq = module->paramQuantities[Smpl::CH_PARAM];
				const float old = pq->getValue();
				pq->setValue((float) i);
				history::ParamChange* const h = new history::ParamChange;
				h->name = "select JX-SMPL slot";
				h->moduleId = module->id;
				h->paramId = Smpl::CH_PARAM;
				h->oldValue = old;
				h->newValue = (float) i;
				APP->history->push(h);
				e.consume(this);
				return;
			}
		}
		lastPress = e.pos;
		if (envView != 0 && data()) {
			// The envelope view: a click selects a point (and starts dragging it), elsewhere none
			const sm::SlotSettings set = module->currentSettings(module->selected());
			selPt = envPointAt(envOf(set), e.pos);
			if (selPt >= 0) {
				dragging = H_ENV;
				envBefore = set;
				dragX = e.pos.x;
				dragY = e.pos.y;
			}
			e.consume(this);
			return;
		}
		dragging = handleAt(e.pos);
		if (dragging != H_NONE) {
			dragX = e.pos.x;
			oldValue = module->params[paramOf(dragging)].getValue();
			e.consume(this);
			return;
		}
		// Dragging the wave pans a zoomed view
		if (data() && !seam && overWave(e.pos)) {
			dragging = H_PAN;
			dragX = dragStartX = e.pos.x;
			panView0 = view0;
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (dragging == H_NONE || !module)
			return;
		dragX += e.mouseDelta.x / getAbsoluteZoom();
		dragY += e.mouseDelta.y / getAbsoluteZoom();
		if (dragging == H_ENV) {
			const int i = module->selected();
			sm::SlotSettings s = module->currentSettings(i);
			sm::envMove(envOf(s), selPt, tX(dragX), vY(dragY));
			module->postSettings(i, s);
			return;
		}
		if (dragging == H_PAN) {
			const math::Rect wr = waveRect();
			const double w = view1 - view0;
			view0 = panView0 - (dragX - dragStartX) / wr.size.x * w;
			view1 = view0 + w;
			clampView();
			return;
		}
		const double n = (double) frames();
		if (n <= 0.0)
			return;
		const float t = (float) dclamp(fOf(dragX) / n, 0.0, 1.0);
		module->paramQuantities[paramOf(dragging)]->setValue(t);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (dragging == H_ENV && module) {
			const int i = module->selected();
			const sm::SlotSettings now = module->currentSettings(i);
			const sm::Envelope &a = envOf(envBefore), &b = envOf(now);
			if (!std::equal(a.t, a.t + a.n, b.t) || !std::equal(a.v, a.v + a.n, b.v))
				changeSlot(module, i, envBefore, now, "move JX-SMPL envelope point");
		}
		if (dragging >= H_START && dragging <= H_LOOP_END && module) {
			history::ParamChange* const h = new history::ParamChange;
			h->name = "move JX-SMPL point";
			h->moduleId = module->id;
			h->paramId = paramOf(dragging);
			h->oldValue = oldValue;
			h->newValue = module->params[paramOf(dragging)].getValue();
			APP->history->push(h);
		}
		dragging = H_NONE;
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		// Only the bare wheel belongs to the display: with Shift / Ctrl it moves or zooms the rack as anywhere else
		if (APP->window->getMods() & RACK_MOD_MASK)
			return OpaqueWidget::onHoverScroll(e);
		if (browsing && module && !module->menu.open) {
			if (e.scrollDelta.y != 0.f)
				brMove(e.scrollDelta.y > 0.f ? -1 : 1);
			e.consume(this);
			return;
		}
		if (!module || module->menu.open || !data() || !overWave(e.pos) || envView != 0)
			return OpaqueWidget::onHoverScroll(e);
		// The wheel zooms around the frame under the pointer
		const double f = fOf(e.pos.x), k = e.scrollDelta.y > 0.f ? 1.0 / 1.3 : 1.3;
		view0 = f - (f - view0) * k;
		view1 = f + (view1 - f) * k;
		seam = false;
		clampView();
		e.consume(this);
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (browsing && module && !module->menu.open) {
			if (brRowAt(lastPress) == br.sel)
				brOk();
			e.consume(this);
			return;
		}
		if (module && !module->menu.open && data() && envView != 0) {
			// On a point: removes it; elsewhere on the graph: adds one there
			const int i = module->selected();
			const sm::SlotSettings set = module->currentSettings(i);
			sm::SlotSettings s = set;
			sm::Envelope& en = envOf(s);
			const int k = envPointAt(en, lastPress);
			if (k == 0)
				module->message(i, "THE FIRST POINT STAYS");
			else if (k > 0) {
				if (sm::envRemove(en, k)) {
					selPt = -1;
					changeSlot(module, i, set, s, "remove JX-SMPL envelope point");
					module->message(i, string::f("POINT %d REMOVED", k + 1));
				}
				else
					module->message(i, "AT LEAST 2 POINTS");
			}
			else if (overWave(lastPress)) {
				const int n = sm::envInsert(en, tX(lastPress.x), vY(lastPress.y));
				if (n < 0)
					module->message(i, "12 POINTS AT MOST");
				else {
					selPt = n;
					changeSlot(module, i, set, s, "add JX-SMPL envelope point");
					module->message(i, string::f("POINT %d ADDED", n + 1));
				}
			}
			e.consume(this);
			return;
		}
		if (module && !module->menu.open && data()) {
			whole();
			e.consume(this);
			return;
		}
		OpaqueWidget::onDoubleClick(e);
	}
};

/** The menu buttons: the same button as PLAY with an icon on its cap (▲ ▼ ≡ ✓). They act on press. */
struct IconButton : VCVButton {
	enum Icon { UP, DOWN, MENU, OK };
	Icon icon = UP;
	std::function<void()> action;

	void onDragStart(const DragStartEvent& e) override {
		VCVButton::onDragStart(e);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && action)
			action();
	}

	void draw(const DrawArgs& args) override {
		VCVButton::draw(args);
		NVGcontext* const vg = args.vg;
		const math::Vec c = box.size.div(2);
		const float s = box.size.x * 0.17f;
		const NVGcolor ic = nvgRGB(0xd2, 0xd4, 0xd9);
		nvgBeginPath(vg);
		switch (icon) {
		case UP:
		case DOWN: {
			const float d = icon == UP ? -1.f : 1.f;
			nvgMoveTo(vg, c.x, c.y + d * s * 0.8f);
			nvgLineTo(vg, c.x + s, c.y - d * s * 0.6f);
			nvgLineTo(vg, c.x - s, c.y - d * s * 0.6f);
			nvgClosePath(vg);
			nvgFillColor(vg, ic);
			nvgFill(vg);
			break;
		}
		case MENU:
			for (int k = -1; k <= 1; ++k) {
				nvgMoveTo(vg, c.x - s, c.y + k * s * 0.62f);
				nvgLineTo(vg, c.x + s, c.y + k * s * 0.62f);
			}
			nvgStrokeWidth(vg, 1.2f);
			nvgLineCap(vg, NVG_ROUND);
			nvgStrokeColor(vg, ic);
			nvgStroke(vg);
			break;
		case OK:
			nvgMoveTo(vg, c.x - s, c.y + s * 0.05f);
			nvgLineTo(vg, c.x - s * 0.3f, c.y + s * 0.72f);
			nvgLineTo(vg, c.x + s, c.y - s * 0.6f);
			nvgStrokeWidth(vg, 1.3f);
			nvgLineCap(vg, NVG_ROUND);
			nvgLineJoin(vg, NVG_ROUND);
			nvgStrokeColor(vg, ic);
			nvgStroke(vg);
			break;
		}
	}
};

/** A grey frame round the four menu buttons (like the section lines) with MENU in a gap of its top line. The word is
 * the menu's light: dark green while the menu is closed, lit (green with a glow, like the toggles' LEDs) while open. */
struct MenuFrame : widget::TransparentWidget {
	Smpl* module = nullptr;
	math::Rect frame;  // in this widget, px

	void draw(const DrawArgs& args) override {
		namespace st = jxmp::style;
		NVGcontext* const vg = args.vg;
		const bool on = module && module->menu.open;
		const float size = 6.f, r = mm2px(1.2f);
		const math::Vec t(frame.pos.x + frame.size.x / 2, frame.pos.y);
		// The gap in the top line: the word's width and a little air
		float half = mm2px(5.f);
		if (std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL)) {
			nvgFontFaceId(vg, f->handle);
			nvgFontSize(vg, size);
			nvgTextLetterSpacing(vg, 0.8f);
			float bounds[4];
			const float w = nvgTextBounds(vg, 0, 0, "MENU", nullptr, bounds);
			half = w / 2 + mm2px(1.f);
		}
		const float x0 = frame.pos.x, y0 = frame.pos.y, x1 = x0 + frame.size.x, y1 = y0 + frame.size.y;
		nvgBeginPath(vg);
		nvgMoveTo(vg, t.x + half, y0);
		nvgArcTo(vg, x1, y0, x1, y1, r);
		nvgArcTo(vg, x1, y1, x0, y1, r);
		nvgArcTo(vg, x0, y1, x0, y0, r);
		nvgArcTo(vg, x0, y0, t.x - half, y0, r);
		nvgLineTo(vg, t.x - half, y0);
		nvgStrokeWidth(vg, 0.8f);
		nvgStrokeColor(vg, st::line());
		nvgStroke(vg);
		if (on) {
			nvgFontBlur(vg, 3.f);
			st::text(vg, t, "MENU", size, nvgRGBAf(0.36f, 0.94f, 0.42f, 0.8f), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
			nvgFontBlur(vg, 0.f);
		}
		st::text(vg, t, "MENU", size, on ? nvgRGB(0x5d, 0xf0, 0x6a) : nvgRGB(0x1f, 0x4a, 0x27), st::FONT_LABEL,
		         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
	}
};

// SLOT row (mm): the menu buttons' offsets from the square's middle; how far MODE's legends reach right of the lever
const float kMenuDx = 4.4f, kMenuDy = 4.1f, kModeLegendSpan = 18.5f;

// Back layout (mm): two blocks of eight slots, MIX on the right
const float kBlockA = 27.f, kBlockB = 71.f, kGateY = 10.f, kVoctY = 20.5f, kOutY = 32.f, kMixX = 109.f, kRecX = 112.f;
inline float colX(int i) { return 17.f + i * 10.4f; }

struct SmplWidget : JXMPModuleWidget {
	bool loadHeld = false;
	SmplDisplay* display = nullptr;

	SmplWidget(Smpl* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-SMPL", kColour, 24);
		styled();
		const float w = 121.92f;

		// Layout. Across: five equal columns, every row on the same ones. Down: each band has the same air under its
		// heading and above the next one; every control sits the same distance under its name, measured to the
		// control's own top (scale tick, lever tip), so names never look nearer to one control than another.
		const float col[5] = {14.f, 37.f, 60.f, 83.f, 106.f};
		const float kAir = 2.f;        // heading text to contents, contents to the next heading's text
		const float kHead = 1.2f;      // half the height of a heading's text
		const float kName = 1.1f;      // half the height of a control's name
		const float kNameGap = 1.2f;   // a name's bottom to its control's top
		const float kKnob = 7.5f;      // every small knob
		auto knobTop = [](float d) { return d / 2.f + 1.3f; };                      // top scale tick
		auto knobBottom = [](float d, bool numbers) {                               // end ticks, or their numbers
			return numbers ? (d / 2.f + 3.7f) * 0.866f + 1.1f : (d / 2.f + 1.3f) * 0.866f;
		};
		const float toggleTop = 5.6f;                                               // the lever up
		const float toggleBottom = 4.2f + 1.f;                                      // the lowest legend
		float hLoopBottom = 0.f, hTuneBottom = 0.f;
		// Where a band's names and controls' tops go, from its heading
		auto nameY = [&](float heading) { return heading + kHead + kAir + kName; };
		auto topY = [&](float heading) { return nameY(heading) + kName + kNameGap; };
		auto nextHeading = [&](float bottom) { return bottom + kAir + kHead; };

		const float hSample = 19.f;
		section(3.f, hSample, w - 6.f, 34.f, "SAMPLE");
		display = new SmplDisplay;
		display->module = module;
		display->box = math::Rect(mm2px(math::Vec(5.f, hSample + kHead + kAir)), mm2px(math::Vec(w - 10.f, 29.f)));
		addChild(display);

		// Bipolar scale without the "0" text: above a knob it would run into the knob's name (the centre tick stays)
		auto centred = [](float range) {
			std::vector<st::Mark> out = st::bipolar();
			for (st::Mark& m : out) {
				m.t *= range;
				if (m.label == "0")
					m.label.clear();
			}
			return out;
		};
		const std::vector<st::Mark> unit = st::marks(0.f, 1.f, 0.1f, 0, [](float) { return std::string(); });

		const float hSlot = nextHeading(hSample + kHead + kAir + 29.f);
		section(3.f, hSlot, w - 6.f, 20.f, "SLOT");
		{
			const float n = nameY(hSlot), t = topY(hSlot);
			const float chD = 8.5f;
			// Left to right: CH on the first column (like START and ROOT), the menu buttons, PLAY, MODE whose legends
			// end where LOOP END and PAN end; the menu square and PLAY share the space between with equal gaps.
			// Widths (mm) are what each one takes up: CH with its scale numbers, MODE with its legends.
			const float chX = col[0], chRight = chX + 8.f;
			const float modeRight = col[4] + 6.f, modeX = modeRight - kModeLegendSpan, modeLeft = modeX - 3.f;
			const float menuW = 2.f * kMenuDx + 6.1f, playW = 6.1f;
			const float gap = (modeLeft - chRight - menuW - playW) / 3.f;
			const float menuX = chRight + gap + menuW / 2.f, playX = chRight + 2.f * gap + menuW + playW / 2.f;
			label(math::Vec(chX, n), "CH", 6.f);
			knob(math::Vec(chX, t + knobTop(chD)), Smpl::CH_PARAM, chD, st::CAP_MAIN, 0.f, 15.f,
			     st::marks(0.f, 15.f, 1.f, 15, [](float v) { return string::f("%d", (int) v + 1); }));
			// The buttons sit on the row's middle line (CH and MODE), not under their names like the taller controls
			const float mid = t + knobTop(chD);
			// Menu: ▲ ▼ one under the other on the left, MENU over OK on the right; no names, the icons say it
			const IconButton::Icon icons[4] = {IconButton::UP, IconButton::DOWN, IconButton::MENU, IconButton::OK};
			const int ids[4] = {Smpl::UP_PARAM, Smpl::DOWN_PARAM, Smpl::MENU_PARAM, Smpl::OK_PARAM};
			for (int k = 0; k < 4; ++k) {
				IconButton* const b = createParamCentered<IconButton>(
				    mm2px(math::Vec(menuX + (k < 2 ? -kMenuDx : kMenuDx), mid + (k % 2 ? kMenuDy : -kMenuDy))), module, ids[k]);
				b->icon = icons[k];
				const int which = ids[k];
				b->action = [this, which]() { menuButton(which); };
				addParam(b);
			}
			// The frame round the menu buttons, MENU in its top line (the menu's light)
			{
				const float pad = 1.5f, rb = 3.05f;
				const math::Vec a(menuX - kMenuDx - rb - pad, mid - kMenuDy - rb - pad - 0.6f);
				const math::Vec b(menuX + kMenuDx + rb + pad, mid + kMenuDy + rb + pad);
				MenuFrame* const fr = new MenuFrame;
				fr->module = module;
				fr->box.pos = mm2px(a.minus(math::Vec(1.f, 2.f)));
				fr->box.size = mm2px(b.minus(a).plus(math::Vec(2.f, 3.f)));
				fr->frame = math::Rect(mm2px(math::Vec(1.f, 2.f)), mm2px(b.minus(a)));
				addChild(fr);
			}
			label(math::Vec(playX, n), "PLAY", 6.f);
			addParam(createParamCentered<VCVButton>(mm2px(math::Vec(playX, mid)), module, Smpl::PLAY_PARAM));
			label(math::Vec(modeX, n), "MODE", 6.f);
			toggle(math::Vec(modeX, t + toggleTop), Smpl::MODE_PARAM, 2, {});
			ledLegends(math::Vec(modeX, t + toggleTop), 2, {"ONE-SHOT", "GATE"});
			hLoopBottom = std::max(t + knobTop(chD) + knobBottom(chD, true), t + toggleTop + toggleBottom);
		}

		const float hLoop = nextHeading(hLoopBottom);
		section(3.f, hLoop, w - 6.f, 20.f, "SAMPLE · LOOP");
		{
			const float n = nameY(hLoop), t = topY(hLoop), y = t + knobTop(kKnob);
			label(math::Vec(col[0], n), "START", 6.f);
			knob(math::Vec(col[0], y), Smpl::START_PARAM, kKnob, st::CAP_WHITE, 0.f, 1.f, unit);
			label(math::Vec(col[1], n), "END", 6.f);
			knob(math::Vec(col[1], y), Smpl::END_PARAM, kKnob, st::CAP_WHITE, 0.f, 1.f, unit);
			label(math::Vec(col[2], n), "LOOP", 6.f);
			toggle(math::Vec(col[2], t + toggleTop), Smpl::LOOP_PARAM, 3, {});
			ledLegends(math::Vec(col[2], t + toggleTop), 3, {"OFF", "FWD", "PING"});
			label(math::Vec(col[3], n), "LOOP ST", 6.f);
			knob(math::Vec(col[3], y), Smpl::LOOP_START_PARAM, kKnob, st::CAP_WHITE, 0.f, 1.f, unit);
			label(math::Vec(col[4], n), "LOOP END", 6.f);
			knob(math::Vec(col[4], y), Smpl::LOOP_END_PARAM, kKnob, st::CAP_WHITE, 0.f, 1.f, unit);
			hTuneBottom = std::max(y + knobBottom(kKnob, false), t + toggleTop + toggleBottom);
		}

		// TUNE pair on the left, LEVEL pair on the right
		const float hTune = nextHeading(hTuneBottom);
		section(3.f, hTune, w - 6.f, 17.f, "TUNE · LEVEL");
		{
			const float n = nameY(hTune), y = topY(hTune) + knobTop(kKnob);
			label(math::Vec(col[0], n), "ROOT", 6.f);
			knob(math::Vec(col[0], y), Smpl::ROOT_PARAM, kKnob, st::CAP_MAIN, 12.f, 119.f, st::marks(12.f, 108.f, 12.f, 0, [](float) { return std::string(); }));
			label(math::Vec(col[1], n), "FINE", 6.f);
			knob(math::Vec(col[1], y), Smpl::FINE_PARAM, kKnob, st::CAP_WHITE, -100.f, 100.f, centred(100.f));
			label(math::Vec(col[3], n), "LEVEL", 6.f);
			knob(math::Vec(col[3], y), Smpl::LEVEL_PARAM, kKnob, st::CAP_WHITE, 0.f, 1.f, unit);
			label(math::Vec(col[4], n), "PAN", 6.f);
			knob(math::Vec(col[4], y), Smpl::PAN_PARAM, kKnob, st::CAP_WHITE, -1.f, 1.f, centred(1.f));
			INFO("JX-SMPL layout: TUNE bottom %.2f mm (footer at 119.5)", y + knobBottom(kKnob, true));
		}

		// Back: two blocks of eight slots (GATE, V/OCT, OUT), MIX on the right
		for (int i = 0; i < kSlots; ++i) {
			const float x = colX(i % 8), y0 = i < 8 ? kBlockA : kBlockB;
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(x, y0 + kGateY)), module, Smpl::GATE_INPUT + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(x, y0 + kVoctY)), module, Smpl::VOCT_INPUT + i));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(x, y0 + kOutY)), module, Smpl::OUT_OUTPUT + i));
		}
		// MIX on the rows of slots 1-8, REC (IN L, IN R, GATE) on the rows of slots 9-16
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(kMixX, kBlockA + kVoctY)), module, Smpl::MIX_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(kMixX, kBlockA + kOutY)), module, Smpl::MIX_R_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(kRecX, kBlockB + kGateY)), module, Smpl::REC_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(kRecX, kBlockB + kVoctY)), module, Smpl::REC_R_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(kRecX, kBlockB + kOutY)), module, Smpl::REC_GATE_INPUT));
	}


	void drawBackLegends(NVGcontext* const vg) override {
		namespace st = jxmp::style;
		const NVGcolor colour = decor->stripe;
		auto heading = [&](float x, float y, float w, const char* text) {
			st::text(vg, mm2px(math::Vec(x, y)), text, 7.f, colour, st::FONT_LABEL, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.8f);
			nvgBeginPath(vg);
			nvgMoveTo(vg, mm2px(x + std::strlen(text) * 1.45f + 1.5f), mm2px(y));
			nvgLineTo(vg, mm2px(x + w), mm2px(y));
			nvgStrokeWidth(vg, 0.8f);
			nvgStrokeColor(vg, st::line());
			nvgStroke(vg);
		};
		for (int b = 0; b < 2; ++b) {
			const float y0 = b ? kBlockB : kBlockA;
			heading(4.f, y0, 92.f, b ? "SLOTS 9-16" : "SLOTS 1-8");
			for (int i = 0; i < 8; ++i) {
				const float x = colX(i);
				st::text(vg, mm2px(math::Vec(x, y0 + 4.f)), string::f("%d", b * 8 + i + 1), 8.f, colour, st::FONT_LABEL,
				         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.3f);
				const math::Rect field(mm2px(math::Vec(x - 4.4f, y0 + kOutY - 4.9f)), mm2px(math::Vec(8.8f, 9.8f)));
				nvgBeginPath(vg);
				nvgRoundedRect(vg, RECT_ARGS(field), 2.f);
				nvgFillColor(vg, nvgRGB(0xc9, 0xcb, 0xcf));
				nvgFill(vg);
			}
			const char* const rows[3] = {"GATE", "V/OCT", "OUT"};
			const float ys[3] = {kGateY, kVoctY, kOutY};
			for (int r = 0; r < 3; ++r)
				st::text(vg, mm2px(math::Vec(3.f, y0 + ys[r])), rows[r], 6.5f, st::legend(), st::FONT_LABEL,
				         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.3f);
		}
		heading(99.f, kBlockA, 19.f, "MIX");
		const math::Rect field(mm2px(math::Vec(kMixX - 6.f, 39.3f)), mm2px(math::Vec(12.f, 25.4f)));
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(field), 2.f);
		nvgFillColor(vg, nvgRGB(0xc9, 0xcb, 0xcf));
		nvgFill(vg);
		st::text(vg, mm2px(math::Vec(kMixX, 41.6f)), "L", 7.f, nvgRGB(0x22, 0x23, 0x26), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.3f);
		st::text(vg, mm2px(math::Vec(kMixX, 53.25f)), "R", 7.f, nvgRGB(0x22, 0x23, 0x26), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.3f);
		heading(99.f, kBlockB, 19.f, "REC");
		const char* const rec[3] = {"IN L", "IN R", "GATE"};
		const float recY[3] = {kGateY, kVoctY, kOutY};
		for (int k = 0; k < 3; ++k)
			st::text(vg, mm2px(math::Vec(99.f, kBlockB + recY[k])), rec[k], 6.5f, st::legend(), st::FONT_LABEL,
			         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.3f);
	}

	Smpl* smpl() const { return dynamic_cast<Smpl*>(module); }

	void openDialog() {
		Smpl* const m = smpl();
		if (!m)
			return;
		const int slot = m->selected();
		const int64_t moduleId = m->id;
		async_dialog_filebrowser(false, nullptr, nullptr, "Load a sample (WAV, FLAC, MP3, OGG)", [slot, moduleId](char* path) {
			if (!path)
				return;
			const std::string p = path;
			std::free(path);
			// The module may be gone by now
			Smpl* const mm = dynamic_cast<Smpl*>(APP->engine->getModule(moduleId));
			if (mm)
				loadWithUndo(mm, slot, p);
		});
	}

	void step() override {
		JXMPModuleWidget::step();
		Smpl* const m = smpl();
		if (!m)
			return;
		m->syncEdit();
		slotUndo(m);
		m->menu.fadeNow = m->currentSettings(m->selected()).fadeout;
		if (m->rec.state == sm::Recorder::DONE)
			finishRecording(m);
		const bool held = m->params[Smpl::LOAD_PARAM].getValue() > 0.5f;
		if (held && !loadHeld)
			openDialog();
		loadHeld = held;
		// Free the replaced samples the audio thread no longer plays
		m->pruneRetired();
	}

	/** An edit knob's undo step names the slot it was turned on: Rack's own step would change whichever slot is
	 * selected when Undo comes. The newest step, when it is such a knob change, is swapped for a SlotChange. */
	history::Action* lastTop = nullptr;
	void slotUndo(Smpl* m) {
		history::State* const hs = APP->history;
		if (!hs || hs->actionIndex <= 0 || hs->actionIndex != (int) hs->actions.size())
			return;
		history::Action* const top = hs->actions[hs->actionIndex - 1];
		if (top == lastTop)
			return;
		lastTop = top;
		history::ParamChange* const pc = dynamic_cast<history::ParamChange*>(top);
		if (!pc || pc->moduleId != m->id || pc->paramId < Smpl::MODE_PARAM || pc->paramId > Smpl::PAN_PARAM)
			return;
		SlotChange* const sc = new SlotChange;
		sc->name = pc->name;
		sc->moduleId = m->id;
		sc->slot = m->selected();
		sc->after = m->currentSettings(sc->slot);
		sc->before = m->settingsWith(sc->slot, pc->paramId, pc->oldValue);
		hs->actions[hs->actionIndex - 1] = sc;
		delete pc;
		lastTop = sc;
	}

	/** A menu button was pressed */
	void menuButton(int which) {
		Smpl* const m = smpl();
		if (!m)
			return;
		if (display && display->browsing && !m->menu.open) {
			// The browser: ▲ ▼ move (each file plays), OK opens / loads, MENU closes
			switch (which) {
			case Smpl::MENU_PARAM: display->closeBrowser(); break;
			case Smpl::UP_PARAM: display->brMove(-1); break;
			case Smpl::DOWN_PARAM: display->brMove(1); break;
			case Smpl::OK_PARAM: display->brOk(); break;
			}
			return;
		}
		if (m->recording() && !m->menu.open) {
			// While recording: OK starts / stops (unless REC GATE does it), MENU throws it away
			if (which == Smpl::MENU_PARAM) {
				m->rec.cancel();
				m->message(m->recSlot, "RECORDING CANCELLED");
			}
			else if (which == Smpl::OK_PARAM && !m->inputs[Smpl::REC_GATE_INPUT].isConnected()) {
				if (!m->rec.start()) {
					m->rec.stop();
					finishRecording(m);
				}
			}
			return;
		}
		sm::MenuResult r;
		switch (which) {
		case Smpl::MENU_PARAM: m->menu.pressMenu(); break;
		case Smpl::UP_PARAM: m->menu.up(); break;
		case Smpl::DOWN_PARAM: m->menu.down(); break;
		case Smpl::OK_PARAM: r = m->menu.ok(); break;
		}
		if (r.action != sm::A_NONE)
			run(m, r);
	}

	/** The finished recording into its slot (with an undo step); the slot keeps its envelopes and fadeout */
	void finishRecording(Smpl* m) {
		auto d = std::make_shared<sm::SampleData>();
		const int i = m->recSlot;
		if (!m->rec.take(*d)) {
			m->message(i, "NOTHING RECORDED");
			return;
		}
		const sm::SlotSettings old = m->currentSettings(i);
		sm::SlotSettings s = sm::defaults(d->frames());
		s.volEnv = old.volEnv;
		s.panEnv = old.panEnv;
		s.fadeout = old.fadeout;
		m->params[Smpl::CH_PARAM].setValue((float) i);
		editSample(m, i, d, s, "record", string::f("REC %02d", i + 1));
		m->message(i, string::f("RECORDED %.2f S", d->frames() / d->rate));
		if (display)
			display->envView = 0;
	}

	/** Pushes an undo step for an edit of slot i's sample and installs the result (`name`: a new name, else the old) */
	void editSample(Smpl* m, int i, std::shared_ptr<const sm::SampleData> data, const sm::SlotSettings& set,
	                const char* what, const std::string& name = std::string()) {
		SampleSwap* const h = new SampleSwap;
		h->name = std::string("JX-SMPL ") + what;
		h->moduleId = m->id;
		h->slot = i;
		h->before = {m->ui[i].data, m->currentSettings(i), m->ui[i].name};
		h->after = {data, set, data ? (name.empty() ? m->ui[i].name : name) : std::string()};
		APP->history->push(h);
		m->replaceSample(i, data, set, h->after.name);
	}

	/** The envelope items: show, on / off, sustain and loop on the selected point, reset */
	void envAction(Smpl* m, int i, const sm::SlotSettings& set, const sm::MenuResult& r) {
		const int which = r.value > 0.5f ? 2 : 1;
		const char* const label = which == 2 ? "PAN ENV" : "VOLUME ENV";
		if (!display)
			return;
		if (display->envView != which)
			display->showEnv(which);
		sm::SlotSettings s = set;
		sm::Envelope& e = which == 2 ? s.panEnv : s.volEnv;
		const int p = display->selPt;
		const bool pointItem = r.action == sm::A_ENV_SUS || r.action == sm::A_ENV_LOOP_START || r.action == sm::A_ENV_LOOP_END;
		if (pointItem && (p < 0 || p >= e.n)) {
			m->message(i, "CLICK A POINT FIRST");
			return;
		}
		std::string msg;
		switch (r.action) {
		case sm::A_ENV_SHOW: return;
		case sm::A_ENV_ONOFF:
			e.on = !e.on;
			msg = std::string(label) + (e.on ? " ON" : " OFF");
			break;
		case sm::A_ENV_SUS:
			e.sus = p;
			msg = string::f("SUSTAIN AT POINT %d", p + 1);
			break;
		case sm::A_ENV_LOOP_START:
			e.ls = p;
			if (e.le < e.ls)
				e.le = e.ls;
			msg = string::f("LOOP START AT POINT %d", p + 1);
			break;
		case sm::A_ENV_LOOP_END:
			e.le = p;
			if (e.ls < 0 || e.ls > e.le)
				e.ls = e.le;
			msg = string::f("LOOP END AT POINT %d", p + 1);
			break;
		case sm::A_ENV_SUS_OFF:
			e.sus = -1;
			msg = "SUSTAIN OFF";
			break;
		case sm::A_ENV_LOOP_OFF:
			e.ls = e.le = -1;
			msg = "LOOP OFF";
			break;
		case sm::A_ENV_RESET: {
			const bool on = e.on;
			e = which == 2 ? sm::defaultPanEnv() : sm::defaultVolEnv();
			e.on = on;
			display->selPt = -1;
			msg = std::string(label) + " RESET";
			break;
		}
		default: return;
		}
		changeSlot(m, i, set, s, "JX-SMPL envelope");
		m->message(i, msg);
	}

	/** Runs a menu item on the selected slot */
	void run(Smpl* m, const sm::MenuResult& r) {
		const int i = m->selected();
		const std::shared_ptr<const sm::SampleData> d = m->ui[i].data;
		const sm::SlotSettings set = m->currentSettings(i);
		const bool needsSample = r.action != sm::A_LOAD && r.action != sm::A_RECORD && r.action != sm::A_BROWSE;
		if (needsSample && !d) {
			m->message(i, "SLOT IS EMPTY");
			return;
		}
		switch (r.action) {
		case sm::A_CROP: {
			sm::SlotSettings s = set;
			auto c = std::make_shared<sm::SampleData>(sm::cropped(*d, s));
			editSample(m, i, c, s, "crop");
			m->message(i, string::f("CROPPED TO %.2f S", c->frames() / c->rate));
			break;
		}
		case sm::A_NORMALIZE: {
			auto c = std::make_shared<sm::SampleData>(*d);
			const float g = sm::normalize(*c);
			if (g <= 0.f) {
				m->message(i, "SAMPLE IS SILENT");
				break;
			}
			editSample(m, i, c, set, "normalize");
			const float db = 20.f * std::log10(g);
			m->message(i, string::f("NORMALIZED %+.1f DB", std::fabs(db) < 0.05f ? 0.f : db));
			break;
		}
		case sm::A_REVERSE: {
			auto c = std::make_shared<sm::SampleData>(*d);
			sm::reverse(*c, set.start, set.end);
			editSample(m, i, c, set, "reverse");
			m->message(i, "REVERSED S-E");
			break;
		}
		case sm::A_XFADE: {
			auto c = std::make_shared<sm::SampleData>(*d);
			if (!sm::crossfadeLoop(*c, set, r.value)) {
				m->message(i, "NO ROOM BEFORE LOOP ST");
				break;
			}
			editSample(m, i, c, set, "loop crossfade");
			m->message(i, string::f("LOOP CROSSFADE %d MS", (int) r.value));
			break;
		}
		case sm::A_SNAP: {
			sm::SlotSettings s = set;
			sm::snapToZero(*d, s);
			SlotChange* const h = new SlotChange;
			h->name = "JX-SMPL snap to zero";
			h->moduleId = m->id;
			h->slot = i;
			h->before = set;
			h->after = s;
			APP->history->push(h);
			m->postSettings(i, s);
			m->refreshEdit = true;
			m->message(i, "POINTS ON ZERO CROSSINGS");
			break;
		}
		case sm::A_SEAM:
			if (display) {
				display->envView = 0;
				display->seam = true;
			}
			break;
		case sm::A_ZOOM_LOOP:
			if (display) {
				display->envView = 0;
				display->zoomToLoop(set);
			}
			break;
		case sm::A_WHOLE:
			if (display) {
				display->envView = 0;
				display->whole();
			}
			break;
		case sm::A_SHOW_WAVE:
			if (display)
				display->envView = 0;
			break;
		case sm::A_ENV_SHOW:
		case sm::A_ENV_ONOFF:
		case sm::A_ENV_SUS:
		case sm::A_ENV_LOOP_START:
		case sm::A_ENV_LOOP_END:
		case sm::A_ENV_SUS_OFF:
		case sm::A_ENV_LOOP_OFF:
		case sm::A_ENV_RESET:
			envAction(m, i, set, r);
			break;
		case sm::A_FADE: {
			sm::SlotSettings s = set;
			s.fadeout = r.value;
			changeSlot(m, i, set, s, "JX-SMPL fadeout");
			m->message(i, r.value > 0.f ? string::f("FADEOUT %g S", r.value) : "FADEOUT OFF");
			break;
		}
		case sm::A_LOAD: openDialog(); break;
		case sm::A_BROWSE:
			if (display) {
				display->envView = 0;
				display->openBrowser();
			}
			break;
		case sm::A_RECORD:
			if (!m->inputs[Smpl::REC_L_INPUT].isConnected() && !m->inputs[Smpl::REC_R_INPUT].isConnected()) {
				m->message(i, "PATCH REC IN L ON THE BACK");
				break;
			}
			m->recSlot = i;
			if (display) {
				display->recReset();
				display->envView = 0;
			}
			m->rec.arm(APP->engine->getSampleRate(), 60.f);
			break;
		case sm::A_DETECT: {
			// The note START..END sounds at goes into ROOT, the rest into FINE; no clear pitch: nothing changes
			const sm::PitchResult p = sm::detectRoot(*d, set.start, set.end);
			if (!p.found) {
				m->message(i, "NO CLEAR PITCH");
				break;
			}
			sm::SlotSettings s = set;
			s.root = p.root;
			s.fine = std::round(p.fine);
			SlotChange* const h = new SlotChange;
			h->name = "JX-SMPL detect root";
			h->moduleId = m->id;
			h->slot = i;
			h->before = set;
			h->after = s;
			APP->history->push(h);
			m->postSettings(i, s);
			m->refreshEdit = true;
			m->message(i, string::f("ROOT %s  FINE %+d", sm::noteName(s.root).c_str(), (int) s.fine));
			break;
		}
		case sm::A_CLEAR:
			editSample(m, i, nullptr, sm::SlotSettings(), "clear sample");
			m->message(i, string::f("SAMPLE %02d CLEARED", i + 1));
			break;
		default: break;
		}
	}

	void onPathDrop(const PathDropEvent& e) override {
		Smpl* const m = smpl();
		if (!m || e.paths.empty())
			return JXMPModuleWidget::onPathDrop(e);
		loadWithUndo(m, m->selected(), e.paths[0]);
		e.consume(this);
	}

	void appendContextMenu(ui::Menu* menu) override {
		JXMPModuleWidget::appendContextMenu(menu);
		Smpl* const m = smpl();
		if (!m)
			return;
		const int slot = m->selected();
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Slot %d", slot + 1)));
		menu->addChild(createMenuItem("Load…", "", [this]() { openDialog(); }));
		menu->addChild(createMenuItem("Clear slot", "", [m, slot]() { m->clearSlot(slot); }, !m->ui[slot].data));
	}
};

} // namespace


Model* modelJXMPSampler = createModel<Smpl, SmplWidget>("Sampler");

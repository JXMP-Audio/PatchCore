/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL: 16 sample slots in the spirit of the FastTracker 2 sample editor. Every slot holds one WAV; GATE n plays
 * slot n, V/OCT n its note (without a cable the slot plays at its original pitch). The front edits the slot chosen
 * with CH: start / end, loop off / forward / ping-pong, root note, fine tune, level, pan. Samples are saved inside
 * the patch (the module's patch storage folder, which Cardinal packs into the .vcv and into the DAW project).
 */

#include "JXMPWidget.hpp"
#include "dsp/Sampler.hpp"
#include "dsp/SampleFile.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace {

namespace sm = jxmp::sampler;

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
		NUM_PARAMS
	};
	enum InputIds { ENUMS(GATE_INPUT, kSlots), ENUMS(VOCT_INPUT, kSlots), NUM_INPUTS };
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
		double position = -1.0;  // for the display
	};
	AudioSlot audio[kSlots];

	/** A new sample (or new settings) waiting for the audio thread */
	struct Pending {
		bool has = false;
		std::shared_ptr<const sm::SampleData> data;
		sm::SlotSettings set;
	};
	std::mutex pendingMutex;
	Pending pending[kSlots];
	std::atomic<bool> anyPending{false};
	/** Set by the audio thread when a loop point turns but cannot do what was asked; the UI says why:
	 * 1 = the loop does not apply (ONE-SHOT or LOOP OFF), 2 = the loop stays between START and END */
	std::atomic<int> loopHint{0};
	/** Counts process() calls: when it stands still the engine is paused (the display then hides the playhead) */
	std::atomic<uint32_t> ticks{0};

	/** UI side of every slot (the UI thread only) */
	struct UiSlot {
		std::shared_ptr<const sm::SampleData> data;
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

	// Edit knobs ↔ selected slot
	int lastSel = -1;
	bool refreshEdit = true;
	float lastWritten[kEdit] = {};

	Smpl() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CH_PARAM, 0.f, kSlots - 1, 0.f, "Slot", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configButton(LOAD_PARAM, "Load a WAV into the selected slot");
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
			if (i == selected())
				refreshEdit = true;
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

	/** Moves turned edit knobs into the selected slot, the slot's values onto the knobs when CH changes */
	void syncEdit() {
		const int sel = selected();
		AudioSlot& a = audio[sel];
		const size_t frames = a.live ? a.live->frames() : 0;
		if (sel != lastSel || refreshEdit) {
			writeEdit(a.set, frames);
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
			auto frame = [&](float t) { return (size_t) std::lround(math::clamp(t, 0.f, 1.f) * frames); };
			switch (MODE_PARAM + k) {
			case MODE_PARAM: a.set.mode = modeOf(v); break;
			case START_PARAM: a.set.start = frame(v); moved = sm::P_START; break;
			case END_PARAM: a.set.end = frame(v); moved = sm::P_END; break;
			case LOOP_PARAM: a.set.loop = loopOf(v); break;
			case LOOP_START_PARAM: a.set.loopStart = frame(v); moved = sm::P_LOOP_START; break;
			case LOOP_END_PARAM: a.set.loopEnd = frame(v); moved = sm::P_LOOP_END; break;
			case ROOT_PARAM: a.set.root = (int) std::round(v); break;
			case FINE_PARAM: a.set.fine = v; break;
			case LEVEL_PARAM: a.set.level = v; break;
			case PAN_PARAM: a.set.pan = v; break;
			}
		}
		if (!changed)
			return;
		const bool loopMoved = moved == sm::P_LOOP_START || moved == sm::P_LOOP_END;
		if (loopMoved && !(a.set.mode == sm::GATE && a.set.loop != sm::LOOP_OFF))
			loopHint.store(1, std::memory_order_relaxed);
		if (frames) {
			const sm::SlotSettings asked = a.set;
			sm::constrain(a.set, frames, moved < 0 ? sm::P_START : moved);
			// Only points that constrain() moved go back onto their knobs: rewriting every knob with its value rounded
			// to a frame would undo small drags on short samples
			const float n = (float) frames;
			const size_t was[4] = {asked.start, asked.end, asked.loopStart, asked.loopEnd};
			const size_t now[4] = {a.set.start, a.set.end, a.set.loopStart, a.set.loopEnd};
			const int ids[4] = {START_PARAM, END_PARAM, LOOP_START_PARAM, LOOP_END_PARAM};
			for (int k = 0; k < 4; ++k)
				if (was[k] != now[k])
					params[ids[k]].setValue(now[k] / n);
			// A loop point pushed against START / END: it cannot go further that way
			if (loopMoved && was[moved] != now[moved])
				loopHint.store(2, std::memory_order_relaxed);
		}
		for (int k = 0; k < kEdit; ++k)
			lastWritten[k] = params[MODE_PARAM + k].getValue();
	}

	void process(const ProcessArgs& args) override {
		ticks.fetch_add(1, std::memory_order_relaxed);
		if (anyPending.load(std::memory_order_acquire))
			takePending();
		syncEdit();
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
				mix.add(fl * 5.f, fr * 5.f, st, a.fadeSet.level, a.fadeSet.pan, ownOut);
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
				mix.add(vl * 5.f, vr * 5.f, st, a.set.level, a.set.pan, ownOut);
				a.position = a.voice.position();
			}
			else {
				a.position = -1.0;
			}
			outputs[OUT_OUTPUT + i].setVoltage(out);
			lights[SLOT_LIGHT + i].setBrightness(a.voice.active() ? 1.f : 0.f);
		}
		outputs[MIX_L_OUTPUT].setVoltage(sm::softLimit10(mix.L));
		outputs[MIX_R_OUTPUT].setVoltage(sm::softLimit10(mix.R));
	}

	// ---- UI thread ----

	void post(int i, std::shared_ptr<const sm::SampleData> data, const sm::SlotSettings& set) {
		std::lock_guard<std::mutex> lock(pendingMutex);
		pending[i].has = true;
		pending[i].data = data;
		pending[i].set = set;
		anyPending.store(true, std::memory_order_release);
	}

	void message(int i, const std::string& text) {
		ui[i].message = text;
		ui[i].messageTime = system::getTime();
	}

	std::string storagePath(int i) {
		return system::join(createPatchStorageDirectory(), string::f("slot%02d.wav", i + 1));
	}

	/** Min / max of the sample in 256 parts for the display */
	static void overview(const sm::SampleData& d, std::vector<float>& lo, std::vector<float>& hi) {
		const size_t parts = 256, n = d.frames();
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

	/** Installs decoded bytes into slot i. `set` = settings to apply (from the patch), or defaults when null. */
	bool install(int i, const std::vector<uint8_t>& bytes, const std::string& name, const sm::SlotSettings* set) {
		auto data = std::make_shared<sm::SampleData>();
		std::string error;
		bool cut = false;
		if (!sm::decodeWav(bytes.data(), bytes.size(), *data, error, cut)) {
			message(i, error);
			return false;
		}
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
		post(i, data, s);
		message(i, cut ? "CUT TO 60 S" : "");
		return true;
	}

	/** Loads a file from the computer into slot i and keeps a copy in the patch */
	void loadFile(int i, const std::string& path) {
		std::vector<uint8_t> bytes;
		if (!system::isFile(path)) {
			message(i, "NOT A FILE");
			return;
		}
		try {
			bytes = system::readFile(path);
		}
		catch (std::exception& e) {
			message(i, "CANNOT READ FILE");
			return;
		}
		std::string name = string::uppercase(system::getStem(path));
		if (name.size() > 24)
			name = name.substr(0, 24);
		if (!install(i, bytes, name, nullptr))
			return;
		try {
			system::writeFile(storagePath(i), bytes);
		}
		catch (std::exception& e) {
			WARN("JX-SMPL: cannot save slot %d into the patch: %s", i + 1, e.what());
		}
	}

	void clearSlot(int i) {
		if (ui[i].data)
			retired.push_back(ui[i].data);
		ui[i] = UiSlot();
		post(i, nullptr, sm::SlotSettings());
		// The file stays until the patch is saved, so Undo of Initialize can bring the sample back
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

	/** The settings the audio thread uses now (pending ones first) */
	sm::SlotSettings currentSettings(int i) {
		std::lock_guard<std::mutex> lock(pendingMutex);
		return pending[i].has ? pending[i].set : audio[i].set;
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
			hasSaved[i] = true;
			// A preset or undo on a module that is already running: apply the settings at once; a slot emptied by
			// Initialize or Clear (file still in the storage until a save) gets its sample back
			if (added && ui[i].data) {
				sm::SlotSettings c = t;
				sm::constrain(c, ui[i].data->frames(), sm::P_START);
				post(i, ui[i].data, c);
			}
			else if (added) {
				installFromStorage(i, &t);
			}
		}
		refreshEdit = true;
	}
};

/** The VFD of JX-SMPL: the selected slot's name, root note, mode and loop; its waveform with START / END handles below
 * and the loop flags above (drag them); a strip of the 16 slots (click one to select it). Drop a WAV here to load it. */
struct SmplDisplay : widget::OpaqueWidget {
	Smpl* module = nullptr;
	enum Handle { H_NONE = -1, H_START, H_END, H_LOOP_START, H_LOOP_END };
	int dragging = H_NONE;
	uint32_t lastTicks = 0;
	double lastTickTime = 0.0;
	float dragX = 0.f, oldValue = 0.f;

	math::Rect waveRect() const {
		return math::Rect(mm2px(math::Vec(3.f, 10.5f)), math::Vec(box.size.x - mm2px(6.f), box.size.y - mm2px(19.5f)));
	}
	math::Rect stripBox(int i) const {
		const float x0 = mm2px(4.f), w = (box.size.x - 2 * x0) / kSlots;
		return math::Rect(math::Vec(x0 + i * w + 1.f, box.size.y - mm2px(4.2f)), math::Vec(w - 2.f, mm2px(2.4f)));
	}

	int paramOf(int h) const {
		static const int ids[4] = {Smpl::START_PARAM, Smpl::END_PARAM, Smpl::LOOP_START_PARAM, Smpl::LOOP_END_PARAM};
		return ids[h];
	}

	void draw(const DrawArgs& args) override {
		namespace st = jxmp::style;
		NVGcontext* const vg = args.vg;
		const NVGcolor lit = st::vfdTint(kColour);
		auto dim = [&](float a) { return nvgRGBAf(lit.r, lit.g, lit.b, a); };
		st::vfdWindow(vg, box.size, lit);
		const int sel = module ? module->selected() : 6;
		const Smpl::UiSlot* u = module ? &module->ui[sel] : nullptr;
		const bool has = u && u->data;
		const sm::SlotSettings set = has ? module->currentSettings(sel) : sm::SlotSettings();
		const float line1 = mm2px(4.f), line2 = mm2px(8.f);

		st::vfdText(vg, math::Vec(mm2px(3.f), line1), string::f("%02d", sel + 1), 13.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		const std::string name = u ? (has ? u->name : (u->name.empty() ? "EMPTY" : u->name)) : "GUITAR E2";
		st::vfdText(vg, math::Vec(mm2px(11.f), line1), name, 10.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		if (has || !module)
			st::vfdText(vg, math::Vec(box.size.x - mm2px(3.f), line1),
			            sm::noteName(module ? set.root : 40) + string::f("  %+03d", module ? (int) std::round(set.fine) : 0), 10.f,
			            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, lit);
		// Line 2: a fresh message, else mode and loop on the left, rate and length on the right
		const bool fresh = u && !u->message.empty() && system::getTime() - u->messageTime < 4.0;
		if (fresh)
			st::vfdText(vg, math::Vec(mm2px(3.f), line2), u->message, 7.5f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit);
		else if (has) {
			std::string mode = set.mode == sm::ONE_SHOT ? "ONE-SHOT" : "GATE";
			if (set.mode == sm::GATE)
				mode += set.loop == sm::LOOP_OFF ? "  LOOP OFF" : set.loop == sm::LOOP_FWD ? "  LOOP FWD" : "  LOOP PING";
			st::vfdLegend(vg, math::Vec(mm2px(3.f), line2), mode, 7.5f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.75f));
		}
		if (has)
			st::vfdLegend(vg, math::Vec(box.size.x - mm2px(3.f), line2),
			              string::f("%.1f kHz  %.2f s", u->data->rate / 1000.f, u->data->frames() / u->data->rate), 7.5f, true,
			              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.75f));
		else if (module && !fresh)
			st::vfdLegend(vg, math::Vec(mm2px(3.f), line2), "LOAD OR DROP A WAV", 7.5f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.75f));

		// Waveform
		const math::Rect wr = waveRect();
		const float mid = wr.pos.y + wr.size.y / 2;
		nvgBeginPath(vg);
		nvgMoveTo(vg, wr.pos.x, mid);
		nvgLineTo(vg, wr.pos.x + wr.size.x, mid);
		nvgStrokeWidth(vg, 0.6f);
		nvgStrokeColor(vg, dim(0.18f));
		nvgStroke(vg);
		if (has) {
			const float frames = (float) u->data->frames();
			auto xOf = [&](double f) { return wr.pos.x + (float) (f / frames) * wr.size.x; };
			// The loop is always shown (and can be dragged); dimmed when it does not apply (ONE-SHOT or LOOP OFF)
			const bool loopOn = set.mode == sm::GATE && set.loop != sm::LOOP_OFF;
			const float loopA = loopOn ? 1.f : 0.35f;
			{
				nvgBeginPath(vg);
				nvgRect(vg, xOf(set.loopStart), wr.pos.y, xOf(set.loopEnd) - xOf(set.loopStart), wr.size.y);
				nvgFillColor(vg, dim(0.12f * loopA));
				nvgFill(vg);
			}
			// Outside START..END dimmer
			const size_t parts = u->overMin.size();
			nvgBeginPath(vg);
			for (size_t p = 0; p < parts; ++p) {
				const float x = wr.pos.x + (p + 0.5f) * wr.size.x / parts;
				const float y0 = mid - u->overMax[p] * wr.size.y * 0.48f, y1 = mid - u->overMin[p] * wr.size.y * 0.48f;
				nvgMoveTo(vg, x, y0);
				nvgLineTo(vg, x, std::max(y1, y0 + 0.6f));
			}
			nvgStrokeWidth(vg, std::max(0.8f, wr.size.x / parts * 0.9f));
			nvgStrokeColor(vg, lit);
			nvgSave(vg);
			nvgScissor(vg, xOf(set.start), wr.pos.y - 2.f, xOf(set.end) - xOf(set.start), wr.size.y + 4.f);
			nvgStroke(vg);
			nvgRestore(vg);
			// What START and END cut off: only a faint trace of the wave under a dark veil, so the part that plays
			// stands out at a glance
			nvgStrokeColor(vg, dim(0.14f));
			const float cutL = xOf(set.start) - wr.pos.x, cutR = wr.pos.x + wr.size.x - xOf(set.end);
			for (int k = 0; k < 2; ++k) {
				const float x = k == 0 ? wr.pos.x : xOf(set.end), w = k == 0 ? cutL : cutR;
				if (w <= 0.f)
					continue;
				nvgSave(vg);
				nvgScissor(vg, x, wr.pos.y - 2.f, w, wr.size.y + 4.f);
				nvgStroke(vg);
				nvgRestore(vg);
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
				const float x = xOf(sePos[k]);
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
				// The letter inside the window beside its line, on the side that plays (clear of the slot numbers)
				st::vfdLegend(vg, math::Vec(x + (k == 0 ? 2.5f : -2.5f), hy - 4.f), se[k], 6.f, true,
				              (k == 0 ? NVG_ALIGN_LEFT : NVG_ALIGN_RIGHT) | NVG_ALIGN_MIDDLE, pale);
			}
			// Loop flags at the top
			{
				const size_t lp[2] = {set.loopStart, set.loopEnd};
				for (int k = 0; k < 2; ++k) {
					const float x = xOf(lp[k]);
					nvgBeginPath(vg);
					nvgMoveTo(vg, x, wr.pos.y);
					nvgLineTo(vg, x, wr.pos.y + wr.size.y);
					nvgStrokeWidth(vg, 0.7f);
					nvgStrokeColor(vg, dim(0.7f * loopA));
					nvgStroke(vg);
					// The flag hangs inside the wave area from its top edge, clear of the text lines above
					nvgBeginPath(vg);
					nvgMoveTo(vg, x, wr.pos.y);
					nvgLineTo(vg, x + (k ? -6.f : 6.f), wr.pos.y + 2.5f);
					nvgLineTo(vg, x, wr.pos.y + 5.f);
					nvgClosePath(vg);
					nvgFillColor(vg, dim(loopA));
					nvgFill(vg);
				}
			}
			// Playhead; hidden while the engine is paused (Space), so nothing but S marks where the sample begins
			const uint32_t ticks = module->ticks.load(std::memory_order_relaxed);
			const double now = system::getTime();
			if (ticks != lastTicks) {
				lastTicks = ticks;
				lastTickTime = now;
			}
			const bool running = now - lastTickTime < 0.1;
			const double pos = module->audio[sel].position;
			if (pos >= 0.0 && running) {
				const float x = xOf(pos);
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, wr.pos.y);
				nvgLineTo(vg, x, wr.pos.y + wr.size.y);
				nvgStrokeWidth(vg, 1.2f);
				nvgStrokeColor(vg, nvgRGBf(1.f, 1.f, 1.f));
				nvgStroke(vg);
			}
		}

		// Slot strip: filled = has a sample, bright = playing, white frame = selected
		for (int i = 0; i < kSlots; ++i) {
			const math::Rect b = stripBox(i);
			const bool loaded = module ? (bool) module->ui[i].data : (i < 9 || i == 12);
			const bool playing = module ? module->audio[i].voice.active() : (i == 0 || i == 6);
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

	/** Which handle is near x (and in its zone: S / E below the middle, loop flags above) */
	int handleAt(math::Vec p) const {
		if (!module || !module->ui[module->selected()].data)
			return H_NONE;
		const int sel = module->selected();
		const sm::SlotSettings set = module->currentSettings(sel);
		const math::Rect wr = waveRect();
		const float frames = (float) module->ui[sel].data->frames();
		auto xOf = [&](size_t f) { return wr.pos.x + f / frames * wr.size.x; };
		const bool upper = p.y < wr.pos.y + wr.size.y / 2;
		int best = H_NONE;
		float bestD = mm2px(2.5f);
		const size_t pts[4] = {set.start, set.end, set.loopStart, set.loopEnd};
		for (int h = 0; h < 4; ++h) {
			const bool isLoop = h >= H_LOOP_START;
			// Loop flags in the upper half, START / END in the lower half
			if (isLoop != upper)
				continue;
			const float d = std::fabs(p.x - xOf(pts[h]));
			if (d < bestD) {
				bestD = d;
				best = h;
			}
		}
		return best;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS || !module)
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
		dragging = handleAt(e.pos);
		if (dragging != H_NONE) {
			dragX = e.pos.x;
			oldValue = module->params[paramOf(dragging)].getValue();
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (dragging == H_NONE || !module)
			return;
		dragX += e.mouseDelta.x / getAbsoluteZoom();
		const math::Rect wr = waveRect();
		const float t = math::clamp((dragX - wr.pos.x) / wr.size.x, 0.f, 1.f);
		module->paramQuantities[paramOf(dragging)]->setValue(t);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (dragging != H_NONE && module) {
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
};

// Back layout (mm): two blocks of eight slots, MIX on the right
const float kBlockA = 27.f, kBlockB = 71.f, kGateY = 10.f, kVoctY = 20.5f, kOutY = 32.f, kMixX = 109.f;
inline float colX(int i) { return 17.f + i * 10.4f; }

struct SmplWidget : JXMPModuleWidget {
	bool loadHeld = false;

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
		SmplDisplay* const display = new SmplDisplay;
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
			label(math::Vec(col[0], n), "CH", 6.f);
			knob(math::Vec(col[0], t + knobTop(chD)), Smpl::CH_PARAM, chD, st::CAP_MAIN, 0.f, 15.f,
			     st::marks(0.f, 15.f, 1.f, 15, [](float v) { return string::f("%d", (int) v + 1); }));
			label(math::Vec(col[1], n), "LOAD", 6.f);
			// The buttons sit on the row's middle line (CH and MODE), not under their names like the taller controls
			const float mid = t + knobTop(chD);
			addParam(createParamCentered<VCVButton>(mm2px(math::Vec(col[1], mid)), module, Smpl::LOAD_PARAM));
			label(math::Vec(col[2], n), "PLAY", 6.f);
			addParam(createParamCentered<VCVButton>(mm2px(math::Vec(col[2], mid)), module, Smpl::PLAY_PARAM));
			label(math::Vec(col[3], n), "MODE", 6.f);
			toggle(math::Vec(col[3], t + toggleTop), Smpl::MODE_PARAM, 2, {});
			ledLegends(math::Vec(col[3], t + toggleTop), 2, {"ONE-SHOT", "GATE"});
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
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(kMixX, 50.f)), module, Smpl::MIX_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(kMixX, 66.f)), module, Smpl::MIX_R_OUTPUT));
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
		const math::Rect field(mm2px(math::Vec(kMixX - 6.f, 40.f)), mm2px(math::Vec(12.f, 32.f)));
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(field), 2.f);
		nvgFillColor(vg, nvgRGB(0xc9, 0xcb, 0xcf));
		nvgFill(vg);
		st::text(vg, mm2px(math::Vec(kMixX, 44.f)), "L", 7.f, nvgRGB(0x22, 0x23, 0x26), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.3f);
		st::text(vg, mm2px(math::Vec(kMixX, 60.f)), "R", 7.f, nvgRGB(0x22, 0x23, 0x26), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.3f);
		const char* const note[4] = {"SLOTS", "WITHOUT", "THEIR", "OWN OUT"};
		for (int k = 0; k < 4; ++k)
			st::text(vg, mm2px(math::Vec(kMixX, 77.f + k * 3.f)), note[k], 6.f, st::legendDim(), st::FONT_LABEL,
			         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.6f);
	}

	Smpl* smpl() const { return dynamic_cast<Smpl*>(module); }

	void openDialog() {
		Smpl* const m = smpl();
		if (!m)
			return;
		const int slot = m->selected();
		const int64_t moduleId = m->id;
		async_dialog_filebrowser(false, nullptr, nullptr, "Load a sample (WAV)", [slot, moduleId](char* path) {
			if (!path)
				return;
			const std::string p = path;
			std::free(path);
			// The module may be gone by now
			Smpl* const mm = dynamic_cast<Smpl*>(APP->engine->getModule(moduleId));
			if (mm)
				mm->loadFile(slot, p);
		});
	}

	void step() override {
		JXMPModuleWidget::step();
		Smpl* const m = smpl();
		if (!m)
			return;
		if (const int hint = m->loopHint.exchange(0, std::memory_order_relaxed))
			m->message(m->selected(), hint == 1 ? "LOOP: GATE + FWD/PING" : "LOOP STAYS BETWEEN S AND E");
		const bool held = m->params[Smpl::LOAD_PARAM].getValue() > 0.5f;
		if (held && !loadHeld)
			openDialog();
		loadHeld = held;
		// Free the replaced samples the audio thread no longer plays
		m->retired.erase(std::remove_if(m->retired.begin(), m->retired.end(),
		                                [](const std::shared_ptr<const sm::SampleData>& p) { return p.use_count() == 1; }),
		                 m->retired.end());
	}

	void onPathDrop(const PathDropEvent& e) override {
		Smpl* const m = smpl();
		if (!m || e.paths.empty())
			return JXMPModuleWidget::onPathDrop(e);
		m->loadFile(m->selected(), e.paths[0]);
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

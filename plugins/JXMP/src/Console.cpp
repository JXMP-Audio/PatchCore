/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-CONSOLE: the project's main mixer, 4/8/16/32 mono channels with a three-band EQ, two sends, pan, mute,
 * solo and a fader each, two stereo returns, a master fader and two VU meters. At most one per project.
 * The module front only has the meters and the master; the channels are controlled in the Mixer view
 * (double-click the module, or Mixer in the top bar). Design: research_docs/11-views-console-editor-design.md
 */

#include "JXMPWidget.hpp"
#include "dsp/Console.hpp"
#include <patchcore/Mixer.hpp>

#include <atomic>

namespace {

namespace cs = jxmp::console;
namespace st = jxmp::style;

const NVGcolor kConsoleColor = nvgRGB(0x5b, 0x6e, 0xe1);  // indigo
const int kMaxChannels = 32;

enum ChannelParam { CH_HIGH, CH_MID, CH_FREQ, CH_LOW, CH_SEND_A, CH_SEND_B, CH_PAN, CH_MUTE, CH_SOLO, CH_FADER, CH_PARAMS };
enum GlobalParam { G_MASTER, G_MASTER_MUTE, G_RETURN_A, G_RETURN_B, G_SEND_A_PRE, G_SEND_B_PRE, G_METER, G_PARAMS };
enum Output { OUT_MAIN_L, OUT_MAIN_R, OUT_SEND_A, OUT_SEND_B, NUM_OUTPUTS };


/** Fader: the value is the travel 0..1, shown in dB. */
struct FaderQuantity : engine::ParamQuantity {
	float getDisplayValue() override {
		return cs::faderDb(getValue());
	}
	void setDisplayValue(float db) override {
		setValue(cs::faderPos(db));
	}
	std::string getDisplayValueString() override {
		const float db = cs::faderDb(getValue());
		return std::isfinite(db) ? string::f("%.1f", db) : "-inf";
	}
};


struct Console : engine::Module {
	const int n;
	/** Read by the UI */
	cs::Vu channelVu[kMaxChannels];
	cs::Vu vuL, vuR;
	bool active = false;

	cs::Biquad eq[kMaxChannels][3];
	dsp::ClockDivider eqDivider;

	/** The project's console: the first one to run. Others stay silent. */
	static std::atomic<Console*>& owner() {
		static std::atomic<Console*> o(nullptr);
		return o;
	}

	int ch(int channel, int p) const { return channel * CH_PARAMS + p; }
	int global(int p) const { return n * CH_PARAMS + p; }
	int returnInput(int ret, int side) const { return n + ret * 2 + side; }
	int muteLight(int channel) const { return channel; }
	int soloLight(int channel) const { return n + channel; }
	int masterMuteLight() const { return 2 * n; }

	explicit Console(int channels) : n(channels) {
		config(n * CH_PARAMS + G_PARAMS, n + 4, NUM_OUTPUTS, 2 * n + 1);
		for (int i = 0; i < n; ++i) {
			const int c = i + 1;
			configParam(ch(i, CH_HIGH), -15.f, 15.f, 0.f, string::f("Ch %d high (8 kHz shelf)", c), " dB");
			configParam(ch(i, CH_MID), -15.f, 15.f, 0.f, string::f("Ch %d mid", c), " dB");
			configParam(ch(i, CH_FREQ), 0.f, 1.f, 0.5f, string::f("Ch %d mid frequency", c), " Hz", 25.f, 200.f);
			configParam(ch(i, CH_LOW), -15.f, 15.f, 0.f, string::f("Ch %d low (100 Hz shelf)", c), " dB");
			configParam(ch(i, CH_SEND_A), 0.f, 1.f, 0.f, string::f("Ch %d send A", c), "%", 0.f, 100.f);
			configParam(ch(i, CH_SEND_B), 0.f, 1.f, 0.f, string::f("Ch %d send B", c), "%", 0.f, 100.f);
			configParam(ch(i, CH_PAN), -1.f, 1.f, 0.f, string::f("Ch %d pan", c), "%", 0.f, 100.f);
			configSwitch(ch(i, CH_MUTE), 0.f, 1.f, 0.f, string::f("Ch %d mute", c), {"On", "Muted"});
			configSwitch(ch(i, CH_SOLO), 0.f, 1.f, 0.f, string::f("Ch %d solo", c), {"Off", "Solo"});
			configParam<FaderQuantity>(ch(i, CH_FADER), 0.f, 1.f, 0.75f, string::f("Ch %d fader", c), " dB");
			configInput(i, string::f("In %d", c))->description =
			    string::f("Channel %d input, mono (a polyphonic cable is summed). Controlled in the Mixer view.", c);
		}
		configParam<FaderQuantity>(global(G_MASTER), 0.f, 1.f, 0.75f, "Master fader", " dB");
		configSwitch(global(G_MASTER_MUTE), 0.f, 1.f, 0.f, "Master mute", {"On", "Muted"});
		configParam(global(G_RETURN_A), 0.f, 1.f, 1.f, "Return A level", "%", 0.f, 100.f);
		configParam(global(G_RETURN_B), 0.f, 1.f, 1.f, "Return B level", "%", 0.f, 100.f);
		configSwitch(global(G_SEND_A_PRE), 0.f, 1.f, 0.f, "Send A", {"Post-fader", "Pre-fader"});
		configSwitch(global(G_SEND_B_PRE), 0.f, 1.f, 0.f, "Send B", {"Post-fader", "Pre-fader"});
		std::vector<std::string> sources = {"Master"};
		for (int i = 0; i < n; ++i)
			sources.push_back(string::f("Channel %d", i + 1));
		configSwitch(global(G_METER), 0.f, (float) n, 0.f, "VU meter source", sources);
		for (int r = 0; r < 2; ++r) {
			const char ab = 'A' + r;
			configInput(returnInput(r, 0), string::f("Ret %c L", ab))->description =
			    string::f("Return %c left, e.g. from an effect fed by SEND %c. Mixed into the master.", ab, ab);
			configInput(returnInput(r, 1), string::f("Ret %c R", ab))->description =
			    string::f("Return %c right. Unpatched: the left return is used for both sides.", ab);
		}
		configOutput(OUT_MAIN_L, "Main L")->description = "Stereo master, left.";
		configOutput(OUT_MAIN_R, "Main R")->description = "Stereo master, right.";
		configOutput(OUT_SEND_A, "Send A")->description = "Mono effect send A: the channels × their SEND A (pre- or post-fader).";
		configOutput(OUT_SEND_B, "Send B")->description = "Mono effect send B: the channels × their SEND B (pre- or post-fader).";
		eqDivider.setDivision(32);

		Console* expected = nullptr;
		owner().compare_exchange_strong(expected, this);
	}

	~Console() {
		Console* expected = this;
		owner().compare_exchange_strong(expected, nullptr);
	}

	/** True when this is the project's console (takes over when the first one was removed). */
	bool claim() {
		Console* expected = nullptr;
		owner().compare_exchange_strong(expected, this);
		return owner().load() == this;
	}

	float param(int id) { return params[id].getValue(); }

	void process(const ProcessArgs& args) override {
		active = claim();
		if (!active) {
			for (int o = 0; o < NUM_OUTPUTS; ++o)
				outputs[o].setVoltage(0.f);
			return;
		}
		const float dt = args.sampleTime;
		const bool updateEq = eqDivider.process();
		const int meter = (int) std::round(param(global(G_METER)));
		const bool preA = param(global(G_SEND_A_PRE)) > 0.5f, preB = param(global(G_SEND_B_PRE)) > 0.5f;

		bool anySolo = false;
		for (int i = 0; i < n; ++i)
			anySolo = anySolo || param(ch(i, CH_SOLO)) > 0.5f;

		float left = 0.f, right = 0.f, sendA = 0.f, sendB = 0.f;
		for (int i = 0; i < n; ++i) {
			const bool mute = param(ch(i, CH_MUTE)) > 0.5f, solo = param(ch(i, CH_SOLO)) > 0.5f;
			lights[muteLight(i)].setBrightness(mute);
			lights[soloLight(i)].setBrightness(solo);
			if (updateEq) {
				eq[i][0].setLowShelf(args.sampleRate, 100.f, param(ch(i, CH_LOW)));
				eq[i][1].setPeak(args.sampleRate, cs::midFreq(param(ch(i, CH_FREQ))), 0.7f, param(ch(i, CH_MID)));
				eq[i][2].setHighShelf(args.sampleRate, 8000.f, param(ch(i, CH_HIGH)));
			}
			float post = 0.f, pl = 0.f, pr = 0.f;
			if (inputs[i].isConnected()) {
				float x = inputs[i].getVoltageSum();
				for (int b = 0; b < 3; ++b)
					x = eq[i][b].process(x);
				const float pre = cs::audible(mute, solo, anySolo) ? x : 0.f;
				post = pre * cs::faderGain(param(ch(i, CH_FADER)));
				sendA += param(ch(i, CH_SEND_A)) * (preA ? pre : post);
				sendB += param(ch(i, CH_SEND_B)) * (preB ? pre : post);
				cs::panGains(param(ch(i, CH_PAN)), &pl, &pr);
				left += post * pl;
				right += post * pr;
			}
			channelVu[i].process(post, dt);
			if (meter == i + 1) {
				vuL.process(post * pl, dt);
				vuR.process(post * pr, dt);
			}
		}

		for (int r = 0; r < 2; ++r) {
			const float l = inputs[returnInput(r, 0)].getVoltageSum();
			const float rr = inputs[returnInput(r, 1)].isConnected() ? inputs[returnInput(r, 1)].getVoltageSum() : l;
			const float g = param(global(r == 0 ? G_RETURN_A : G_RETURN_B));
			left += l * g;
			right += rr * g;
		}

		const bool masterMute = param(global(G_MASTER_MUTE)) > 0.5f;
		lights[masterMuteLight()].setBrightness(masterMute);
		const float mg = masterMute ? 0.f : cs::faderGain(param(global(G_MASTER)));
		const float outL = math::clamp(left * mg, -12.f, 12.f), outR = math::clamp(right * mg, -12.f, 12.f);
		outputs[OUT_MAIN_L].setVoltage(outL);
		outputs[OUT_MAIN_R].setVoltage(outR);
		outputs[OUT_SEND_A].setVoltage(math::clamp(sendA, -12.f, 12.f));
		outputs[OUT_SEND_B].setVoltage(math::clamp(sendB, -12.f, 12.f));
		if (meter == 0) {
			vuL.process(outL, dt);
			vuR.process(outR, dt);
		}
	}
};

template <int N>
struct ConsoleN : Console {
	ConsoleN() : Console(N) {}
};


// ---------------------------------------------------------------------------------------------------------
// Shared widgets

/** Analogue VU meter: backlit cream face, −20..+3 scale with a red zone, black needle. */
struct VuMeter : widget::TransparentWidget {
	Console* module = nullptr;
	bool right = false;

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const float w = box.size.x, h = box.size.y;

		// Bezel and backlit face
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, w, h, 3.f);
		nvgFillColor(vg, nvgRGB(0x0e, 0x0e, 0x10));
		nvgFill(vg);
		const float b = std::max(2.f, w * 0.03f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, b, b, w - 2 * b, h - 2 * b, 2.f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, b, 0, h - b, nvgRGB(0xf6, 0xea, 0xc6), nvgRGB(0xe0, 0xcb, 0x95)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, b, b, w - 2 * b, h - 2 * b, 2.f);
		nvgFillPaint(vg, nvgRadialGradient(vg, w / 2, h * 0.95f, h * 0.1f, h * 0.9f, nvgRGBAf(1.f, 0.86f, 0.45f, 0.35f), nvgRGBAf(1.f, 0.86f, 0.45f, 0.f)));
		nvgFill(vg);

		// Scale on an arc around the pivot below the face
		const math::Vec pivot(w / 2, h * 1.02f);
		const float radius = h * 0.78f;
		const float a0 = -0.88f, a1 = 0.88f;  // radians from straight up
		const NVGcolor ink = nvgRGB(0x2a, 0x24, 0x1c), red = nvgRGB(0xc8, 0x30, 0x28);
		auto at = [&](float t, float r) {
			const float a = a0 + (a1 - a0) * t - (float) M_PI / 2;
			return math::Vec(pivot.x + std::cos(a) * r, pivot.y + std::sin(a) * r);
		};
		// Red arc from 0 VU up
		nvgBeginPath(vg);
		nvgArc(vg, pivot.x, pivot.y, radius, a0 + (a1 - a0) * cs::vuNeedle(0.f) - (float) M_PI / 2, a1 - (float) M_PI / 2, NVG_CW);
		nvgStrokeWidth(vg, std::max(1.5f, h * 0.035f));
		nvgStrokeColor(vg, red);
		nvgStroke(vg);
		nvgBeginPath(vg);
		nvgArc(vg, pivot.x, pivot.y, radius, a0 - (float) M_PI / 2, a0 + (a1 - a0) * cs::vuNeedle(0.f) - (float) M_PI / 2, NVG_CW);
		nvgStrokeWidth(vg, 0.8f);
		nvgStrokeColor(vg, ink);
		nvgStroke(vg);
		const float marks[] = {-20.f, -10.f, -7.f, -5.f, -3.f, -2.f, -1.f, 0.f, 1.f, 2.f, 3.f};
		const float fs = std::max(5.f, h * 0.12f);
		for (const float m : marks) {
			const float t = cs::vuNeedle(m);
			const bool labelled = m == -20.f || m == -10.f || m == -5.f || m == -3.f || m == 0.f || m == 3.f;
			nvgBeginPath(vg);
			const math::Vec p0 = at(t, radius), p1 = at(t, radius + h * (labelled ? 0.1f : 0.06f));
			nvgMoveTo(vg, p0.x, p0.y);
			nvgLineTo(vg, p1.x, p1.y);
			nvgStrokeWidth(vg, labelled ? 1.f : 0.7f);
			nvgStrokeColor(vg, m > 0.f ? red : ink);
			nvgStroke(vg);
			if (labelled) {
				const std::string s = m > 0.f ? string::f("+%d", (int) m) : string::f("%d", (int) -m);
				st::text(vg, at(t, radius + h * 0.19f), s, fs, m > 0.f ? red : ink, st::FONT_NUMBER, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.f);
			}
		}
		st::text(vg, math::Vec(w / 2, h * 0.62f), "VU", std::max(6.f, h * 0.16f), ink, st::FONT_TITLE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.5f);
		st::text(vg, math::Vec(b + h * 0.12f, h - b - h * 0.1f), right ? "R" : "L", fs, ink, st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.f);

		// Needle, clipped to the face
		const float level = module ? (right ? module->vuR.vu() : module->vuL.vu()) : -60.f;
		nvgSave(vg);
		nvgScissor(vg, b, b, w - 2 * b, h - 2 * b);
		const math::Vec tip = at(cs::vuNeedle(level), radius + h * 0.08f), base = at(cs::vuNeedle(level), h * 0.22f);
		nvgBeginPath(vg);
		nvgMoveTo(vg, base.x + 0.8f, base.y + 1.2f);
		nvgLineTo(vg, tip.x + 0.8f, tip.y + 1.2f);
		nvgStrokeWidth(vg, 1.2f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.18f));
		nvgStroke(vg);
		nvgBeginPath(vg);
		nvgMoveTo(vg, base.x, base.y);
		nvgLineTo(vg, tip.x, tip.y);
		nvgStrokeWidth(vg, 1.1f);
		nvgStrokeColor(vg, nvgRGB(0x12, 0x12, 0x12));
		nvgStroke(vg);
		nvgRestore(vg);

		// Glass
		nvgBeginPath(vg);
		nvgRoundedRect(vg, b, b, w - 2 * b, (h - 2 * b) * 0.45f, 2.f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, b, 0, b + h * 0.45f, nvgRGBAf(1, 1, 1, 0.22f), nvgRGBAf(1, 1, 1, 0.f)));
		nvgFill(vg);
	}
};

/** Fader with a dB scale on the left of the slot and a grey cap with a white grip line. */
struct Fader : app::SliderKnob {
	float capHeight = 18.f;

	Fader() {
		minAngle = 0.f;
		maxAngle = 2.f * (float) M_PI;  // makes the drag range ratio 1, see Knob::onDragMove
	}

	float travel() const { return box.size.y - capHeight; }

	void onDragStart(const DragStartEvent& e) override {
		// The cap follows the mouse: the whole travel in as many screen pixels
		speed = 1.f / std::max(1e-6f, settings::knobLinearSensitivity * travel() * getAbsoluteZoom());
		app::SliderKnob::onDragStart(e);
	}
	void onDragEnd(const DragEndEvent& e) override {
		app::SliderKnob::onDragEnd(e);
		speed = 1.f;
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const float w = box.size.x, h = box.size.y;
		const float slotX = w * 0.62f;
		engine::ParamQuantity* const pq = getParamQuantity();
		const float pos = pq ? pq->getValue() : 0.75f;
		auto yOf = [&](float p) { return capHeight / 2 + (1.f - p) * travel(); };

		// Scale
		const float dbs[] = {6.f, 0.f, -5.f, -10.f, -20.f, -40.f};
		const float fs = math::clamp(w * 0.12f, 5.5f, 8.f);
		for (const float db : dbs) {
			const float y = yOf(cs::faderPos(db));
			nvgBeginPath(vg);
			nvgMoveTo(vg, slotX - w * 0.2f, y);
			nvgLineTo(vg, slotX - w * 0.1f, y);
			nvgStrokeWidth(vg, db == 0.f ? 1.2f : 0.8f);
			nvgStrokeColor(vg, db == 0.f ? st::legend() : st::legendDim());
			nvgStroke(vg);
			st::text(vg, math::Vec(slotX - w * 0.26f, y), db > 0.f ? string::f("+%d", (int) db) : string::f("%d", (int) db), fs,
			         db == 0.f ? st::legend() : st::legendDim(), st::FONT_NUMBER, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, 0.f);
		}
		st::text(vg, math::Vec(slotX - w * 0.26f, yOf(0.f)), "\xE2\x88\x9E", fs, st::legendDim(), st::FONT_NUMBER, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, 0.f);

		// Slot
		nvgBeginPath(vg);
		nvgRoundedRect(vg, slotX - 2.f, capHeight / 2 - 2.f, 4.f, travel() + 4.f, 2.f);
		nvgFillColor(vg, nvgRGB(0x05, 0x05, 0x06));
		nvgFill(vg);

		// Cap
		const float cy = yOf(pos), cw = std::min(w * 0.62f, 26.f);
		const math::Rect cap(slotX - cw / 2, cy - capHeight / 2, cw, capHeight);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, cap.pos.x + 1.f, cap.pos.y + 2.5f, cap.size.x, cap.size.y, 2.f);
		nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.45f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(cap), 2.f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, cap.pos.y, 0, cap.pos.y + cap.size.y, nvgRGB(0x9a, 0x9d, 0xa3), nvgRGB(0x4a, 0x4c, 0x51)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.7f);
		nvgStrokeColor(vg, nvgRGB(0x16, 0x16, 0x18));
		nvgStroke(vg);
		for (const float dy : {-capHeight * 0.3f, capHeight * 0.3f}) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, cap.pos.x + 2.f, cy + dy);
			nvgLineTo(vg, cap.pos.x + cap.size.x - 2.f, cy + dy);
			nvgStrokeWidth(vg, 0.6f);
			nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.35f));
			nvgStroke(vg);
		}
		nvgBeginPath(vg);
		nvgMoveTo(vg, cap.pos.x + 1.5f, cy);
		nvgLineTo(vg, cap.pos.x + cap.size.x - 1.5f, cy);
		nvgStrokeWidth(vg, 1.4f);
		nvgStrokeColor(vg, nvgRGB(0xf2, 0xf2, 0xf2));
		nvgStroke(vg);
	}
};

/** Vertical LED bar of a channel level (VU ballistics): green, yellow from −3, red from 0. */
struct LedMeter : widget::TransparentWidget {
	Console* module = nullptr;
	int channel = 0;

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const int leds = 16;
		const float level = module ? module->channelVu[channel].vu() : -60.f;
		const float gap = 1.5f, lh = (box.size.y - gap * (leds - 1)) / leds;
		for (int i = 0; i < leds; ++i) {
			const float db = -30.f + 33.f * (i + 1) / leds;  // −28 .. +3
			const bool on = level >= db - 33.f / leds;
			NVGcolor c = db > 0.f ? nvgRGB(0xe8, 0x3a, 0x30) : db > -3.f ? nvgRGB(0xf0, 0xc0, 0x30) : nvgRGB(0x3c, 0xd0, 0x6e);
			if (!on)
				c = color::mult(c, 0.18f);
			nvgBeginPath(vg);
			nvgRect(vg, 0, box.size.y - (i + 1) * lh - i * gap, box.size.x, lh);
			nvgFillColor(vg, c);
			nvgFill(vg);
		}
	}
};

/** Text of the VU meter source, on a small dark window. */
struct SourceReadout : widget::TransparentWidget {
	Console* module = nullptr;
	float fontSize = 9.f;

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(vg, nvgRGB(0x0d, 0x0e, 0x10));
		nvgFill(vg);
		std::string s = "MASTER";
		if (module) {
			const int m = (int) std::round(module->params[module->global(G_METER)].getValue());
			if (m > 0)
				s = string::f("CH %02d", m);
		}
		st::text(vg, box.size.div(2), s, fontSize, nvgRGB(0xff, 0xc0, 0x50), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 1.f);
	}
};

st::Knob* makeKnob(Console* module, int paramId, math::Vec centerPx, float diameterMm, NVGcolor cap) {
	st::Knob* const k = createParam<st::Knob>(math::Vec(), module, paramId);
	k->setDiameter(diameterMm);
	k->cap = cap;
	k->box.pos = centerPx.minus(k->box.size.div(2));
	return k;
}

const NVGcolor kCapWhite = nvgRGB(0xe8, 0xe6, 0xe0), kCapGrey = nvgRGB(0x7c, 0x80, 0x88), kCapBlack = nvgRGB(0x26, 0x27, 0x2a);


// ---------------------------------------------------------------------------------------------------------
// Mixer view content

const float kStripW = 72.f, kMasterW = 300.f, kDesignH = 640.f;

/** Background and legends of one channel strip, its controls are children. */
struct Strip : widget::Widget {
	int channel = 0;

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const float w = box.size.x, cx = w / 2;
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, w, box.size.y);
		nvgFillColor(vg, channel % 2 ? st::panel() : color::plus(st::panel(), nvgRGBf(0.012f, 0.012f, 0.012f)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgMoveTo(vg, w - 0.5f, 0);
		nvgLineTo(vg, w - 0.5f, box.size.y);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGB(0x15, 0x16, 0x18));
		nvgStroke(vg);

		st::text(vg, math::Vec(cx, 15.f), string::f("%d", channel + 1), 15.f, st::legend(), st::FONT_TITLE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.5f);
		nvgBeginPath(vg);
		nvgRect(vg, 6.f, 28.f, w - 12.f, 2.5f);
		nvgFillColor(vg, kConsoleColor);
		nvgFill(vg);

		const struct { float y; const char* text; } labels[] = {
		    {48.f, "HIGH"}, {90.f, "MID"}, {132.f, "FREQ"}, {172.f, "LOW"}, {234.f, "SEND A"}, {276.f, "SEND B"},
		    {336.f, "PAN"}, {376.f, "M      S"},
		};
		for (const auto& l : labels)
			st::text(vg, math::Vec(cx, l.y), l.text, 8.f, st::legend(), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.6f);
		for (const float y : {38.f, 222.f, 322.f, 402.f}) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, 6.f, y);
			nvgLineTo(vg, w - 6.f, y);
			nvgStrokeWidth(vg, 0.8f);
			nvgStrokeColor(vg, st::line());
			nvgStroke(vg);
		}
		Widget::draw(args);
	}
};

/** Background and legends of the master section. */
struct MasterSection : widget::Widget {
	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const float w = box.size.x;
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, w, box.size.y);
		nvgFillColor(vg, st::panelDark());
		nvgFill(vg);
		st::text(vg, math::Vec(w / 2, 15.f), "MASTER", 15.f, st::legend(), st::FONT_TITLE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
		nvgBeginPath(vg);
		nvgRect(vg, 10.f, 28.f, w - 20.f, 2.5f);
		nvgFillColor(vg, kConsoleColor);
		nvgFill(vg);
		const struct { float x, y; const char* text; } labels[] = {
		    {55.f, 150.f, "METER"}, {55.f, 228.f, "RET A"}, {125.f, 228.f, "RET B"}, {55.f, 300.f, "SEND A"},
		    {125.f, 300.f, "SEND B"}, {55.f, 372.f, "PRE"}, {125.f, 372.f, "PRE"}, {55.f, 412.f, "POST"}, {125.f, 412.f, "POST"},
		    {90.f, 450.f, "MUTE"}, {236.f, 212.f, "MASTER"},
		};
		for (const auto& l : labels)
			st::text(vg, math::Vec(l.x, l.y), l.text, 8.f, st::legend(), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.6f);
		for (const float y : {136.f, 212.f, 286.f, 436.f}) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, 12.f, y);
			nvgLineTo(vg, 170.f, y);
			nvgStrokeWidth(vg, 0.8f);
			nvgStrokeColor(vg, st::line());
			nvgStroke(vg);
		}
		st::text(vg, math::Vec(90.f, 600.f), "TAB / ESC: RACK", 7.5f, st::legendDim(), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 1.f);
		Widget::draw(args);
	}
};

struct ConsoleView : widget::Widget {
	Console* module;
	ui::ScrollWidget* scroll;
	widget::ZoomWidget* stripZoom;
	widget::ZoomWidget* masterZoom;

	explicit ConsoleView(Console* m) : module(m) {
		scroll = new ui::ScrollWidget;
		addChild(scroll);
		stripZoom = new widget::ZoomWidget;
		scroll->container->addChild(stripZoom);
		for (int i = 0; i < module->n; ++i)
			stripZoom->addChild(createStrip(i));
		masterZoom = new widget::ZoomWidget;
		masterZoom->addChild(createMaster());
		addChild(masterZoom);
	}

	Strip* createStrip(int i) {
		Strip* const s = new Strip;
		s->channel = i;
		s->box = math::Rect(i * kStripW, 0, kStripW, kDesignH);
		const float cx = kStripW / 2;
		s->addChild(makeKnob(module, module->ch(i, CH_HIGH), math::Vec(cx, 68.f), 8.f, kCapWhite));
		s->addChild(makeKnob(module, module->ch(i, CH_MID), math::Vec(cx, 110.f), 8.f, kCapWhite));
		s->addChild(makeKnob(module, module->ch(i, CH_FREQ), math::Vec(cx, 151.f), 6.5f, kCapGrey));
		s->addChild(makeKnob(module, module->ch(i, CH_LOW), math::Vec(cx, 194.f), 8.f, kCapWhite));
		s->addChild(makeKnob(module, module->ch(i, CH_SEND_A), math::Vec(cx, 254.f), 8.f, kCapWhite));
		s->addChild(makeKnob(module, module->ch(i, CH_SEND_B), math::Vec(cx, 296.f), 8.f, kCapWhite));
		s->addChild(makeKnob(module, module->ch(i, CH_PAN), math::Vec(cx, 356.f), 7.f, kCapGrey));
		s->addChild(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(math::Vec(cx - 15.f, 390.f), module,
		                                                                                  module->ch(i, CH_MUTE), module->muteLight(i)));
		s->addChild(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(math::Vec(cx + 15.f, 390.f), module,
		                                                                                     module->ch(i, CH_SOLO), module->soloLight(i)));
		Fader* const f = createParam<Fader>(math::Vec(4.f, 412.f), module, module->ch(i, CH_FADER));
		f->box.size = math::Vec(48.f, 214.f);
		s->addChild(f);
		LedMeter* const led = new LedMeter;
		led->module = module;
		led->channel = i;
		led->box = math::Rect(56.f, 418.f, 7.f, 202.f);
		s->addChild(led);
		return s;
	}

	MasterSection* createMaster() {
		MasterSection* const s = new MasterSection;
		s->box = math::Rect(0, 0, kMasterW, kDesignH);
		for (int r = 0; r < 2; ++r) {
			VuMeter* const vu = new VuMeter;
			vu->module = module;
			vu->right = r == 1;
			vu->box = math::Rect(12.f + r * 142.f, 40.f, 134.f, 88.f);
			s->addChild(vu);
		}
		s->addChild(makeKnob(module, module->global(G_METER), math::Vec(55.f, 175.f), 9.f, kCapBlack));
		SourceReadout* const ro = new SourceReadout;
		ro->module = module;
		ro->fontSize = 11.f;
		ro->box = math::Rect(92.f, 163.f, 78.f, 24.f);
		s->addChild(ro);
		s->addChild(makeKnob(module, module->global(G_RETURN_A), math::Vec(55.f, 254.f), 9.f, kCapWhite));
		s->addChild(makeKnob(module, module->global(G_RETURN_B), math::Vec(125.f, 254.f), 9.f, kCapWhite));
		for (int r = 0; r < 2; ++r) {
			st::Toggle* const t = createParam<st::Toggle>(math::Vec(), module, module->global(r == 0 ? G_SEND_A_PRE : G_SEND_B_PRE));
			t->box.pos = math::Vec(55.f + r * 70.f, 392.f).minus(t->box.size.div(2));
			s->addChild(t);
		}
		s->addChild(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(math::Vec(90.f, 474.f), module,
		                                                                                  module->global(G_MASTER_MUTE), module->masterMuteLight()));
		Fader* const f = createParam<Fader>(math::Vec(196.f, 226.f), module, module->global(G_MASTER));
		f->box.size = math::Vec(64.f, 390.f);
		f->capHeight = 22.f;
		s->addChild(f);
		return s;
	}

	void step() override {
		const float zoom = math::clamp((box.size.y - 16.f) / kDesignH, 0.55f, 1.6f);
		const float masterW = kMasterW * zoom;
		masterZoom->setZoom(zoom);
		masterZoom->box = math::Rect(box.size.x - masterW, 0, masterW, kDesignH * zoom);
		stripZoom->setZoom(zoom);
		stripZoom->box = math::Rect(0, 0, module->n * kStripW * zoom, kDesignH * zoom);
		scroll->box = math::Rect(0, 0, std::max(0.f, box.size.x - masterW), box.size.y);
		Widget::step();
	}
};


// ---------------------------------------------------------------------------------------------------------
// Module widget

struct InactiveNotice : widget::TransparentWidget {
	Console* module = nullptr;
	void draw(const DrawArgs& args) override {
		if (!module || Console::owner().load() == module || Console::owner().load() == nullptr)
			return;
		NVGcontext* const vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(vg, nvgRGBA(0xc8, 0x30, 0x28, 0xe0));
		nvgFill(vg);
		st::text(vg, box.size.div(2), "SECOND CONSOLE - INACTIVE", 8.f, color::WHITE, st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.6f);
	}
};

struct ConsoleWidget : JXMPModuleWidget, patchcore::MixerProvider {
	Console* console;

	ConsoleWidget(Console* const module, int channels) : console(module) {
		setup(module, "JX-CONSOLE", kConsoleColor, 18);
		styled();
		const float w = 91.44f;

		// METERS: the two VU meters
		section(4.f, 20.5f, w - 8.f, 30.f, string::f("METERS · %d CH", channels).c_str());
		for (int r = 0; r < 2; ++r) {
			VuMeter* const vu = new VuMeter;
			vu->module = module;
			vu->right = r == 1;
			vu->box = math::Rect(mm2px(math::Vec(6.f + r * 40.7f, 25.f)), mm2px(math::Vec(38.7f, 25.f)));
			addChild(vu);
		}

		// SOURCE: what the meters show
		section(4.f, 56.f, w - 8.f, 20.f, "SOURCE");
		label(math::Vec(16.f, 61.f), "METER", 7.f);
		knob(math::Vec(16.f, 70.5f), module ? module->global(G_METER) : 7 * CH_PARAMS + G_METER, 9.f, st::CAP_BLACK, 0.f, (float) channels,
		     {});
		SourceReadout* const ro = new SourceReadout;
		ro->module = module;
		ro->box = math::Rect(mm2px(math::Vec(28.f, 66.5f)), mm2px(math::Vec(24.f, 8.f)));
		addChild(ro);
		label(math::Vec(70.f, 66.f), "DOUBLE-CLICK", 6.f);
		label(math::Vec(70.f, 71.f), "OPENS THE MIXER", 6.f);

		// MASTER: mute and fader
		section(4.f, 82.f, w - 8.f, 36.f, "MASTER");
		label(math::Vec(16.f, 88.f), "MUTE", 7.f);
		if (module)
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(mm2px(math::Vec(16.f, 96.f)), module,
			                                                                               module->global(G_MASTER_MUTE), module->masterMuteLight()));
		Fader* const f = createParam<Fader>(mm2px(math::Vec(48.f, 84.5f)), module, module ? module->global(G_MASTER) : 0);
		f->box.size = mm2px(math::Vec(30.f, 33.5f));
		f->capHeight = mm2px(5.f);
		addParam(f);

		InactiveNotice* const notice = new InactiveNotice;
		notice->module = module;
		notice->box = math::Rect(mm2px(math::Vec(6.f, 34.f)), mm2px(math::Vec(w - 12.f, 7.f)));
		addChild(notice);

		// Back: channel inputs in rows of eight, then returns, sends and the main outputs
		for (int i = 0; i < channels; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(9.5f + (i % 8) * 10.35f, 30.f + (i / 8) * 15.f)), module, i));
		for (int j = 0; j < 4; ++j)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(9.5f + j * 10.35f, 98.f)), module, channels + j));
		for (int j = 0; j < 4; ++j)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(9.5f + (4 + j) * 10.35f, 98.f)), module, j));
	}

	widget::Widget* createMixerView() override {
		return new ConsoleView(console);
	}

	bool mixerActive() override {
		if (!console)
			return false;
		Console* const o = Console::owner().load();
		return o == console || o == nullptr;
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (mixerActive()) {
			patchcore::showMixer(true);
			e.consume(this);
		}
	}
};

template <int N>
struct ConsoleWidgetN : ConsoleWidget {
	ConsoleWidgetN(ConsoleN<N>* module) : ConsoleWidget(module, N) {}
};

} // namespace


Model* modelJXMPConsole4 = createModel<ConsoleN<4>, ConsoleWidgetN<4>>("Console4");
Model* modelJXMPConsole8 = createModel<ConsoleN<8>, ConsoleWidgetN<8>>("Console8");
Model* modelJXMPConsole16 = createModel<ConsoleN<16>, ConsoleWidgetN<16>>("Console16");
Model* modelJXMPConsole32 = createModel<ConsoleN<32>, ConsoleWidgetN<32>>("Console32");

// Only one console per project
static const bool consoleSingletons = [] {
	for (Model* const m : {modelJXMPConsole4, modelJXMPConsole8, modelJXMPConsole16, modelJXMPConsole32})
		patchcore::registerSingletonModel(m, "jxmp-console");
	return true;
}();

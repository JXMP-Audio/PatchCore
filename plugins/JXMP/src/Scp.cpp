/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SCP: two-channel oscilloscope with a round green CRT, styled after an old lab scope. Y–T with a trigger
 * (level, slope, source CH1 / CH2 / EXT; AUTO / NORM) or X–Y. It acquires as a digital scope (Hantek DSO) does:
 * the trigger point in the middle, a complete picture each time, NORM holds it. Square tube. Design: research_docs/14-jxmp-modules-set3-design.md.
 */

#include "JXMPWidget.hpp"
#include "dsp/Scope.hpp"


namespace {

namespace sc = jxmp::scope;

struct Scp : engine::Module {
	enum ParamIds {
		TIME_PARAM,
		VOLT1_PARAM,
		POS1_PARAM,
		VOLT2_PARAM,
		POS2_PARAM,
		TRIG_LEVEL_PARAM,
		TRIG_SLOPE_PARAM,
		TRIG_SOURCE_PARAM,
		MODE_PARAM,
		TRIG_MODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds { IN1_INPUT, IN2_INPUT, TRIG_INPUT, NUM_INPUTS };
	enum OutputIds { NUM_OUTPUTS };
	enum LightIds { TRIG_LIGHT, NUM_LIGHTS };

	sc::Trigger trigger;
	sc::Capture capture;
	float trigLight = 0.f;
	int lastTimeStep = -1;
	/** The level the trigger really uses (the knob, or the middle of the signal), for the display marker */
	float usedLevel = 0.f;

	Scp() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// Every control explains itself in its tooltip, for players who never used a scope
		configSwitch(TIME_PARAM, 0.f, sc::kTimeSteps - 1, 6.f, "Time / div",
		             {"0.1 ms", "0.2 ms", "0.5 ms", "1 ms", "2 ms", "5 ms", "10 ms", "20 ms", "50 ms", "0.1 s", "0.2 s", "0.5 s", "1 s"})
		    ->description = "How much time one square across is. Short for audio waves, long for slow LFOs and envelopes.";
		configSwitch(VOLT1_PARAM, 0.f, sc::kVoltSteps - 1, 4.f, "CH 1 volts / div", {"0.1 V", "0.2 V", "0.5 V", "1 V", "2 V", "5 V"})
		    ->description = "How many volts one square up is for the green trace. Smaller = the wave is drawn taller.";
		configParam(POS1_PARAM, -4.f, 4.f, 0.f, "CH 1 position", " div")->description = "Moves the green trace up or down.";
		configSwitch(VOLT2_PARAM, 0.f, sc::kVoltSteps - 1, 4.f, "CH 2 volts / div", {"0.1 V", "0.2 V", "0.5 V", "1 V", "2 V", "5 V"})
		    ->description = "How many volts one square up is for the amber trace. Smaller = the wave is drawn taller.";
		configParam(POS2_PARAM, -4.f, 4.f, 0.f, "CH 2 position", " div")->description = "Moves the amber trace up or down.";
		configParam(TRIG_LEVEL_PARAM, -10.f, 10.f, 0.f, "Trigger level", " V")
		    ->description = "The voltage the wave must cross to start a picture (the dashed line). Outside the wave nothing "
		                    "triggers: AUTO then shows the wave running, NORM keeps the last picture.";
		// Toggles: lever up = the highest value, so the default (top) choice is the highest value
		configSwitch(TRIG_SLOPE_PARAM, 0.f, 1.f, 1.f, "Trigger slope", {"Falling", "Rising"})
		    ->description = "Start the drawing when the wave crosses the level going up (RISE) or going down (FALL).";
		configSwitch(TRIG_SOURCE_PARAM, 0.f, 2.f, 2.f, "Trigger source", {"External", "CH 2", "CH 1"})
		    ->description = "Which signal starts the drawing: the green input, the amber input, or the TRIG input on the back.";
		configSwitch(MODE_PARAM, 0.f, 1.f, 1.f, "Display", {"X–Y (CH 1 across, CH 2 up)", "Y–T (time)"})
		    ->description = "Y-T draws the waves over time. X-Y draws CH 1 against CH 2 (Lissajous figures, stereo width).";
		configSwitch(TRIG_MODE_PARAM, 0.f, 1.f, 1.f, "Trigger mode", {"Normal: only on a trigger", "Auto: runs by itself without a trigger"})
		    ->description = "AUTO: always shows something, locked to the trigger when one comes. NORM: a new picture only on "
		                    "a trigger; without one the last picture stays. TRIG'D lights on every trigger.";
		configInput(IN1_INPUT, "In 1")->description = "Channel 1, the green trace (X in X–Y mode). Polyphonic: the first voice is drawn.";
		configInput(IN2_INPUT, "In 2")->description = "Channel 2, the amber trace (Y in X–Y mode).";
		configInput(TRIG_INPUT, "Trig")->description = "External trigger, used when the trigger source is EXT.";
		configLight(TRIG_LIGHT, "Triggered");
	}

	void process(const ProcessArgs& args) override {
		const float a = inputs[IN1_INPUT].getVoltage();
		const float b = inputs[IN2_INPUT].getVoltage();
		const int source = (int) std::round(params[TRIG_SOURCE_PARAM].getValue());  // 2 CH 1, 1 CH 2, 0 EXT
		const float t = source == 2 ? a : source == 1 ? b : inputs[TRIG_INPUT].getVoltage();
		const bool xy = params[MODE_PARAM].getValue() < 0.5f;
		const bool autoRun = params[TRIG_MODE_PARAM].getValue() > 0.5f;

		// As on a DSO: a level outside the signal simply never triggers (AUTO then runs untriggered)
		const float level = params[TRIG_LEVEL_PARAM].getValue();
		usedLevel = level;
		const bool fired = trigger.process(t, level, params[TRIG_SLOPE_PARAM].getValue() > 0.5f);
		if (fired && !xy)
			trigLight = 1.f;
		// A new time base starts a fresh sweep at once
		const int timeStep = (int) std::round(params[TIME_PARAM].getValue());
		if (timeStep != lastTimeStep) {
			lastTimeStep = timeStep;
			capture.restart();
		}
		capture.process(a, b, fired, args.sampleTime, sc::timeDiv(timeStep), xy, autoRun);
		trigLight = std::max(0.f, trigLight - args.sampleTime * 5.f);
		lights[TRIG_LIGHT].setBrightness(trigLight);
	}
};


/** The CRT, a square tube: bezel, phosphor screen with a graticule, the traces, a glass highlight. */
struct CrtScreen : widget::TransparentWidget {
	Scp* module = nullptr;

	float radius() const {
		return box.size.x / 2 - mm2px(2.5f);  // the screen inside a thin bezel
	}

	/** Screen point of (x, y) in divisions. It may lie off the round screen: trace() clips it. */
	math::Vec point(math::Vec c, float r, float xDiv, float yDiv) const {
		const float d = r / 4.f;  // 8 divisions across the diameter
		return c.plus(math::Vec(xDiv * d, -yDiv * d));
	}

	/** The trace; drawTraces() clips it to the screen (what leaves it is hidden behind the bezel). */
	void trace(NVGcontext* vg, math::Vec c, float r, const std::vector<math::Vec>& pts, NVGcolor col, float alpha) {
		if (pts.size() < 2)
			return;
		for (int pass = 0; pass < 2; ++pass) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, pts[0].x, pts[0].y);
			for (size_t i = 1; i < pts.size(); ++i)
				nvgLineTo(vg, pts[i].x, pts[i].y);
			nvgLineJoin(vg, NVG_ROUND);
			nvgLineCap(vg, NVG_ROUND);
			// A wide soft glow, then the bright core
			nvgStrokeWidth(vg, pass == 0 ? 4.5f : 1.4f);
			nvgStrokeColor(vg, nvgRGBAf(col.r, col.g, col.b, alpha * (pass == 0 ? 0.18f : 0.95f)));
			nvgStroke(vg);
		}
	}

	void drawTraces(NVGcontext* vg, math::Vec c, float r) {
		const NVGcolor green = nvgRGB(0x7c, 0xff, 0xa8), amber = nvgRGB(0xff, 0xbe, 0x55);
		if (!module) {
			// Browser preview: a sine on CH 1
			std::vector<math::Vec> pts;
			for (int i = 0; i < sc::kPoints; ++i) {
				const float x = (float) i / (sc::kPoints - 1);
				pts.push_back(point(c, r, x * 8.f - 4.f, 2.f * std::sin(2.f * M_PI * 1.5f * x)));
			}
			trace(vg, c, r, pts, green, 1.f);
			return;
		}
		const bool xy = module->params[Scp::MODE_PARAM].getValue() < 0.5f;
		const float v1 = sc::voltDiv((int) std::round(module->params[Scp::VOLT1_PARAM].getValue()));
		const float v2 = sc::voltDiv((int) std::round(module->params[Scp::VOLT2_PARAM].getValue()));
		const float p1 = module->params[Scp::POS1_PARAM].getValue();
		const float p2 = module->params[Scp::POS2_PARAM].getValue();
		const bool on1 = module->inputs[Scp::IN1_INPUT].isConnected();
		const bool on2 = module->inputs[Scp::IN2_INPUT].isConnected();
		const sc::Capture& cap = module->capture;

		const NVGcolor colours[2] = {green, amber};
		// As a DSO: the last complete acquisition, the trigger point in the middle. Never half a picture.
		const float(*lo)[sc::kPoints] = cap.shownLo;
		const float(*hi)[sc::kPoints] = cap.shownHi;
		const float(*val)[sc::kPoints] = cap.shownValue;
		const int count = sc::kPoints;

		// Everything inside the square screen; what leaves it is behind the bezel
		nvgSave(vg);
		nvgIntersectScissor(vg, c.x - r, c.y - r, 2 * r, 2 * r);
		drawWaves(vg, c, r, xy, on1, on2, v1, v2, p1, p2, lo, hi, val, count, colours);
		nvgRestore(vg);
		drawMarkers(vg, c, r, v1, v2, p1, p2);
	}

	void drawWaves(NVGcontext* vg, math::Vec c, float r, bool xy, bool on1, bool on2, float v1, float v2, float p1, float p2,
	               const float (*lo)[sc::kPoints], const float (*hi)[sc::kPoints], const float (*val)[sc::kPoints], int count,
	               const NVGcolor* colours) {
		const NVGcolor green = colours[0];
		if (xy) {
			if (!(on1 && on2))
				return;
			std::vector<math::Vec> pts(sc::kPoints);
			for (int i = 0; i < sc::kPoints; ++i)
				pts[i] = point(c, r, sc::divisions(val[0][i], v1, p1), sc::divisions(val[1][i], v2, p2));
			trace(vg, c, r, pts, green, 1.f);
			return;
		}

		// Each point from its minimum to its maximum, so audio at a slow time base is a solid band
		for (int ch = 0; ch < 2; ++ch) {
			if (!(ch == 0 ? on1 : on2))
				continue;
			const float vd = ch == 0 ? v1 : v2, pd = ch == 0 ? p1 : p2;
			std::vector<math::Vec> pts;
			pts.reserve(2 * count);
			for (int i = 0; i < count; ++i) {
				if (!std::isfinite(lo[ch][i]) || !std::isfinite(hi[ch][i]))
					continue;
				const float x = (float) i / (sc::kPoints - 1) * 8.f - 4.f;
				pts.push_back(point(c, r, x, sc::divisions(i % 2 ? lo[ch][i] : hi[ch][i], vd, pd)));
				pts.push_back(point(c, r, x, sc::divisions(i % 2 ? hi[ch][i] : lo[ch][i], vd, pd)));
			}
			trace(vg, c, r, pts, colours[ch], 1.f);
		}
	}

	/** Markers as on a DSO: the trigger point (top, middle) and the trigger level (right edge) */
	void drawMarkers(NVGcontext* vg, math::Vec c, float r, float v1, float v2, float p1, float p2) {
		const NVGcolor mark = nvgRGBA(0xff, 0xff, 0xff, 0xb0);
		const float top = c.y - r + 2.f;
		nvgBeginPath(vg);
		nvgMoveTo(vg, c.x - 4.f, top);
		nvgLineTo(vg, c.x + 4.f, top);
		nvgLineTo(vg, c.x, top + 6.f);
		nvgClosePath(vg);
		nvgFillColor(vg, mark);
		nvgFill(vg);
		// The level in the trigger source's scale (CH 2's when it is the source, else CH 1's)
		const int source = (int) std::round(module->params[Scp::TRIG_SOURCE_PARAM].getValue());
		const float lvDiv = source == 1 ? sc::divisions(module->usedLevel, v2, p2) : sc::divisions(module->usedLevel, v1, p1);
		const float ly = c.y - lvDiv * r / 4.f;
		if (ly > c.y - r + 4.f && ly < c.y + r - 4.f) {
			// A thin dashed grey line across the screen at the trigger level
			nvgBeginPath(vg);
			for (float x = c.x - r + 2.f; x < c.x + r - 8.f; x += 7.f) {
				nvgMoveTo(vg, x, ly);
				nvgLineTo(vg, std::min(x + 4.f, c.x + r - 8.f), ly);
			}
			nvgStrokeWidth(vg, 0.8f);
			nvgStrokeColor(vg, nvgRGBA(0xc8, 0xcc, 0xd0, 0x70));
			nvgStroke(vg);
			const float rx = c.x + r - 2.f;
			nvgBeginPath(vg);
			nvgMoveTo(vg, rx, ly - 4.f);
			nvgLineTo(vg, rx, ly + 4.f);
			nvgLineTo(vg, rx - 6.f, ly);
			nvgClosePath(vg);
			nvgFillColor(vg, mark);
			nvgFill(vg);
		}
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const math::Vec c = box.size.div(2);
		const float r = radius(), outer = box.size.x / 2;

		// Bezel: a grey cast ring with a dark rubber lip around the tube
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.x - outer + 1.5f, c.y - outer + 2.5f, 2 * outer, 2 * outer, mm2px(4.f));
		nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.4f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.x - outer, c.y - outer, 2 * outer, 2 * outer, mm2px(4.f));
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y - outer, c.x, c.y + outer, nvgRGB(0x8e, 0x91, 0x96), nvgRGB(0x4a, 0x4c, 0x51)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.x - outer + mm2px(1.4f), c.y - outer + mm2px(1.4f), 2 * (outer - mm2px(1.4f)), 2 * (outer - mm2px(1.4f)), mm2px(3.f));
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y - outer, c.x, c.y + outer, nvgRGB(0x5a, 0x5c, 0x61), nvgRGB(0x9a, 0x9d, 0xa2)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.x - r - mm2px(1.2f), c.y - r - mm2px(1.2f), 2 * (r + mm2px(1.2f)), 2 * (r + mm2px(1.2f)), mm2px(2.5f));
		nvgFillColor(vg, nvgRGB(0x0d, 0x0e, 0x0f));
		nvgFill(vg);
		// Phosphor screen, dark green, a little lighter in the middle
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.x - r, c.y - r, 2 * r, 2 * r, mm2px(2.f));
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, r * 0.1f, r * 1.3f, nvgRGB(0x10, 0x2a, 0x1c), nvgRGB(0x05, 0x10, 0x0a)));
		nvgFill(vg);

		// Graticule: 8 × 8 divisions over the square screen, ticks on the axes
		const float d = r / 4.f;
		const NVGcolor grid = nvgRGBA(0x6a, 0xc8, 0x90, 0x38);
		nvgStrokeWidth(vg, 0.7f);
		for (int i = -3; i <= 3; ++i) {
			const float o = i * d, half = r;
			nvgBeginPath(vg);
			nvgMoveTo(vg, c.x + o, c.y - half);
			nvgLineTo(vg, c.x + o, c.y + half);
			nvgMoveTo(vg, c.x - half, c.y + o);
			nvgLineTo(vg, c.x + half, c.y + o);
			nvgStrokeColor(vg, i == 0 ? nvgRGBA(0x6a, 0xc8, 0x90, 0x60) : grid);
			nvgStroke(vg);
		}
		nvgBeginPath(vg);
		for (int i = -20; i <= 20; ++i) {
			const float o = i * d / 5.f;
			if (std::fabs(o) >= r * 0.98f)
				continue;
			nvgMoveTo(vg, c.x + o, c.y - 2.f);
			nvgLineTo(vg, c.x + o, c.y + 2.f);
			nvgMoveTo(vg, c.x - 2.f, c.y + o);
			nvgLineTo(vg, c.x + 2.f, c.y + o);
		}
		nvgStrokeColor(vg, nvgRGBA(0x6a, 0xc8, 0x90, 0x60));
		nvgStroke(vg);

		drawTraces(vg, c, r);

		// Curved glass: a soft highlight top left and a darker rim
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.x - r, c.y - r, 2 * r, 2 * r, mm2px(2.f));
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x - r * 0.35f, c.y - r * 0.45f, r * 0.05f, r * 0.75f, nvgRGBAf(1, 1, 1, 0.10f), nvgRGBAf(1, 1, 1, 0)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.x - r, c.y - r, 2 * r, 2 * r, mm2px(2.f));
		nvgFillPaint(vg, nvgBoxGradient(vg, c.x - r, c.y - r, 2 * r, 2 * r, mm2px(2.f), r * 0.25f, nvgRGBAf(0, 0, 0, 0), nvgRGBAf(0, 0, 0, 0.45f)));
		nvgFill(vg);
	}
};


struct ScpWidget : JXMPModuleWidget {
	ScpWidget(Scp* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-SCP", nvgRGB(0x5f, 0xf0, 0xa0), 24);
		styled();

		// The tube, left
		CrtScreen* const crt = new CrtScreen;
		crt->module = module;
		// The tube fills the left side; its top lines up with the TIME / DIV header
		const float crtR = 36.f;  // mm, half the bezel
		crt->box = math::Rect(mm2px(math::Vec(42.f - crtR, 56.5f - crtR)), mm2px(math::Vec(2 * crtR, 2 * crtR)));
		addChild(crt);

		// TIME/DIV, right top
		// Labels sit in the headers or under the knobs, so every scale keeps its value at the top
		section(83.f, 20.5f, 35.f, 27.f, "TIME / DIV");
		knob(math::Vec(100.5f, 36.5f), Scp::TIME_PARAM, 15.f, st::CAP_BLACK, 0.f, sc::kTimeSteps - 1,
		     {{0.f, ".1m"}, {1.f, ""}, {2.f, ""}, {3.f, "1m"}, {4.f, ""}, {5.f, ""}, {6.f, "10m"},
		      {7.f, ""}, {8.f, ""}, {9.f, ".1"}, {10.f, ""}, {11.f, ""}, {12.f, "1s"}});

		// Trigger
		// The divider stops short: TRIG'D and its lamp sit at its end, in the header row
		section(83.f, 52.f, 22.f, 25.f, "TRIGGER");
		label(math::Vec(110.f, 52.f), "TRIG'D", 5.5f);
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(116.f, 52.f)), module, Scp::TRIG_LIGHT));
		knob(math::Vec(92.f, 64.f), Scp::TRIG_LEVEL_PARAM, 9.f, st::CAP_WHITE, -10.f, 10.f,
		     st::marks(-10.f, 10.f, 2.f, 5, [](float v) { return v == 0.f ? std::string("0") : string::f("%+d", (int) v); }));
		label(math::Vec(92.f, 76.f), "LEVEL", 6.5f);
		toggle(math::Vec(110.f, 66.f), Scp::TRIG_SLOPE_PARAM, 2, {"RISE", "FALL"});

		// Trigger source, AUTO / NORM, display mode
		section(83.f, 82.f, 35.f, 18.f, "SOURCE · MODE");
		toggle(math::Vec(90.f, 93.f), Scp::TRIG_SOURCE_PARAM, 3, {"CH1", "CH2", "EXT"});
		toggle(math::Vec(102.f, 93.f), Scp::TRIG_MODE_PARAM, 2, {"AUTO", "NORM"});
		toggle(math::Vec(113.5f, 93.f), Scp::MODE_PARAM, 2, {"Y-T", "X-Y"});

		// Channels, under the tube
		const std::vector<st::Mark> volts = {{0.f, ".1"}, {1.f, ".2"}, {2.f, ".5"}, {3.f, "1"}, {4.f, "2"}, {5.f, "5"}};
		const std::vector<st::Mark> pos = st::marks(-4.f, 4.f, 1.f, 4, [](float v) { return v == 0.f ? std::string("0") : string::f("%+d", (int) v); });
		const struct { const char* title; float x; int volt, pos; } chans[2] = {
		    {"CH 1 · GREEN", 4.f, Scp::VOLT1_PARAM, Scp::POS1_PARAM},
		    {"CH 2 · AMBER", 45.f, Scp::VOLT2_PARAM, Scp::POS2_PARAM},
		};
		for (const auto& ch : chans) {
			section(ch.x, 95.f, 37.f, 22.f, ch.title);
			knob(math::Vec(ch.x + 10.f, 106.5f), ch.volt, 8.f, st::CAP_BLACK, 0.f, sc::kVoltSteps - 1, volts);
			label(math::Vec(ch.x + 10.f, 116.f), "V / DIV", 6.f);
			knob(math::Vec(ch.x + 27.f, 106.5f), ch.pos, 8.f, st::CAP_WHITE, -4.f, 4.f, pos);
			label(math::Vec(ch.x + 27.f, 116.f), "POSITION", 6.f);
		}

		// Back: the three inputs
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(30.f, 40.f)), module, Scp::IN1_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(61.f, 40.f)), module, Scp::IN2_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(92.f, 40.f)), module, Scp::TRIG_INPUT));
	}
};

} // namespace


Model* modelJXMPScope = createModel<Scp, ScpWidget>("Scope");

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SCP: two-channel oscilloscope with a round green CRT, styled after an old lab scope. Y–T with a trigger
 * (level, slope, source CH1 / CH2 / EXT, AUTO free-run when nothing triggers) or X–Y; the trace leaves a short
 * phosphor afterglow. Design: research_docs/14-jxmp-modules-set3-design.md.
 */

#include "JXMPWidget.hpp"
#include "dsp/Scope.hpp"

#include <deque>

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
		NUM_PARAMS
	};
	enum InputIds { IN1_INPUT, IN2_INPUT, TRIG_INPUT, NUM_INPUTS };
	enum OutputIds { NUM_OUTPUTS };
	enum LightIds { TRIG_LIGHT, NUM_LIGHTS };

	sc::Trigger trigger;
	sc::Capture capture;
	float trigLight = 0.f;

	Scp() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(TIME_PARAM, 0.f, sc::kTimeSteps - 1, 6.f, "Time / div",
		             {"0.1 ms", "0.2 ms", "0.5 ms", "1 ms", "2 ms", "5 ms", "10 ms", "20 ms", "50 ms", "0.1 s", "0.2 s", "0.5 s", "1 s"});
		configSwitch(VOLT1_PARAM, 0.f, sc::kVoltSteps - 1, 4.f, "CH 1 volts / div", {"0.1 V", "0.2 V", "0.5 V", "1 V", "2 V", "5 V"});
		configParam(POS1_PARAM, -4.f, 4.f, 0.f, "CH 1 position", " div");
		configSwitch(VOLT2_PARAM, 0.f, sc::kVoltSteps - 1, 4.f, "CH 2 volts / div", {"0.1 V", "0.2 V", "0.5 V", "1 V", "2 V", "5 V"});
		configParam(POS2_PARAM, -4.f, 4.f, 0.f, "CH 2 position", " div");
		configParam(TRIG_LEVEL_PARAM, -10.f, 10.f, 0.f, "Trigger level", " V");
		configSwitch(TRIG_SLOPE_PARAM, 0.f, 1.f, 0.f, "Trigger slope", {"Rising", "Falling"});
		configSwitch(TRIG_SOURCE_PARAM, 0.f, 2.f, 0.f, "Trigger source", {"CH 1", "CH 2", "External"});
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"Y–T (time)", "X–Y (CH 1 across, CH 2 up)"});
		configInput(IN1_INPUT, "In 1")->description = "Channel 1, the green trace (X in X–Y mode). Polyphonic: the first voice is drawn.";
		configInput(IN2_INPUT, "In 2")->description = "Channel 2, the amber trace (Y in X–Y mode).";
		configInput(TRIG_INPUT, "Trig")->description = "External trigger, used when the trigger source is EXT.";
		configLight(TRIG_LIGHT, "Triggered");
	}

	void process(const ProcessArgs& args) override {
		const float a = inputs[IN1_INPUT].getVoltage();
		const float b = inputs[IN2_INPUT].getVoltage();
		const int source = (int) std::round(params[TRIG_SOURCE_PARAM].getValue());
		const float t = source == 0 ? a : source == 1 ? b : inputs[TRIG_INPUT].getVoltage();
		const bool fired = trigger.process(t, params[TRIG_LEVEL_PARAM].getValue(), params[TRIG_SLOPE_PARAM].getValue() < 0.5f);
		const bool xy = params[MODE_PARAM].getValue() > 0.5f;
		if (fired && !capture.running() && !xy)
			trigLight = 1.f;
		capture.process(a, b, fired, args.sampleTime, sc::timeDiv((int) std::round(params[TIME_PARAM].getValue())), xy);
		trigLight = std::max(0.f, trigLight - args.sampleTime * 5.f);
		lights[TRIG_LIGHT].setBrightness(trigLight);
	}
};


/** The round CRT: bezel, phosphor screen with a graticule, the traces with afterglow, a glass highlight. */
struct CrtScreen : widget::TransparentWidget {
	Scp* module = nullptr;

	struct Frame {
		float data[2][sc::kPoints];
		double time;
	};
	std::deque<Frame> frames;
	int lastSweep = -1;

	static constexpr float kAfterglow = 0.15f;  // seconds until a trace has faded

	float radius() const {
		return box.size.x / 2 - mm2px(4.5f);  // the screen inside the bezel
	}

	void step() override {
		TransparentWidget::step();
		const double now = system::getTime();
		if (module && module->capture.sweeps != lastSweep) {
			lastSweep = module->capture.sweeps;
			Frame f;
			std::copy(&module->capture.shown[0][0], &module->capture.shown[0][0] + 2 * sc::kPoints, &f.data[0][0]);
			f.time = now;
			frames.push_back(f);
		}
		while (frames.size() > 1 && now - frames.front().time > kAfterglow)
			frames.pop_front();
		while (frames.size() > 12)
			frames.pop_front();
	}

	/** Screen point of (x, y) in divisions; kept on the round screen. */
	math::Vec point(math::Vec c, float r, float xDiv, float yDiv) const {
		const float d = r / 4.f;  // 8 divisions across the diameter
		math::Vec p(xDiv * d, -yDiv * d);
		const float len = p.norm(), maxLen = r * 0.985f;
		if (len > maxLen)
			p = p.mult(maxLen / len);
		return c.plus(p);
	}

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
		const bool xy = module ? module->params[Scp::MODE_PARAM].getValue() > 0.5f : false;
		const float v1 = module ? sc::voltDiv((int) std::round(module->params[Scp::VOLT1_PARAM].getValue())) : 2.f;
		const float v2 = module ? sc::voltDiv((int) std::round(module->params[Scp::VOLT2_PARAM].getValue())) : 2.f;
		const float p1 = module ? module->params[Scp::POS1_PARAM].getValue() : 0.f;
		const float p2 = module ? module->params[Scp::POS2_PARAM].getValue() : 0.f;
		const bool on1 = module ? module->inputs[Scp::IN1_INPUT].isConnected() : true;
		const bool on2 = module ? module->inputs[Scp::IN2_INPUT].isConnected() : false;
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

		const double now = system::getTime();
		for (const Frame& f : frames) {
			const float age = (float) (now - f.time);
			const float alpha = &f == &frames.back() ? 1.f : std::max(0.f, 1.f - age / kAfterglow) * 0.6f;
			if (alpha <= 0.f)
				continue;
			std::vector<math::Vec> pts(sc::kPoints);
			if (xy) {
				if (!(on1 && on2))
					continue;
				for (int i = 0; i < sc::kPoints; ++i)
					pts[i] = point(c, r, sc::divisions(f.data[0][i], v1, p1), sc::divisions(f.data[1][i], v2, p2));
				trace(vg, c, r, pts, green, alpha);
				continue;
			}
			for (int ch = 0; ch < 2; ++ch) {
				if (!(ch == 0 ? on1 : on2))
					continue;
				for (int i = 0; i < sc::kPoints; ++i) {
					const float x = (float) i / (sc::kPoints - 1) * 8.f - 4.f;
					pts[i] = point(c, r, x, sc::divisions(f.data[ch][i], ch == 0 ? v1 : v2, ch == 0 ? p1 : p2));
				}
				trace(vg, c, r, pts, ch == 0 ? green : amber, alpha);
			}
		}
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const math::Vec c = box.size.div(2);
		const float r = radius(), outer = box.size.x / 2;

		// Bezel: a grey cast ring with a dark rubber lip around the tube
		nvgBeginPath(vg);
		nvgCircle(vg, c.x + 1.5f, c.y + 2.5f, outer);
		nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.4f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, outer);
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y - outer, c.x, c.y + outer, nvgRGB(0x8e, 0x91, 0x96), nvgRGB(0x4a, 0x4c, 0x51)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, outer - mm2px(1.4f));
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y - outer, c.x, c.y + outer, nvgRGB(0x5a, 0x5c, 0x61), nvgRGB(0x9a, 0x9d, 0xa2)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r + mm2px(1.2f));
		nvgFillColor(vg, nvgRGB(0x0d, 0x0e, 0x0f));
		nvgFill(vg);
		// Four bezel screws
		for (int i = 0; i < 4; ++i) {
			const float a = M_PI / 4 + i * M_PI / 2;
			const math::Vec s(c.x + std::cos(a) * (outer - mm2px(2.6f)), c.y + std::sin(a) * (outer - mm2px(2.6f)));
			nvgBeginPath(vg);
			nvgCircle(vg, s.x, s.y, mm2px(0.9f));
			nvgFillPaint(vg, nvgLinearGradient(vg, s.x, s.y - 3, s.x, s.y + 3, nvgRGB(0xc4, 0xc6, 0xca), nvgRGB(0x5c, 0x5e, 0x62)));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgMoveTo(vg, s.x - mm2px(0.6f), s.y);
			nvgLineTo(vg, s.x + mm2px(0.6f), s.y);
			nvgStrokeWidth(vg, 0.8f);
			nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.6f));
			nvgStroke(vg);
		}

		// Phosphor screen, dark green, a little lighter in the middle
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r);
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, r * 0.1f, r, nvgRGB(0x10, 0x2a, 0x1c), nvgRGB(0x05, 0x10, 0x0a)));
		nvgFill(vg);

		// Graticule: 8 × 8 divisions over the diameter, lines as chords of the circle, ticks on the axes
		const float d = r / 4.f;
		const NVGcolor grid = nvgRGBA(0x6a, 0xc8, 0x90, 0x38);
		nvgStrokeWidth(vg, 0.7f);
		for (int i = -3; i <= 3; ++i) {
			const float o = i * d, half = std::sqrt(std::max(0.f, r * r - o * o));
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
		nvgCircle(vg, c.x, c.y, r);
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x - r * 0.35f, c.y - r * 0.45f, r * 0.05f, r * 0.75f, nvgRGBAf(1, 1, 1, 0.10f), nvgRGBAf(1, 1, 1, 0)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r);
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, r * 0.8f, r, nvgRGBAf(0, 0, 0, 0), nvgRGBAf(0, 0, 0, 0.45f)));
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
		const float crtR = 33.5f;  // mm, bezel outside
		crt->box = math::Rect(mm2px(math::Vec(42.f - crtR, 59.f - crtR)), mm2px(math::Vec(2 * crtR, 2 * crtR)));
		addChild(crt);

		// TIME/DIV, right top
		section(83.f, 20.5f, 35.f, 27.f, "SWEEP");
		// No numbers at the top of a scale: the label above the knob sits there
		label(math::Vec(100.5f, 24.5f), "TIME / DIV", 7.f);
		knob(math::Vec(100.5f, 38.5f), Scp::TIME_PARAM, 15.f, st::CAP_BLACK, 0.f, sc::kTimeSteps - 1,
		     {{0.f, ".1m"}, {1.f, ""}, {2.f, ""}, {3.f, "1m"}, {4.f, ""}, {5.f, ""}, {6.f, ""},
		      {7.f, ""}, {8.f, ""}, {9.f, ".1"}, {10.f, ""}, {11.f, ""}, {12.f, "1s"}});

		// Trigger
		section(83.f, 52.f, 35.f, 25.f, "TRIGGER");
		label(math::Vec(92.f, 56.5f), "LEVEL", 6.5f);
		knob(math::Vec(92.f, 66.5f), Scp::TRIG_LEVEL_PARAM, 9.f, st::CAP_WHITE, -10.f, 10.f,
		     st::marks(-10.f, 10.f, 2.f, 5, [](float v) { return v == 0.f ? std::string() : string::f("%+d", (int) v); }));
		toggle(math::Vec(110.f, 66.f), Scp::TRIG_SLOPE_PARAM, 2, {"RISE", "FALL"});
		// Lit while sweeps start on the trigger (dark when AUTO free-runs)
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(97.5f, 78.5f)), module, Scp::TRIG_LIGHT));
		label(math::Vec(104.5f, 78.5f), "TRIG'D", 5.5f);

		// Source and mode
		section(83.f, 82.f, 35.f, 18.f, "SOURCE · MODE");
		toggle(math::Vec(95.f, 93.f), Scp::TRIG_SOURCE_PARAM, 3, {"CH1", "CH2", "EXT"});
		toggle(math::Vec(111.f, 93.f), Scp::MODE_PARAM, 2, {"Y-T", "X-Y"});

		// Channels, under the tube
		const std::vector<st::Mark> volts = {{0.f, ".1"}, {1.f, ""}, {2.f, ""}, {3.f, ""}, {4.f, ""}, {5.f, "5"}};
		const std::vector<st::Mark> pos = st::marks(-4.f, 4.f, 1.f, 4, [](float v) { return v == 0.f ? std::string() : string::f("%+d", (int) v); });
		const struct { const char* title; float x; int volt, pos; } chans[2] = {
		    {"CH 1 · GREEN", 4.f, Scp::VOLT1_PARAM, Scp::POS1_PARAM},
		    {"CH 2 · AMBER", 45.f, Scp::VOLT2_PARAM, Scp::POS2_PARAM},
		};
		for (const auto& ch : chans) {
			section(ch.x, 97.f, 37.f, 20.f, ch.title);
			label(math::Vec(ch.x + 10.f, 100.5f), "V / DIV", 6.f);
			knob(math::Vec(ch.x + 10.f, 109.5f), ch.volt, 8.f, st::CAP_BLACK, 0.f, sc::kVoltSteps - 1, volts);
			label(math::Vec(ch.x + 27.f, 100.5f), "POSITION", 6.f);
			knob(math::Vec(ch.x + 27.f, 109.5f), ch.pos, 8.f, st::CAP_WHITE, -4.f, 4.f, pos);
		}

		// Back: the three inputs
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(30.f, 40.f)), module, Scp::IN1_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(61.f, 40.f)), module, Scp::IN2_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(92.f, 40.f)), module, Scp::TRIG_INPUT));
	}
};

} // namespace


Model* modelJXMPScope = createModel<Scp, ScpWidget>("Scope");

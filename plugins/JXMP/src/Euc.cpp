/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-EUC: four Euclidean rhythm tracks. Each spreads HITS as evenly as possible over STEPS, shifted by ROTATE,
 * and steps on CLOCK (e.g. JX-CLK x4). GATE follows the clock on a hit, ACCENT on the first hit of the pattern.
 *
 */

#include "JXMPWidget.hpp"
#include "dsp/Euclid.hpp"

namespace {

namespace eu = jxmp::euclid;

constexpr int kTracks = 4;

struct Euc : engine::Module {
	enum ParamIds { ENUMS(STEPS_PARAM, kTracks), ENUMS(HITS_PARAM, kTracks), ENUMS(ROTATE_PARAM, kTracks), NUM_PARAMS };
	enum InputIds { CLOCK_INPUT, RESET_INPUT, NUM_INPUTS };
	enum OutputIds { ENUMS(GATE_OUTPUT, kTracks), ENUMS(ACCENT_OUTPUT, kTracks), NUM_OUTPUTS };
	enum LightIds { ENUMS(HIT_LIGHT, kTracks), NUM_LIGHTS };

	dsp::SchmittTrigger clockTrigger, resetTrigger;
	eu::Track tracks[kTracks];

	Euc() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// Ready-made beat: four on the floor, backbeat, eighths, a five-hit figure
		const int steps[kTracks] = {16, 16, 16, 16}, hits[kTracks] = {4, 2, 8, 5}, rotate[kTracks] = {0, 4, 2, 0};
		for (int t = 0; t < kTracks; ++t) {
			configParam(STEPS_PARAM + t, 1.f, eu::kMaxSteps, steps[t], string::f("Track %d steps", t + 1))->snapEnabled = true;
			configParam(HITS_PARAM + t, 0.f, eu::kMaxSteps, hits[t], string::f("Track %d hits", t + 1))->snapEnabled = true;
			configParam(ROTATE_PARAM + t, 0.f, eu::kMaxSteps - 1, rotate[t], string::f("Track %d rotate", t + 1))->snapEnabled = true;
			paramQuantities[STEPS_PARAM + t]->description = "How long the pattern is, in clock steps.";
			paramQuantities[HITS_PARAM + t]->description = "How many hits are spread as evenly as possible over the steps.";
			paramQuantities[ROTATE_PARAM + t]->description = "Moves the whole pattern later by this many steps.";
			configOutput(GATE_OUTPUT + t, string::f("Gate %d", t + 1))->description =
			    "High while the clock is high on a hit: a trigger for a drum voice or an envelope.";
			configOutput(ACCENT_OUTPUT + t, string::f("Accent %d", t + 1))->description =
			    "Like GATE, but only on the first hit of the pattern: patch it to an ACCENT input.";
		}
		configInput(CLOCK_INPUT, "Clock")->description = "Every rising edge moves all tracks one step, e.g. JX-CLK x4 (sixteenths).";
		configInput(RESET_INPUT, "Reset")->description = "Back to the start: the next clock plays step 1 (patch JX-CLK RESET here).";
	}

	int steps(int t) {
		return (int) params[STEPS_PARAM + t].getValue();
	}

	bool isHit(int t, int step) {
		return step >= 0 && eu::hit(step, steps(t), (int) params[HITS_PARAM + t].getValue(), (int) params[ROTATE_PARAM + t].getValue());
	}

	void onReset() override {
		for (eu::Track& t : tracks)
			t.reset();
	}

	/** The step of each track is saved, so a reloaded patch goes on where it was */
	json_t* dataToJson() override {
		json_t* const rootJ = json_object();
		json_t* const stepsJ = json_array();
		for (const eu::Track& t : tracks)
			json_array_append_new(stepsJ, json_integer(t.step));
		json_object_set_new(rootJ, "steps", stepsJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* const stepsJ = json_object_get(rootJ, "steps");
		for (int t = 0; t < kTracks && t < (int) json_array_size(stepsJ); ++t)
			tracks[t].restore((int) json_integer_value(json_array_get(stepsJ, t)));
	}

	void process(const ProcessArgs& args) override {
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
			onReset();
		const bool clocked = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		const bool clockHigh = clockTrigger.isHigh();
		for (int t = 0; t < kTracks; ++t) {
			if (clocked)
				tracks[t].clock(steps(t));
			const int step = tracks[t].current(steps(t));
			const bool on = clockHigh && isHit(t, step);
			const bool accent = on && step == eu::accentStep(steps(t), (int) params[ROTATE_PARAM + t].getValue());
			outputs[GATE_OUTPUT + t].setVoltage(on ? 10.f : 0.f);
			outputs[ACCENT_OUTPUT + t].setVoltage(accent ? 10.f : 0.f);
			lights[HIT_LIGHT + t].setBrightnessSmooth(on ? 1.f : 0.f, args.sampleTime);
		}
	}
};


/** Four rows of 16 dots: the pattern of each track (dark past its length), the current step ringed. */
struct PatternDisplay : widget::TransparentWidget {
	Euc* module = nullptr;
	NVGcolor colour;

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 2.5f);
		nvgFillColor(vg, nvgRGB(0x0d, 0x0e, 0x10));
		nvgFill(vg);
		const float padX = mm2px(2.5f), padY = mm2px(2.2f);
		const float dx = (box.size.x - 2 * padX) / eu::kMaxSteps, dy = (box.size.y - 2 * padY) / kTracks;
		const float rad = std::min(dx, dy) * 0.3f;
		static const int demoHits[kTracks] = {4, 2, 8, 5}, demoRot[kTracks] = {0, 4, 2, 0};
		for (int t = 0; t < kTracks; ++t) {
			const int steps = module ? module->steps(t) : 16;
			const int current = module ? module->tracks[t].current(steps) : -1;
			for (int i = 0; i < eu::kMaxSteps; ++i) {
				const float x = padX + (i + 0.5f) * dx, y = padY + (t + 0.5f) * dy;
				const bool inside = i < steps;
				const bool on = inside && (module ? module->isHit(t, i) : eu::hit(i, 16, demoHits[t], demoRot[t]));
				nvgBeginPath(vg);
				nvgCircle(vg, x, y, rad);
				nvgFillColor(vg, on ? colour : inside ? nvgRGB(0x3a, 0x3c, 0x42) : nvgRGB(0x1a, 0x1b, 0x1e));
				nvgFill(vg);
				if (i == current) {
					nvgBeginPath(vg);
					nvgCircle(vg, x, y, rad + 1.8f);
					nvgStrokeWidth(vg, 1.2f);
					nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
					nvgStroke(vg);
				}
			}
		}
	}
};


struct EucWidget : JXMPModuleWidget {
	EucWidget(Euc* const module) {
		namespace st = jxmp::style;
		const NVGcolor colour = nvgRGB(0xe0, 0x5a, 0x8a);
		setup(module, "JX-EUC", colour, 14);
		styled();
		const float w = 71.12f;

		section(3.f, 20.5f, w - 6.f, 24.f, "PATTERNS");
		PatternDisplay* const display = new PatternDisplay;
		display->module = module;
		display->colour = colour;
		display->box = math::Rect(mm2px(math::Vec(5.f, 24.f)), mm2px(math::Vec(w - 10.f, 18.f)));
		addChild(display);

		// One row per track: number and hit lamp, STEPS, HITS, ROTATE
		section(3.f, 48.f, w - 6.f, 69.f, "STEPS · HITS · ROTATE");
		label(math::Vec(26.f, 53.f), "STEPS", 6.f);
		label(math::Vec(43.f, 53.f), "HITS", 6.f);
		label(math::Vec(60.f, 53.f), "ROTATE", 6.f);
		const std::vector<st::Mark> sixteen = st::marks(1.f, 16.f, 1.f, 15, [](float v) { return string::f("%d", (int) v); });
		const std::vector<st::Mark> hitMarks = st::marks(0.f, 16.f, 1.f, 16, [](float v) { return string::f("%d", (int) v); });
		const std::vector<st::Mark> rotMarks = st::marks(0.f, 15.f, 1.f, 15, [](float v) { return string::f("%d", (int) v); });
		for (int t = 0; t < kTracks; ++t) {
			const float y = 63.f + t * 15.f;
			label(math::Vec(7.f, y), string::f("%d", t + 1).c_str(), 8.f);
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(math::Vec(12.5f, y)), module, Euc::HIT_LIGHT + t));
			knob(math::Vec(26.f, y), Euc::STEPS_PARAM + t, 8.f, st::CAP_BLACK, 1.f, 16.f, sixteen);
			knob(math::Vec(43.f, y), Euc::HITS_PARAM + t, 8.f, st::CAP_MAIN, 0.f, 16.f, hitMarks);
			knob(math::Vec(60.f, y), Euc::ROTATE_PARAM + t, 8.f, st::CAP_WHITE, 0.f, 15.f, rotMarks);
		}

		// Back: clock and reset, then gate and accent of each track
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(18.f, 32.f)), module, Euc::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(53.f, 32.f)), module, Euc::RESET_INPUT));
		for (int t = 0; t < kTracks; ++t) {
			const float y = 52.f + t * 17.f;
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(18.f, y)), module, Euc::GATE_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(53.f, y)), module, Euc::ACCENT_OUTPUT + t));
		}
	}
};

} // namespace


Model* modelJXMPEuclid = createModel<Euc, EucWidget>("Euclid");

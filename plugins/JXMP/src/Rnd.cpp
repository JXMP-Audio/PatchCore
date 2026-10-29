/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-RND: random voltages. Sample & hold of noise (or of the IN socket) at an internal rate or on a
 * clock, with a smooth companion that glides between the values.
 */

#include "JXMPWidget.hpp"
#include "dsp/Lfo.hpp"
#include "dsp/Random.hpp"

namespace {

struct Rnd : engine::Module {
	enum ParamIds { RATE_PARAM, RATE_CV_PARAM, LEVEL_PARAM, OFFSET_PARAM, NUM_PARAMS };
	enum InputIds { CLOCK_INPUT, IN_INPUT, RATE_INPUT, NUM_INPUTS };
	enum OutputIds { STEP_OUTPUT, SMOOTH_OUTPUT, TRIG_OUTPUT, NUM_OUTPUTS };
	enum LightIds { TICK_LIGHT, NUM_LIGHTS };

	jxmp::RandomCore core;
	dsp::SchmittTrigger clockTrigger;
	dsp::Timer clockTimer;
	float clockFreq = 2.f;
	dsp::PulseGenerator trigPulse;

	Rnd() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(RATE_PARAM, -5.f, 8.f, 1.f, "Rate", " Hz", 2.f, 1.f);
		configParam(RATE_CV_PARAM, -1.f, 1.f, 0.f, "Rate CV", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 1.f, 1.f, "Level", "%", 0.f, 100.f);
		configSwitch(OFFSET_PARAM, 0.f, 1.f, 0.f, "Offset", {"Bipolar (±5 V)", "Unipolar (0–10 V)"});
		configInput(CLOCK_INPUT, "Clock");
		configInput(IN_INPUT, "In (sampled instead of noise)");
		configInput(RATE_INPUT, "Rate CV");
		configOutput(STEP_OUTPUT, "Step");
		configOutput(SMOOTH_OUTPUT, "Smooth");
		configOutput(TRIG_OUTPUT, "Trig");
	}

	/** A new value: the IN socket if patched, else uniform noise, both scaled to ±1 */
	float sample() {
		if (inputs[IN_INPUT].isConnected())
			return math::clamp(inputs[IN_INPUT].getVoltage() / 5.f, -1.f, 1.f);
		return random::uniform() * 2.f - 1.f;
	}

	void process(const ProcessArgs& args) override {
		bool tick = false;
		if (inputs[CLOCK_INPUT].isConnected()) {
			// Clocked: tick on every edge and glide over the measured clock period
			clockTimer.process(args.sampleTime);
			if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f)) {
				const float f = 1.f / std::max(clockTimer.getTime(), 1e-3f);
				clockTimer.reset();
				if (f <= 1000.f)
					clockFreq = f;
				tick = true;
			}
			core.advance(clockFreq, args.sampleTime, false);
		}
		else {
			const float pitch = params[RATE_PARAM].getValue() + inputs[RATE_INPUT].getVoltage() * params[RATE_CV_PARAM].getValue();
			tick = core.advance(jxmp::lfoFreq(pitch, 2.f), args.sampleTime, true);
		}
		if (tick) {
			core.tick(sample());
			trigPulse.trigger(1e-3f);
		}

		const bool unipolar = params[OFFSET_PARAM].getValue() > 0.5f;
		const float level = params[LEVEL_PARAM].getValue();
		auto scale = [&](float v) { return unipolar ? (v + 1.f) * 5.f * level : v * 5.f * level; };
		outputs[STEP_OUTPUT].setVoltage(scale(core.held));
		outputs[SMOOTH_OUTPUT].setVoltage(scale(core.smooth()));
		const bool trig = trigPulse.process(args.sampleTime);
		outputs[TRIG_OUTPUT].setVoltage(trig ? 10.f : 0.f);
		lights[TICK_LIGHT].setBrightnessSmooth(trig ? 1.f : 0.f, args.sampleTime * 0.1f);
	}
};


struct RndWidget : JXMPModuleWidget {
	RndWidget(Rnd* const module) {
		setup(module, "JX-RND", nvgRGB(0xe0, 0xa4, 0x3a), 8);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();

		// RATE: 2^param Hz (same law as JX-LFO), 0.03 Hz .. 256 Hz
		section(3.f, 20.5f, 34.64f, 52.f, "RATE");
		label(math::Vec(20.32f, 25.5f), "RATE", 7.5f);
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(math::Vec(27.f, 25.5f)), module, Rnd::TICK_LIGHT));
		std::vector<st::Mark> rateMarks;
		for (int p = -5; p <= 8; ++p)
			rateMarks.push_back({(float) p, p == -5 ? ".03" : p == 1 ? "2" : p == 8 ? "256" : ""});
		knob(math::Vec(20.32f, 41.5f), Rnd::RATE_PARAM, 15.f, st::CAP_MAIN, -5.f, 8.f, rateMarks);
		label(math::Vec(20.32f, 56.f), "RATE CV", 7.f);
		knob(math::Vec(20.32f, 64.f), Rnd::RATE_CV_PARAM, 7.f, st::CAP_GREY, -1.f, 1.f, st::bipolar());

		// OUTPUT
		section(3.f, 77.f, 34.64f, 40.5f, "OUTPUT");
		label(math::Vec(12.f, 82.f), "LEVEL", 7.f);
		knob(math::Vec(12.f, 93.5f), Rnd::LEVEL_PARAM, 9.f, st::CAP_WHITE, 0.f, 1.f,
		     st::marks(0.f, 1.f, 0.1f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); }));
		label(math::Vec(29.f, 82.f), "OFFSET", 7.f);
		toggle(math::Vec(29.f, 95.f), Rnd::OFFSET_PARAM, 2, {"UNI", "BI"});

		// Back: sockets
		for (int i = 0; i < Rnd::NUM_INPUTS; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(11.f, 32.f + i * 16.f)), module, Rnd::CLOCK_INPUT + i));
		for (int i = 0; i < Rnd::NUM_OUTPUTS; ++i)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(29.f, 32.f + i * 16.f)), module, Rnd::STEP_OUTPUT + i));
	}
};

} // namespace


Model* modelJXMPRandom = createModel<Rnd, RndWidget>("Random");

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-DLY: mono delay, 1 ms to 10 s, with feedback, tone and dry/wet mix.
 */

#include "JXMPWidget.hpp"
#include "dsp/Delay.hpp"

namespace {

struct Dly : engine::Module {
	enum ParamIds { TIME_PARAM, FEEDBACK_PARAM, TONE_PARAM, MIX_PARAM, NUM_PARAMS };
	enum InputIds { IN_INPUT, TIME_INPUT, FEEDBACK_INPUT, TONE_INPUT, MIX_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_OUTPUT, WET_OUTPUT, NUM_OUTPUTS };
	enum LightIds { TIME_LIGHT, NUM_LIGHTS };

	jxmp::DelayCore core;
	float blink = 0.f;

	Dly() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Time", " s", 10000.f, 0.001f);
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.4f, "Feedback", "%", 0.f, 100.f);
		configParam(TONE_PARAM, -1.f, 1.f, 0.f, "Tone (dark ← → thin)", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix (dry ← → wet)", "%", 0.f, 100.f);
		configInput(IN_INPUT, "In")->description = "Audio to delay (polyphonic inputs are summed to mono).";
		configInput(TIME_INPUT, "Time CV")->description = "Time CV: 1 V moves the TIME knob by a tenth of its range. Changing it bends the pitch like tape.";
		configInput(FEEDBACK_INPUT, "FB CV")->description = "Feedback CV, 10 % per volt.";
		configInput(TONE_INPUT, "Tone CV")->description = "Tone CV, 10 % per volt (negative darker, positive thinner).";
		configInput(MIX_INPUT, "Mix CV")->description = "Mix CV, 10 % per volt.";
		configOutput(OUT_OUTPUT, "Out")->description = "Dry and delayed signal mixed by MIX.";
		configOutput(WET_OUTPUT, "Wet")->description = "Only the delayed signal (the repeats), whatever MIX is set.";
		configBypass(IN_INPUT, OUT_OUTPUT);
		allocate(48000.f);
	}

	void allocate(float sampleRate) {
		core.init((size_t) (sampleRate * 10.f) + 16);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		allocate(e.sampleRate);
	}

	void process(const ProcessArgs& args) override {
		// CV: 1 V moves TIME by a tenth of its range, the others by 10 % per volt
		const float timeKnob = params[TIME_PARAM].getValue() + inputs[TIME_INPUT].getVoltage() / 10.f;
		const float feedback = params[FEEDBACK_PARAM].getValue() + inputs[FEEDBACK_INPUT].getVoltage() / 10.f;
		const float tone = math::clamp(params[TONE_PARAM].getValue() + inputs[TONE_INPUT].getVoltage() / 10.f, -1.f, 1.f);
		const float mix = params[MIX_PARAM].getValue() + inputs[MIX_INPUT].getVoltage() / 10.f;
		const float seconds = jxmp::delayTime(timeKnob);

		const float in = inputs[IN_INPUT].getVoltageSum();
		const float out = core.process(in, seconds * args.sampleRate, feedback, tone, mix, args.sampleTime);
		outputs[OUT_OUTPUT].setVoltage(math::clamp(out, -12.f, 12.f));
		outputs[WET_OUTPUT].setVoltage(math::clamp(core.lastWet, -12.f, 12.f));

		// Tempo LED blinks once per delay time
		blink += args.sampleTime / std::max(seconds, 0.02f);
		if (blink >= 1.f)
			blink -= std::floor(blink);
		lights[TIME_LIGHT].setBrightnessSmooth(blink < 0.15f ? 1.f : 0.f, args.sampleTime);
	}
};


struct DlyWidget : JXMPModuleWidget {
	DlyWidget(Dly* const module) {
		setup(module, "JX-DLY", nvgRGB(0x4a, 0xc0, 0xa0), 10);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();
		const std::vector<st::Mark> percent =
			st::marks(0.f, 1.f, 0.1f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); });

		// DELAY: time follows 1 ms · 10^(4·knob)
		section(4.f, 20.5f, 42.8f, 46.f, "DELAY");
		label(math::Vec(25.4f, 25.f), "TIME", 7.5f);
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(32.f, 25.f)), module, Dly::TIME_LIGHT));
		knob(math::Vec(25.4f, 39.f), Dly::TIME_PARAM, 15.f, st::CAP_MAIN, 0.f, 1.f,
		     {{0.f, "1ms"}, {0.25f, ""}, {0.5f, "100ms"}, {0.75f, ""}, {1.f, "10s"}});
		label(math::Vec(25.4f, 52.5f), "FEEDBACK", 7.5f);
		knob(math::Vec(25.4f, 60.f), Dly::FEEDBACK_PARAM, 7.f, st::CAP_WHITE, 0.f, 1.f, {{0.f, "0"}, {0.5f, ""}, {1.f, "100"}});

		// SOUND
		section(4.f, 72.f, 42.8f, 45.5f, "SOUND");
		label(math::Vec(14.f, 77.f), "TONE", 7.5f);
		knob(math::Vec(14.f, 90.f), Dly::TONE_PARAM, 9.f, st::CAP_WHITE, -1.f, 1.f,
		     {{-1.f, "DARK"}, {-0.5f, ""}, {0.f, "0"}, {0.5f, ""}, {1.f, "THIN"}});
		label(math::Vec(36.8f, 77.f), "MIX", 7.5f);
		knob(math::Vec(36.8f, 90.f), Dly::MIX_PARAM, 9.f, st::CAP_WHITE, 0.f, 1.f,
		     {{0.f, "DRY"}, {0.25f, ""}, {0.5f, "50"}, {0.75f, ""}, {1.f, "WET"}});

		// Back: sockets
		for (int i = 0; i < Dly::NUM_INPUTS; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(13.f, 32.f + i * 15.f)), module, Dly::IN_INPUT + i));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(37.f, 32.f)), module, Dly::OUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(37.f, 47.f)), module, Dly::WET_OUTPUT));
	}
};

} // namespace


Model* modelJXMPDelay = createModel<Dly, DlyWidget>("Delay");

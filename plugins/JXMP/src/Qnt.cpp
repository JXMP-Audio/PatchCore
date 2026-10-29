/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-QNT: quantizer to ten scales on any root, with octave shift and a trigger on every note change.
 */

#include "JXMPWidget.hpp"
#include "dsp/Quant.hpp"

namespace {

struct Qnt : engine::Module {
	enum ParamIds { SCALE_PARAM, ROOT_PARAM, OCTAVE_PARAM, NUM_PARAMS };
	enum InputIds { IN_INPUT, ROOT_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_OUTPUT, TRIG_OUTPUT, NUM_OUTPUTS };
	enum LightIds { ENUMS(NOTE_LIGHT, 12), NUM_LIGHTS };

	float last[16] = {};
	dsp::PulseGenerator trig[16];
	dsp::ClockDivider lightDivider;

	Qnt() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		std::vector<std::string> scales;
		for (int s = 0; s < jxmp::NUM_SCALES; ++s)
			scales.push_back(jxmp::scaleName((jxmp::Scale) s));
		configSwitch(SCALE_PARAM, 0.f, jxmp::NUM_SCALES - 1, 1.f, "Scale", scales);
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
		configParam(OCTAVE_PARAM, -2.f, 2.f, 0.f, "Octave")->snapEnabled = true;
		configInput(IN_INPUT, "V/Oct")->description = "Pitch to quantize, 1 V per octave. Polyphonic.";
		configInput(ROOT_INPUT, "Root CV (1 V/oct)")->description = "Root note CV, 1 V per octave (added to the ROOT knob); transposes the scale.";
		configOutput(OUT_OUTPUT, "V/Oct")->description = "The input moved to the nearest note of the scale, plus OCTAVE.";
		configOutput(TRIG_OUTPUT, "Trig (note change)")->description = "A 1 ms trigger whenever the output note changes; can drive an envelope.";
		configBypass(IN_INPUT, OUT_OUTPUT);
		lightDivider.setDivision(256);
	}

	jxmp::Scale scale() {
		return (jxmp::Scale) math::clamp((int) std::round(params[SCALE_PARAM].getValue()), 0, jxmp::NUM_SCALES - 1);
	}

	int root() {
		int r = (int) std::round(params[ROOT_PARAM].getValue());
		if (inputs[ROOT_INPUT].isConnected())
			r += (int) std::lround(inputs[ROOT_INPUT].getVoltage() * 12.f);
		return (r % 12 + 12) % 12;
	}

	void process(const ProcessArgs& args) override {
		const jxmp::Scale s = scale();
		const int r = root();
		const float octave = params[OCTAVE_PARAM].getValue();
		const int channels = std::max(1, inputs[IN_INPUT].getChannels());
		for (int c = 0; c < channels; ++c) {
			const float q = jxmp::quantize(inputs[IN_INPUT].getVoltage(c), s, r) + octave;
			if (std::fabs(q - last[c]) > 1e-4f) {
				trig[c].trigger(1e-3f);
				last[c] = q;
			}
			outputs[OUT_OUTPUT].setVoltage(q, c);
			outputs[TRIG_OUTPUT].setVoltage(trig[c].process(args.sampleTime) ? 10.f : 0.f, c);
		}
		outputs[OUT_OUTPUT].setChannels(channels);
		outputs[TRIG_OUTPUT].setChannels(channels);

		if (lightDivider.process()) {
			const int playing = jxmp::noteOf(last[0]);
			for (int n = 0; n < 12; ++n)
				lights[NOTE_LIGHT + n].setBrightness(n == playing ? 1.f : jxmp::inScale(n, s, r) ? 0.18f : 0.f);
		}
	}
};


/** Shows the scale and root by name, like an 80s LED/LCD window. */
struct ScaleDisplay : widget::TransparentWidget {
	Qnt* module = nullptr;

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(vg, nvgRGB(0x12, 0x14, 0x13));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.8f);
		nvgStrokeColor(vg, nvgRGB(0x05, 0x05, 0x05));
		nvgStroke(vg);
		static const char* const roots[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		const std::string text = module ? std::string(roots[module->root()]) + " " + jxmp::scaleName(module->scale()) : "C Major";
		jxmp::style::text(vg, box.size.div(2), string::uppercase(text), 7.5f, nvgRGB(0xe0, 0x4a, 0xb0), jxmp::style::FONT_LABEL,
		                  NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.6f);
	}
};


struct QntWidget : JXMPModuleWidget {
	QntWidget(Qnt* const module) {
		setup(module, "JX-QNT", nvgRGB(0xc9, 0x4f, 0x9c), 8);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();

		// SCALE: name window, scale and root selectors
		section(3.f, 20.5f, 34.64f, 39.f, "SCALE");
		ScaleDisplay* const display = new ScaleDisplay;
		display->module = module;
		display->box.pos = mm2px(math::Vec(5.f, 24.5f));
		display->box.size = mm2px(math::Vec(30.64f, 6.f));
		addChild(display);
		label(math::Vec(11.f, 35.f), "SCALE", 7.f);
		std::vector<st::Mark> scaleMarks;
		for (int s = 0; s < jxmp::NUM_SCALES; ++s)
			scaleMarks.push_back({(float) s, ""});
		knob(math::Vec(11.f, 46.f), Qnt::SCALE_PARAM, 9.f, st::CAP_MAIN, 0.f, jxmp::NUM_SCALES - 1, scaleMarks);
		label(math::Vec(29.f, 35.f), "ROOT", 7.f);
		std::vector<st::Mark> rootMarks;
		for (int n = 0; n < 12; ++n)
			rootMarks.push_back({(float) n, n == 0 ? "C" : n == 11 ? "B" : ""});
		knob(math::Vec(29.f, 46.f), Qnt::ROOT_PARAM, 9.f, st::CAP_BLACK, 0.f, 11.f, rootMarks);

		// NOTES: a small keyboard of LEDs (scale notes dim, the playing note bright)
		section(3.f, 63.f, 34.64f, 22.f, "NOTES");
		const int whites[7] = {0, 2, 4, 5, 7, 9, 11};
		const int blacks[5] = {1, 3, 6, 8, 10};
		const float blackX[5] = {0.5f, 1.5f, 3.5f, 4.5f, 5.5f};
		for (int i = 0; i < 7; ++i)
			addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(7.f + i * 4.4f, 78.f)), module, Qnt::NOTE_LIGHT + whites[i]));
		for (int i = 0; i < 5; ++i)
			addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(7.f + blackX[i] * 4.4f, 71.f)), module, Qnt::NOTE_LIGHT + blacks[i]));

		// SHIFT
		section(3.f, 89.f, 34.64f, 28.f, "SHIFT");
		label(math::Vec(20.32f, 94.f), "OCTAVE", 7.f);
		knob(math::Vec(20.32f, 105.5f), Qnt::OCTAVE_PARAM, 9.f, st::CAP_BLACK, -2.f, 2.f,
		     {{-2.f, "-2"}, {-1.f, ""}, {0.f, "0"}, {1.f, ""}, {2.f, "+2"}});

		// Back: sockets
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(11.f, 32.f)), module, Qnt::IN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(11.f, 48.f)), module, Qnt::ROOT_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(29.f, 32.f)), module, Qnt::OUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(29.f, 48.f)), module, Qnt::TRIG_OUTPUT));
	}
};

} // namespace


Model* modelJXMPQuantizer = createModel<Qnt, QntWidget>("Quantizer");

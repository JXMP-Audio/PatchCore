/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-PLY: poly utilities. SPLIT a polyphonic cable into 8 mono ones, MERGE 8 mono cables into one polyphonic
 * (as many voices as the highest patched input), the voice COUNT of SPLIT IN as a number and as 1 V per voice,
 * and a MULT that copies one cable to four.
 */

#include "JXMPWidget.hpp"

namespace {

struct Ply : engine::Module {
	enum ParamIds { NUM_PARAMS };
	enum InputIds { SPLIT_INPUT, ENUMS(MERGE_INPUT, 8), MULT_INPUT, NUM_INPUTS };
	enum OutputIds { ENUMS(SPLIT_OUTPUT, 8), MERGE_OUTPUT, COUNT_OUTPUT, ENUMS(MULT_OUTPUT, 4), NUM_OUTPUTS };
	enum LightIds { NUM_LIGHTS };

	int voices = 0;  // read by the display

	Ply() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(SPLIT_INPUT, "Split in")->description = "A polyphonic cable (e.g. V/OCT or GATE from Host MIDI).";
		for (int i = 0; i < 8; ++i) {
			configOutput(SPLIT_OUTPUT + i, string::f("Voice %d", i + 1))->description = string::f("Voice %d of SPLIT IN, mono.", i + 1);
			configInput(MERGE_INPUT + i, string::f("Merge %d", i + 1))->description = string::f("Becomes voice %d of MERGE OUT.", i + 1);
		}
		configOutput(MERGE_OUTPUT, "Merge out")->description = "One polyphonic cable with as many voices as the highest patched MERGE input.";
		configOutput(COUNT_OUTPUT, "Count")->description = "The number of voices on SPLIT IN, 1 V per voice.";
		configInput(MULT_INPUT, "Mult in")->description = "Copied to the four MULT outputs (polyphony kept).";
		for (int i = 0; i < 4; ++i)
			configOutput(MULT_OUTPUT + i, string::f("Mult %d", i + 1))->description = "A copy of the MULT input (all its channels).";
	}

	void process(const ProcessArgs& args) override {
		voices = inputs[SPLIT_INPUT].getChannels();
		for (int i = 0; i < 8; ++i)
			outputs[SPLIT_OUTPUT + i].setVoltage(i < voices ? inputs[SPLIT_INPUT].getVoltage(i) : 0.f);
		outputs[COUNT_OUTPUT].setVoltage((float) voices);

		int highest = 0;
		for (int i = 0; i < 8; ++i)
			if (inputs[MERGE_INPUT + i].isConnected())
				highest = i + 1;
		outputs[MERGE_OUTPUT].setChannels(std::max(1, highest));
		for (int i = 0; i < std::max(1, highest); ++i)
			outputs[MERGE_OUTPUT].setVoltage(inputs[MERGE_INPUT + i].getVoltage(), i);

		const int mc = inputs[MULT_INPUT].getChannels();
		for (int k = 0; k < 4; ++k) {
			outputs[MULT_OUTPUT + k].setChannels(std::max(1, mc));
			for (int c = 0; c < std::max(1, mc); ++c)
				outputs[MULT_OUTPUT + k].setVoltage(inputs[MULT_INPUT].getVoltage(c), c);
		}
	}
};

/** The voice count of SPLIT IN on a green VFD: two digits and a bar of 16 voice marks */
struct VoiceDisplay : widget::TransparentWidget {
	Ply* module = nullptr;
	void draw(const DrawArgs& args) override {
		namespace st = jxmp::style;
		NVGcontext* const vg = args.vg;
		st::vfdWindow(vg, box.size);
		const int v = module ? module->voices : 4;
		const float h = box.size.y * 0.46f;
		st::vfdDigits(vg, box.size.x * 0.5f + h * 0.8f, box.size.y * 0.12f + h + mm2px(1.f), h, 2, v > 0 ? string::f("%d", v) : "-");
		// One mark per voice, lit up to the count
		const float x0 = mm2px(3.f), x1 = box.size.x - mm2px(3.f), y = box.size.y * 0.8f;
		const float dx = (x1 - x0) / 16.f;
		const NVGcolor lit = st::vfdLit();
		for (int i = 0; i < 16; ++i) {
			nvgBeginPath(vg);
			nvgRect(vg, x0 + i * dx + dx * 0.15f, y - mm2px(0.7f), dx * 0.7f, mm2px(1.4f));
			nvgFillColor(vg, i < v ? lit : nvgRGBAf(lit.r, lit.g, lit.b, 0.06f));
			nvgFill(vg);
		}
		st::vfdGlare(vg, box.size);
	}
};

struct PlyWidget : JXMPModuleWidget {
	PlyWidget(Ply* const module) {
		setup(module, "JX-PLY", nvgRGB(0x3f, 0x5f, 0xa8), 10);
		styled();
		const float w = 50.8f;
		section(3.f, 20.5f, w - 6.f, 30.f, "VOICES ON SPLIT IN");
		VoiceDisplay* const d = new VoiceDisplay;
		d->module = module;
		d->box = math::Rect(mm2px(math::Vec(w / 2 - 15.f, 25.f)), mm2px(math::Vec(30.f, 20.f)));
		addChild(d);
		section(3.f, 55.f, w - 6.f, 62.f, "ON THE BACK");
		const char* lines[6] = {"SPLIT: 1 POLY > 8 MONO", "MERGE: 8 MONO > 1 POLY", "COUNT: 1 V PER VOICE", "MULT: 1 > 4",
		                        "e.g. HOST MIDI V/OCT", "> 8 VOICES OF JX-OSC"};
		for (int i = 0; i < 6; ++i)
			label(math::Vec(w / 2, 63.f + i * 8.f + (i >= 4 ? 4.f : 0.f)), lines[i], 5.5f);

		// Back: four columns
		const float xs[4] = {8.f, 19.6f, 31.2f, 42.8f};
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[0], 30.f)), module, Ply::SPLIT_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[1], 30.f)), module, Ply::COUNT_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[2], 30.f)), module, Ply::MULT_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[3], 30.f)), module, Ply::MERGE_OUTPUT));
		for (int i = 0; i < 8; ++i) {
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[i % 2], 45.f + (i / 2) * 14.5f)), module, Ply::SPLIT_OUTPUT + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[2 + i % 2], 45.f + (i / 2) * 14.5f)), module, Ply::MERGE_INPUT + i));
		}
		for (int k = 0; k < 4; ++k)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[k], 104.f)), module, Ply::MULT_OUTPUT + k));
	}
};

} // namespace

Model* modelJXMPPoly = createModel<Ply, PlyWidget>("Poly");

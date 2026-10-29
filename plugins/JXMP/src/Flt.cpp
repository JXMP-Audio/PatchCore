/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-FLT: ladder VCF with LP/BP/HP/notch, 12/24 dB, keytracking and an envelope input.
 * Ladder core and cutoff/drive/resonance handling based on VCV Fundamental's VCF (GPL-3.0-or-later).
 */

#include "JXMPWidget.hpp"
#include "dsp/Ladder.hpp"
#include "dsp/LadderTap.hpp"

namespace {

using simd::float_4;

struct Flt : engine::Module {
	enum ParamIds {
		FREQ_PARAM, RES_PARAM, DRIVE_PARAM, FREQ_CV_PARAM, RES_CV_PARAM, DRIVE_CV_PARAM,
		MODE_PARAM, SLOPE_PARAM, KEYTRACK_PARAM, ENV_AMT_PARAM,
		NUM_PARAMS
	};
	enum InputIds { IN_INPUT, VOCT_INPUT, ENV_INPUT, FREQ_INPUT, RES_INPUT, DRIVE_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_OUTPUT, LPF_OUTPUT, HPF_OUTPUT, NUM_OUTPUTS };
	enum LightIds { NUM_LIGHTS };

	jxmp::LadderFilter<float_4> filters[4];

	Flt() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// Same cutoff law as Fundamental: freq = C4 * 2^(10 * param - 5)
		const float minFreq = (std::log2(dsp::FREQ_C4 / 8000.f) + 5) / 10;
		const float maxFreq = (std::log2(8000.f / dsp::FREQ_C4) + 5) / 10;
		configParam(FREQ_PARAM, minFreq, maxFreq, 0.5f, "Cutoff frequency", " Hz", std::pow(2, 10.f), dsp::FREQ_C4 / std::pow(2, 5.f));
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, -1.f, 1.f, 0.f, "Drive", "%", 0, 100, 100);
		configParam(FREQ_CV_PARAM, -1.f, 1.f, 0.f, "Cutoff CV", "%", 0.f, 100.f);
		configParam(RES_CV_PARAM, -1.f, 1.f, 0.f, "Resonance CV", "%", 0.f, 100.f);
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 0.f, "Drive CV", "%", 0, 100);
		configSwitch(MODE_PARAM, 0.f, 3.f, 0.f, "Mode", {"Lowpass", "Bandpass", "Highpass", "Notch"});
		configSwitch(SLOPE_PARAM, 0.f, 1.f, 1.f, "Slope", {"12 dB", "24 dB"});
		configParam(KEYTRACK_PARAM, 0.f, 1.f, 0.f, "Keytracking", "%", 0.f, 100.f);
		configParam(ENV_AMT_PARAM, -1.f, 1.f, 0.f, "Envelope amount", "%", 0.f, 100.f);
		configInput(IN_INPUT, "In")->description = "Audio to filter. Polyphonic.";
		configInput(VOCT_INPUT, "V/Oct")->description = "Pitch, 1 V per octave, for keytracking: the cutoff follows the notes by the KEYTRACK amount.";
		configInput(ENV_INPUT, "Env")->description = "Envelope or other modulation for the cutoff; the ENV knob sets how far (±5 octaves for 10 V at full).";
		configInput(FREQ_INPUT, "Freq CV")->description = "Cutoff CV in octaves per volt, scaled by the FREQ CV knob.";
		configInput(RES_INPUT, "Res CV")->description = "Resonance CV (10 V = full range), scaled by the RES CV knob.";
		configInput(DRIVE_INPUT, "Drive CV")->description = "Drive CV (10 V = full range), scaled by the DRIVE CV knob.";
		configOutput(OUT_OUTPUT, "Out")->description = "Filtered signal in the selected TYPE (LP, BP, HP, notch) and SLOPE.";
		configOutput(LPF_OUTPUT, "LPF")->description = "Always the 24 dB lowpass, whatever TYPE is set.";
		configOutput(HPF_OUTPUT, "HPF")->description = "Always the 24 dB highpass, whatever TYPE is set.";
		configBypass(IN_INPUT, OUT_OUTPUT);
		configBypass(IN_INPUT, LPF_OUTPUT);
		configBypass(IN_INPUT, HPF_OUTPUT);
	}

	void onReset() override {
		for (int i = 0; i < 4; i++)
			filters[i].reset();
	}

	void process(const ProcessArgs& args) override {
		const float freqParam = params[FREQ_PARAM].getValue() * 10.f - 5.f;
		const float resParam = params[RES_PARAM].getValue();
		const float driveParam = params[DRIVE_PARAM].getValue();
		const float freqCvParam = params[FREQ_CV_PARAM].getValue();
		const float resCvParam = params[RES_CV_PARAM].getValue();
		const float driveCvParam = params[DRIVE_CV_PARAM].getValue();
		const jxmp::FilterMode mode = (jxmp::FilterMode) (int) std::round(params[MODE_PARAM].getValue());
		const bool steep = params[SLOPE_PARAM].getValue() > 0.5f;
		const float keytrack = params[KEYTRACK_PARAM].getValue();
		const float envAmt = params[ENV_AMT_PARAM].getValue();

		const int channels = std::max(1, inputs[IN_INPUT].getChannels());
		for (int c = 0; c < channels; c += 4) {
			jxmp::LadderFilter<float_4>& filter = filters[c / 4];

			float_4 input = inputs[IN_INPUT].getVoltageSimd<float_4>(c) / 5.f;
			float_4 drive = driveParam + inputs[DRIVE_INPUT].getPolyVoltageSimd<float_4>(c) / 10.f * driveCvParam;
			drive = simd::clamp(drive, -1.f, 1.f);
			input *= simd::pow(1.f + drive, 5);
			// -120 dB noise bootstraps self-oscillation
			input += 1e-6f * (2.f * random::uniform() - 1.f);

			float_4 resonance = resParam + inputs[RES_INPUT].getPolyVoltageSimd<float_4>(c) / 10.f * resCvParam;
			resonance = simd::clamp(resonance, 0.f, 1.f);
			filter.resonance = simd::pow(resonance, 2) * 10.f;

			// Cutoff: knob + CV + keytracking (octaves per volt) + envelope (±5 octaves for 10 V at ±100 %)
			float_4 pitch = freqParam + inputs[FREQ_INPUT].getPolyVoltageSimd<float_4>(c) * freqCvParam;
			pitch += keytrack * inputs[VOCT_INPUT].getPolyVoltageSimd<float_4>(c);
			pitch += envAmt * inputs[ENV_INPUT].getPolyVoltageSimd<float_4>(c) / 10.f * 5.f;
			float_4 cutoff = dsp::FREQ_C4 * simd::pow(2.f, pitch);
			cutoff = simd::clamp(cutoff, 1.f, args.sampleRate * 0.18f);
			filter.setCutoff(cutoff);

			filter.process(input, args.sampleTime);

			const float_4 u = filter.input - filter.resonance * filter.state[3];
			float_4 out = 5.f * jxmp::clip(jxmp::ladderTap<float_4>(mode, steep, u, filter.state));
			float_4 lpf = 5.f * filter.lowpass();
			float_4 hpf = 5.f * filter.highpass();

			bool finite = true;
			for (int l = 0; l < 4; ++l)
				finite = finite && std::isfinite(out[l]) && std::isfinite(lpf[l]) && std::isfinite(hpf[l]);
			if (!finite) {
				// Never let a bad state stick
				filter.reset();
				out = lpf = hpf = 0.f;
			}
			outputs[OUT_OUTPUT].setVoltageSimd(simd::clamp(out, -12.f, 12.f), c);
			outputs[LPF_OUTPUT].setVoltageSimd(simd::clamp(lpf, -12.f, 12.f), c);
			outputs[HPF_OUTPUT].setVoltageSimd(simd::clamp(hpf, -12.f, 12.f), c);
		}
		for (int i = 0; i < NUM_OUTPUTS; ++i)
			outputs[i].setChannels(channels);
	}
};


struct FltWidget : JXMPModuleWidget {
	FltWidget(Flt* const module) {
		setup(module, "JX-FLT", nvgRGB(0x3a, 0x8d, 0xde), 14);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();
		const float nameSize = 7.5f;
		const float minFreq = (std::log2(dsp::FREQ_C4 / 8000.f) + 5) / 10;
		const float maxFreq = (std::log2(8000.f / dsp::FREQ_C4) + 5) / 10;

		// FILTER
		section(4.f, 20.5f, 63.12f, 33.f, "FILTER");
		const float fName = 25.f, fRow = 41.5f;
		// Cutoff: a tick per octave (param = (log2(f / C4) + 5) / 10), labelled at both ends and C4
		std::vector<st::Mark> cutoffMarks = {{minFreq, "8"}};
		for (int k = -4; k <= 4; ++k)
			cutoffMarks.push_back({(k + 5) / 10.f, k == 0 ? "262" : ""});
		cutoffMarks.push_back({maxFreq, "8k"});
		label(math::Vec(17.f, fName), "CUTOFF", nameSize);
		knob(math::Vec(17.f, fRow), Flt::FREQ_PARAM, 15.f, st::CAP_MAIN, minFreq, maxFreq, cutoffMarks);
		label(math::Vec(39.f, fName), "RESONANCE", nameSize);
		knob(math::Vec(39.f, fRow), Flt::RES_PARAM, 10.f, st::CAP_MAIN, 0.f, 1.f,
		     st::marks(0.f, 1.f, 0.1f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); }));
		label(math::Vec(57.f, fName), "DRIVE", nameSize);
		knob(math::Vec(57.f, fRow), Flt::DRIVE_PARAM, 9.f, st::CAP_WHITE, -1.f, 1.f,
		     st::marks(-1.f, 1.f, 0.2f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f + 100.f)); }));

		// MODE
		section(4.f, 57.f, 63.12f, 31.f, "MODE");
		const float mName = 62.f, mRow = 76.f;
		const math::Vec modePos(17.f, mRow);
		label(math::Vec(17.f, mName), "TYPE", nameSize);
		knob(modePos, Flt::MODE_PARAM, 10.f, st::CAP_BLACK, 0.f, 3.f, {{0.f, ""}, {1.f, ""}, {2.f, ""}, {3.f, ""}});
		const st::Icon modes[4] = {st::ICON_LP, st::ICON_BP, st::ICON_HP, st::ICON_NOTCH};
		for (int m = 0; m < 4; ++m) {
			const float a = -0.83f * M_PI + 1.66f * M_PI * m / 3.f - M_PI / 2;
			icon(modes[m], modePos.plus(math::Vec(std::cos(a), std::sin(a)).mult(9.f)), 3.6f);
		}
		label(math::Vec(37.f, mName), "SLOPE", nameSize);
		toggle(math::Vec(37.f, mRow), Flt::SLOPE_PARAM, 2, {"24 DB", "12 DB"});
		label(math::Vec(56.f, mName), "KEYTRACK", nameSize);
		knob(math::Vec(56.f, mRow), Flt::KEYTRACK_PARAM, 9.f, st::CAP_GREY, 0.f, 1.f,
		     st::marks(0.f, 1.f, 0.1f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); }));

		// MODULATION
		section(4.f, 91.5f, 63.12f, 26.f, "MODULATION");
		const float cName = 96.5f, cRow = 108.5f;
		const char* cvNames[3] = {"FREQ CV", "RES CV", "DRIVE CV"};
		label(math::Vec(13.f, cName), "ENV", nameSize);
		knob(math::Vec(13.f, cRow), Flt::ENV_AMT_PARAM, 9.f, st::CAP_GREY, -1.f, 1.f, st::bipolar());
		for (int i = 0; i < 3; ++i) {
			const float x = 29.f + i * 14.f;
			label(math::Vec(x, cName), cvNames[i], 6.5f);
			knob(math::Vec(x, cRow), Flt::FREQ_CV_PARAM + i, 7.f, st::CAP_GREY, -1.f, 1.f, st::bipolar());
		}

		// Back: sockets
		for (int i = 0; i < Flt::NUM_INPUTS; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(15.f, 30.f + i * 15.f)), module, Flt::IN_INPUT + i));
		for (int i = 0; i < Flt::NUM_OUTPUTS; ++i)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(48.f, 30.f + i * 15.f)), module, Flt::OUT_OUTPUT + i));
	}
};

} // namespace


Model* modelJXMPVCF = createModel<Flt, FltWidget>("VCF");

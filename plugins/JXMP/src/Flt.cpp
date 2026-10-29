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
		configInput(IN_INPUT, "In");
		configInput(VOCT_INPUT, "V/Oct");
		configInput(ENV_INPUT, "Env");
		configInput(FREQ_INPUT, "Freq CV");
		configInput(RES_INPUT, "Res CV");
		configInput(DRIVE_INPUT, "Drive CV");
		configOutput(OUT_OUTPUT, "Out");
		configOutput(LPF_OUTPUT, "LPF");
		configOutput(HPF_OUTPUT, "HPF");
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
		setup(module, "JX-FLT", nvgRGB(0x3a, 0x8d, 0xde), 10);

		// Front: controls
		label(math::Vec(16.f, 22.f), "CUTOFF");
		addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(math::Vec(16.f, 34.f)), module, Flt::FREQ_PARAM));
		label(math::Vec(38.f, 22.f), "RES");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(math::Vec(38.f, 30.f)), module, Flt::RES_PARAM));
		label(math::Vec(38.f, 39.f), "DRIVE", 6.f);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(math::Vec(38.f, 47.f)), module, Flt::DRIVE_PARAM));

		const float cvY = 62.f;
		const char* cvNames[3] = {"FREQ", "RES", "DRIVE"};
		for (int i = 0; i < 3; ++i) {
			label(math::Vec(10.f + i * 15.f, cvY - 6.f), cvNames[i], 6.f);
			addParam(createParamCentered<Trimpot>(mm2px(math::Vec(10.f + i * 15.f, cvY)), module, Flt::FREQ_CV_PARAM + i));
		}
		label(math::Vec(25.4f, cvY + 6.5f), "CV AMOUNT", 6.f);

		label(math::Vec(13.f, 78.f), "KEYTRACK", 6.f);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(math::Vec(13.f, 86.f)), module, Flt::KEYTRACK_PARAM));
		label(math::Vec(37.f, 78.f), "ENV", 6.f);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(math::Vec(37.f, 86.f)), module, Flt::ENV_AMT_PARAM));

		label(math::Vec(16.f, 99.f), "LP BP HP N", 6.f);
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(math::Vec(16.f, 108.f)), module, Flt::MODE_PARAM));
		label(math::Vec(37.f, 99.f), "12 · 24", 6.f);
		addParam(createParamCentered<CKSS>(mm2px(math::Vec(37.f, 108.f)), module, Flt::SLOPE_PARAM));

		// Back: sockets
		for (int i = 0; i < Flt::NUM_INPUTS; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(12.f, 30.f + i * 14.f)), module, Flt::IN_INPUT + i));
		for (int i = 0; i < Flt::NUM_OUTPUTS; ++i)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(38.f, 30.f + i * 14.f)), module, Flt::OUT_OUTPUT + i));
	}
};

} // namespace


Model* modelJXMPVCF = createModel<Flt, FltWidget>("VCF");

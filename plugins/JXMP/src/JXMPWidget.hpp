/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "plugin.hpp"
#include "Serial.hpp"

#include <string>
#include <vector>

/** Uniform JXMP panel decoration: colour stripe, title, footer and control labels. Text is drawn in code
because nanosvg does not render SVG text. */
struct JXMPPanelDecor : widget::TransparentWidget {
	struct Label {
		math::Vec pos;
		std::string text;
		float size;
	};

	std::string title;
	NVGcolor stripe = nvgRGB(0x8a, 0x8f, 0x98);
	std::vector<Label> labels;

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		nvgBeginPath(vg);
		nvgRect(vg, 0, mm2px(14.f), box.size.x, mm2px(3.f));
		nvgFillColor(vg, stripe);
		nvgFill(vg);

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font)
			return;
		nvgFontFaceId(vg, font->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		nvgFontSize(vg, 11.f);
		nvgTextLetterSpacing(vg, 1.f);
		nvgFillColor(vg, nvgRGB(0xe8, 0xea, 0xee));
		nvgText(vg, box.size.x / 2, mm2px(8.f), title.c_str(), nullptr);

		nvgFontSize(vg, 7.f);
		nvgTextLetterSpacing(vg, 1.5f);
		nvgFillColor(vg, nvgRGB(0x8a, 0x8f, 0x98));
		nvgText(vg, box.size.x / 2, mm2px(123.f), "JXMP AUDIO", nullptr);

		nvgTextLetterSpacing(vg, 0.5f);
		nvgFillColor(vg, nvgRGB(0xc8, 0xcc, 0xd2));
		for (const Label& l : labels) {
			nvgFontSize(vg, l.size);
			nvgText(vg, l.pos.x, l.pos.y, l.text.c_str(), nullptr);
		}
	}
};


/** Base of the JXMP voice modules: uniform panel, every port on the back, serial plate. */
struct JXMPModuleWidget : app::ModuleWidget, patchcore::SidedModuleWidget {
	std::string code;
	JXMPPanelDecor* decor = nullptr;

	void setup(engine::Module* const m, const char* const jxCode, const NVGcolor stripe, const int hp) {
		setModule(m);
		code = jxCode;
		setPanel(createPanel(asset::plugin(pluginInstance, string::f("res/Panel%dHP.svg", hp))));
		decor = new JXMPPanelDecor;
		decor->box.size = box.size;
		decor->title = code;
		decor->stripe = stripe;
		addChild(decor);
	}

	/** Adds a label centred at a position in mm. */
	void label(const math::Vec mm, const char* const text, const float size = 7.f) {
		decor->labels.push_back({mm2px(mm), text, size});
	}

	std::string serialLabel() const override {
		return code + " · S/N " + jxmp::serialNumber(module ? module->id : -1);
	}
};

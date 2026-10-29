/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "plugin.hpp"
#include "Serial.hpp"
#include "JXMPStyle.hpp"

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

	// JXMP style (see JXMPStyle.hpp); modules opt in with JXMPModuleWidget::styled()
	struct Section {
		math::Rect rect;  // px
		std::string title;
	};
	struct Scale {
		math::Vec center;  // px
		float radius;      // px, the knob radius
		std::vector<jxmp::style::Mark> marks;
	};
	struct IconMark {
		jxmp::style::Icon type;
		math::Vec pos;  // px
		float width;    // px
	};
	bool styled = false;
	std::vector<Section> sections;
	std::vector<Scale> scales;
	std::vector<IconMark> icons;

	void drawStyled(NVGcontext* const vg) {
		namespace st = jxmp::style;
		const float w = box.size.x, h = box.size.y;

		// Anthracite plate with darker top and bottom bands
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, w, h);
		nvgFillColor(vg, st::panel());
		nvgFill(vg);
		for (const float y : {0.f, h - mm2px(9.f)}) {
			nvgBeginPath(vg);
			nvgRect(vg, 0, y, w, y == 0.f ? mm2px(12.5f) : mm2px(9.f));
			nvgFillColor(vg, st::panelDark());
			nvgFill(vg);
		}

		// Double stripe in the module colour, 80s style
		nvgBeginPath(vg);
		nvgRect(vg, 0, mm2px(12.5f), w, mm2px(2.4f));
		nvgFillColor(vg, stripe);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, 0, mm2px(15.5f), w, mm2px(0.6f));
		nvgFillColor(vg, stripe);
		nvgFill(vg);

		st::text(vg, math::Vec(w / 2, mm2px(6.6f)), title, 15.f, st::legend(), true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 1.5f);
		st::text(vg, math::Vec(w / 2, h - mm2px(4.5f)), "JXMP AUDIO", 9.f, st::legendDim(), true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 2.f);

		// Framed sections with the title cut into the top line
		for (const Section& sec : sections) {
			const math::Rect& r = sec.rect;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, RECT_ARGS(r), mm2px(1.2f));
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, st::line());
			nvgStroke(vg);
			if (!sec.title.empty()) {
				std::shared_ptr<window::Font> f = st::font(true);
				float tw = 30.f;
				if (f) {
					nvgFontFaceId(vg, f->handle);
					nvgFontSize(vg, 9.f);
					nvgTextLetterSpacing(vg, 1.f);
					float bounds[4];
					tw = nvgTextBounds(vg, 0, 0, sec.title.c_str(), nullptr, bounds);
				}
				const float tx = r.pos.x + mm2px(3.f);
				nvgBeginPath(vg);
				nvgRect(vg, tx - 3.f, r.pos.y - 5.f, tw + 6.f, 10.f);
				nvgFillColor(vg, st::panel());
				nvgFill(vg);
				st::text(vg, math::Vec(tx, r.pos.y), sec.title, 9.f, stripe, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 1.f);
			}
		}

		// Knob scales: a tick at every mark, longer with a label at labelled marks
		const float a0 = -0.83f * M_PI, a1 = 0.83f * M_PI;
		for (const Scale& sc : scales) {
			for (const jxmp::style::Mark& m : sc.marks) {
				const float a = a0 + (a1 - a0) * m.t - M_PI / 2;
				const bool major = !m.label.empty();
				const float r0 = sc.radius + mm2px(0.6f);
				const float r1 = r0 + mm2px(major ? 1.2f : 0.7f);
				nvgBeginPath(vg);
				nvgMoveTo(vg, sc.center.x + std::cos(a) * r0, sc.center.y + std::sin(a) * r0);
				nvgLineTo(vg, sc.center.x + std::cos(a) * r1, sc.center.y + std::sin(a) * r1);
				nvgStrokeWidth(vg, major ? 1.f : 0.7f);
				nvgStrokeColor(vg, major ? st::legend() : st::legendDim());
				nvgStroke(vg);
				if (major) {
					const float rn = r1 + mm2px(1.9f);
					st::text(vg, math::Vec(sc.center.x + std::cos(a) * rn, sc.center.y + std::sin(a) * rn), m.label, 7.f,
					         st::legendDim(), false, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.2f);
				}
			}
		}

		for (const IconMark& ic : icons)
			st::icon(vg, ic.type, ic.pos, ic.width, st::legend());

		for (const Label& l : labels)
			st::text(vg, l.pos, l.text, l.size + 2.f, st::legend(), true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		if (styled) {
			drawStyled(vg);
			return;
		}
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

	/** Switches this module to the JXMP style and adds the panel screws. */
	void styled() {
		decor->styled = true;
		addChild(createWidget<ScrewBlack>(math::Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(math::Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		if (box.size.x > 8 * RACK_GRID_WIDTH) {
			addChild(createWidget<ScrewBlack>(math::Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
			addChild(createWidget<ScrewBlack>(math::Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		}
	}

	/** Framed section in mm with a title in the module colour. */
	void section(float x, float y, float w, float h, const char* title) {
		decor->sections.push_back({math::Rect(mm2px(math::Vec(x, y)), mm2px(math::Vec(w, h))), title});
	}

	/** Styled knob centred at `mm`. `marks` are scale positions in the parameter's own values, between
	`minValue` and `maxValue` (the range given to configParam), so the scale shows what the knob really does. */
	jxmp::style::Knob* knob(math::Vec mm, int paramId, float diameterMm, jxmp::style::Cap cap, float minValue, float maxValue,
	                        const std::vector<jxmp::style::Mark>& marks) {
		jxmp::style::Knob* const k = createParam<jxmp::style::Knob>(math::Vec(), module, paramId);
		k->setDiameter(diameterMm);
		k->box.pos = mm2px(mm).minus(k->box.size.div(2));
		switch (cap) {
		case jxmp::style::CAP_MAIN: k->cap = decor->stripe; break;
		case jxmp::style::CAP_WHITE: k->cap = nvgRGB(0xe8, 0xe6, 0xe0); break;
		case jxmp::style::CAP_GREY: k->cap = nvgRGB(0x7c, 0x80, 0x88); break;
		default: k->cap = nvgRGB(0x26, 0x27, 0x2a); break;
		}
		addParam(k);
		std::vector<jxmp::style::Mark> norm = marks;
		for (jxmp::style::Mark& m : norm)
			m.t = math::clamp((m.t - minValue) / (maxValue - minValue), 0.f, 1.f);
		decor->scales.push_back({mm2px(mm), k->box.size.x / 2, norm});
		return k;
	}

	void icon(jxmp::style::Icon type, math::Vec mm, float widthMm = 4.f) {
		decor->icons.push_back({type, mm2px(mm), mm2px(widthMm)});
	}

	/** Adds a label centred at a position in mm. */
	void label(const math::Vec mm, const char* const text, const float size = 7.f) {
		decor->labels.push_back({mm2px(mm), text, size});
	}

	std::string serialLabel() const override {
		return code + " · S/N " + jxmp::serialNumber(module ? module->id : -1);
	}
};

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JXMP panel style: 80s-inspired anthracite panels, double colour stripe, framed sections,
 * knobs with coloured caps and scales, waveform icons. Everything is drawn in NanoVG.
 */

#pragma once

#include "plugin.hpp"

#include <string>
#include <vector>

namespace jxmp {
namespace style {

inline NVGcolor panel() { return nvgRGB(0x2a, 0x2b, 0x2f); }
inline NVGcolor panelDark() { return nvgRGB(0x1e, 0x1f, 0x22); }
inline NVGcolor line() { return nvgRGB(0x5c, 0x5f, 0x66); }
inline NVGcolor legend() { return nvgRGB(0xe6, 0xe7, 0xea); }
inline NVGcolor legendDim() { return nvgRGB(0x9a, 0x9d, 0xa4); }

/** Cap colours by function, the same in every module */
enum Cap {
	CAP_MAIN,    // the module colour: the main parameter
	CAP_WHITE,   // times, levels, amounts of sound
	CAP_GREY,    // CV / modulation amounts
	CAP_BLACK,   // selectors and stepped knobs
};

inline std::shared_ptr<window::Font> font(bool semibold = true) {
	return APP->window->loadFont(asset::plugin(pluginInstance,
		semibold ? "res/fonts/BarlowCondensed-SemiBold.ttf" : "res/fonts/BarlowCondensed-Medium.ttf"));
}

inline void text(NVGcontext* vg, math::Vec pos, const std::string& s, float size, NVGcolor color, bool semibold = true,
                 int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, float spacing = 0.6f) {
	std::shared_ptr<window::Font> f = font(semibold);
	if (!f)
		return;
	nvgFontFaceId(vg, f->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, spacing);
	nvgTextAlign(vg, align);
	nvgFillColor(vg, color);
	nvgText(vg, pos.x, pos.y, s.c_str(), nullptr);
}


/** Knob with a ridged black body, a coloured cap and a white pointer. `diameterMm` sets its size. */
struct Knob : app::Knob {
	float diameterMm = 10.f;
	NVGcolor cap = nvgRGB(0xee, 0xee, 0xee);

	Knob() {
		minAngle = -0.83f * M_PI;
		maxAngle = 0.83f * M_PI;
	}

	void setDiameter(float mm) {
		diameterMm = mm;
		box.size = mm2px(math::Vec(mm, mm));
	}

	float angle() {
		engine::ParamQuantity* const pq = getParamQuantity();
		const float v = pq ? pq->getScaledValue() : 0.5f;
		return math::rescale(v, 0.f, 1.f, minAngle, maxAngle);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const math::Vec c = box.size.div(2);
		const float r = box.size.x / 2;

		// Drop shadow
		nvgBeginPath(vg);
		nvgCircle(vg, c.x + r * 0.08f, c.y + r * 0.16f, r * 1.02f);
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x + r * 0.08f, c.y + r * 0.16f, r * 0.7f, r * 1.1f, nvgRGBAf(0, 0, 0, 0.55f), nvgRGBAf(0, 0, 0, 0)));
		nvgFill(vg);

		// Ridged body
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r);
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y - r, c.x, c.y + r, nvgRGB(0x34, 0x35, 0x39), nvgRGB(0x0c, 0x0c, 0x0e)));
		nvgFill(vg);
		const int ridges = std::max(12, (int) (diameterMm * 2.2f));
		nvgStrokeWidth(vg, std::max(0.6f, r * 0.05f));
		for (int i = 0; i < ridges; ++i) {
			const float a = i * 2.f * M_PI / ridges;
			nvgBeginPath(vg);
			nvgMoveTo(vg, c.x + std::cos(a) * r * 0.8f, c.y + std::sin(a) * r * 0.8f);
			nvgLineTo(vg, c.x + std::cos(a) * r * 0.98f, c.y + std::sin(a) * r * 0.98f);
			nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, std::sin(a) < 0 ? 0.10f : 0.04f));
			nvgStroke(vg);
		}

		// Cap
		const float cr = r * 0.72f;
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, cr);
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y - cr, c.x, c.y + cr, color::plus(cap, nvgRGBf(0.12f, 0.12f, 0.12f)), color::mult(cap, 0.78f)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.6f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.5f));
		nvgStroke(vg);

		// Pointer
		const float a = angle() - M_PI / 2;
		const bool lightCap = (cap.r + cap.g + cap.b) / 3.f > 0.6f;
		nvgBeginPath(vg);
		nvgMoveTo(vg, c.x + std::cos(a) * r * 0.2f, c.y + std::sin(a) * r * 0.2f);
		nvgLineTo(vg, c.x + std::cos(a) * r * 0.92f, c.y + std::sin(a) * r * 0.92f);
		nvgStrokeWidth(vg, std::max(1.2f, r * 0.13f));
		nvgLineCap(vg, NVG_ROUND);
		nvgStrokeColor(vg, lightCap ? nvgRGB(0x18, 0x18, 0x1a) : nvgRGB(0xf4, 0xf4, 0xf4));
		nvgStroke(vg);

		app::Knob::draw(args);
	}
};


/** Waveform and filter icons for legends */
enum Icon { ICON_SIN, ICON_TRI, ICON_SAW, ICON_SQR, ICON_LP, ICON_BP, ICON_HP, ICON_NOTCH, ICON_ENV };

inline void icon(NVGcontext* vg, Icon type, math::Vec c, float w, NVGcolor color) {
	const float h = w * 0.6f;
	const float x0 = c.x - w / 2, x1 = c.x + w / 2, y0 = c.y - h / 2, y1 = c.y + h / 2;
	nvgBeginPath(vg);
	switch (type) {
	case ICON_SIN:
		for (int i = 0; i <= 24; ++i) {
			const float t = i / 24.f;
			const float x = x0 + t * w, y = c.y - std::sin(t * 2 * M_PI) * h / 2;
			if (i == 0) nvgMoveTo(vg, x, y); else nvgLineTo(vg, x, y);
		}
		break;
	case ICON_TRI:
		nvgMoveTo(vg, x0, c.y); nvgLineTo(vg, x0 + w * 0.25f, y0); nvgLineTo(vg, x0 + w * 0.75f, y1); nvgLineTo(vg, x1, c.y);
		break;
	case ICON_SAW:
		nvgMoveTo(vg, x0, y1); nvgLineTo(vg, c.x, y0); nvgLineTo(vg, c.x, y1); nvgLineTo(vg, x1, y0);
		break;
	case ICON_SQR:
		nvgMoveTo(vg, x0, y1); nvgLineTo(vg, x0, y0); nvgLineTo(vg, c.x, y0); nvgLineTo(vg, c.x, y1); nvgLineTo(vg, x1, y1); nvgLineTo(vg, x1, y0);
		break;
	case ICON_LP:
		nvgMoveTo(vg, x0, y0 + h * 0.3f); nvgLineTo(vg, c.x, y0 + h * 0.3f); nvgBezierTo(vg, c.x + w * 0.2f, y0 + h * 0.3f, c.x + w * 0.25f, y1, x1, y1);
		break;
	case ICON_HP:
		nvgMoveTo(vg, x0, y1); nvgBezierTo(vg, c.x - w * 0.25f, y1, c.x - w * 0.2f, y0 + h * 0.3f, c.x, y0 + h * 0.3f); nvgLineTo(vg, x1, y0 + h * 0.3f);
		break;
	case ICON_BP:
		nvgMoveTo(vg, x0, y1); nvgBezierTo(vg, c.x - w * 0.15f, y1, c.x - w * 0.1f, y0, c.x, y0); nvgBezierTo(vg, c.x + w * 0.1f, y0, c.x + w * 0.15f, y1, x1, y1);
		break;
	case ICON_NOTCH:
		nvgMoveTo(vg, x0, y0); nvgBezierTo(vg, c.x - w * 0.15f, y0, c.x - w * 0.1f, y1, c.x, y1); nvgBezierTo(vg, c.x + w * 0.1f, y1, c.x + w * 0.15f, y0, x1, y0);
		break;
	case ICON_ENV:
		nvgMoveTo(vg, x0, y1); nvgLineTo(vg, x0 + w * 0.2f, y0); nvgLineTo(vg, x0 + w * 0.4f, c.y); nvgLineTo(vg, x0 + w * 0.7f, c.y); nvgLineTo(vg, x1, y1);
		break;
	}
	nvgStrokeWidth(vg, 1.1f);
	nvgLineCap(vg, NVG_ROUND);
	nvgLineJoin(vg, NVG_ROUND);
	nvgStrokeColor(vg, color);
	nvgStroke(vg);
}

} // namespace style
} // namespace jxmp

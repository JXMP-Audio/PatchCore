/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JXMP panel style: 80s-inspired anthracite panels, double colour stripe, framed sections,
 * knobs with coloured caps and scales, waveform icons. Everything is drawn in NanoVG.
 */

#pragma once

#include "plugin.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace jxmp {
namespace style {

inline NVGcolor panel() { return nvgRGB(0x2a, 0x2b, 0x2f); }
inline NVGcolor panelDark() { return nvgRGB(0x1e, 0x1f, 0x22); }
inline NVGcolor line() { return nvgRGB(0x5c, 0x5f, 0x66); }
inline NVGcolor legend() { return nvgRGB(0xe6, 0xe7, 0xea); }
inline NVGcolor legendDim() { return nvgRGB(0x9a, 0x9d, 0xa4); }

/** Pointer colours by function, the same in every module (caps are polished aluminium) */
enum Cap {
	CAP_MAIN,    // the module colour: the main parameter
	CAP_WHITE,   // times, levels, amounts of sound: black pointer
	CAP_GREY,    // CV / modulation amounts: grey pointer
	CAP_BLACK,   // selectors and stepped knobs: black cap, white pointer
};

enum FontId { FONT_LABEL, FONT_NUMBER, FONT_TITLE };

inline std::shared_ptr<window::Font> font(FontId id) {
	const char* const files[3] = {"res/fonts/Barlow-SemiBold.ttf", "res/fonts/Barlow-Medium.ttf", "res/fonts/Barlow-BoldItalic.ttf"};
	return APP->window->loadFont(asset::plugin(pluginInstance, files[id]));
}

inline std::shared_ptr<window::Font> font(bool semibold = true) {
	return font(semibold ? FONT_LABEL : FONT_NUMBER);
}

inline void text(NVGcontext* vg, math::Vec pos, const std::string& s, float size, NVGcolor color, FontId fontId,
                 int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, float spacing = 0.4f) {
	std::shared_ptr<window::Font> f = font(fontId);
	if (!f)
		return;
	nvgFontFaceId(vg, f->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, spacing);
	nvgTextAlign(vg, align);
	nvgFillColor(vg, color);
	nvgText(vg, pos.x, pos.y, s.c_str(), nullptr);
}

inline void text(NVGcontext* vg, math::Vec pos, const std::string& s, float size, NVGcolor color, bool semibold = true,
                 int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, float spacing = 0.4f) {
	text(vg, pos, s, size, color, semibold ? FONT_LABEL : FONT_NUMBER, align, spacing);
}


/** Small deterministic random generator for the per-unit patina */
struct Rng {
	uint32_t state;
	explicit Rng(uint32_t seed) : state(seed * 2654435761u + 1u) {}
	float next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return (state & 0xffffff) / float(0x1000000);
	}
};

/** Subtle wear: worn lighter edges and faint scratches, different for every unit (`seed`). */
inline void patina(NVGcontext* vg, math::Vec size, uint32_t seed) {
	// Worn edges
	const float e = mm2px(2.5f);
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, size.x, size.y);
	nvgPathWinding(vg, NVG_CCW);
	nvgRect(vg, e, e, size.x - 2 * e, size.y - 2 * e);
	nvgPathWinding(vg, NVG_CW);
	nvgFillPaint(vg, nvgBoxGradient(vg, 0, 0, size.x, size.y, e, e * 1.5f, nvgRGBAf(1, 1, 1, 0), nvgRGBAf(1, 1, 1, 0.05f)));
	nvgFill(vg);

	Rng rng(seed);
	const int count = 26 + (int) (rng.next() * 18);
	nvgLineCap(vg, NVG_ROUND);
	for (int i = 0; i < count; ++i) {
		const float x = rng.next() * size.x, y = rng.next() * size.y;
		const float a = rng.next() * 2.f * M_PI;
		const float len = mm2px(0.8f + rng.next() * rng.next() * 7.f);
		nvgBeginPath(vg);
		nvgMoveTo(vg, x, y);
		nvgQuadTo(vg, x + std::cos(a) * len * 0.5f + (rng.next() - 0.5f) * 3.f, y + std::sin(a) * len * 0.5f,
		          x + std::cos(a) * len, y + std::sin(a) * len);
		nvgStrokeWidth(vg, 0.4f + rng.next() * 0.5f);
		nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, 0.025f + rng.next() * 0.045f));
		nvgStroke(vg);
	}
	// Chipped paint along the edges, more near the corners: irregular flakes of bare aluminium with a dark
	// rim where the paint breaks
	const int chips = 10 + (int) (rng.next() * 10);
	for (int i = 0; i < chips; ++i) {
		const int edge = (int) (rng.next() * 4.f) % 4;
		float t = rng.next();
		if (rng.next() < 0.4f)
			t = rng.next() < 0.5f ? t * 0.1f : 1.f - t * 0.1f;  // near a corner
		const float inset = mm2px(rng.next() * 0.5f);
		math::Vec cpos;
		switch (edge) {
		case 0: cpos = math::Vec(t * size.x, inset); break;
		case 1: cpos = math::Vec(size.x - inset, t * size.y); break;
		case 2: cpos = math::Vec(t * size.x, size.y - inset); break;
		default: cpos = math::Vec(inset, t * size.y); break;
		}
		const float rad = mm2px(0.35f + rng.next() * rng.next() * 1.9f);
		const int verts = 7 + (int) (rng.next() * 4);
		nvgBeginPath(vg);
		for (int v = 0; v < verts; ++v) {
			const float a = v * 2.f * M_PI / verts + (rng.next() - 0.5f) * 0.5f;
			const float rr = rad * (0.45f + 0.55f * rng.next());
			const float px = cpos.x + std::cos(a) * rr * (edge % 2 == 0 ? 1.4f : 0.8f);
			const float py = cpos.y + std::sin(a) * rr * (edge % 2 == 0 ? 0.8f : 1.4f);
			if (v == 0) nvgMoveTo(vg, px, py); else nvgLineTo(vg, px, py);
		}
		nvgClosePath(vg);
		nvgFillPaint(vg, nvgLinearGradient(vg, cpos.x - rad, cpos.y - rad, cpos.x + rad, cpos.y + rad, nvgRGBA(0xc4, 0xc7, 0xcc, 230), nvgRGBA(0x7e, 0x82, 0x89, 230)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.6f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.45f));
		nvgStroke(vg);
	}

	// Worn paint line along the very edge, broken in places
	nvgStrokeWidth(vg, 0.8f);
	for (int e = 0; e < 4; ++e) {
		const bool horiz = e % 2 == 0;
		const float len = horiz ? size.x : size.y;
		float pos = 0.f;
		while (pos < len) {
			const float seg = mm2px(1.f + rng.next() * 8.f);
			if (rng.next() < 0.55f) {
				const float off = 0.6f;
				nvgBeginPath(vg);
				if (e == 0) { nvgMoveTo(vg, pos, off); nvgLineTo(vg, pos + seg, off); }
				else if (e == 1) { nvgMoveTo(vg, size.x - off, pos); nvgLineTo(vg, size.x - off, pos + seg); }
				else if (e == 2) { nvgMoveTo(vg, pos, size.y - off); nvgLineTo(vg, pos + seg, size.y - off); }
				else { nvgMoveTo(vg, off, pos); nvgLineTo(vg, off, pos + seg); }
				nvgStrokeColor(vg, nvgRGBAf(0.75f, 0.77f, 0.8f, 0.10f + rng.next() * 0.15f));
				nvgStroke(vg);
			}
			pos += seg;
		}
	}

	// A few faint rub marks
	for (int i = 0; i < 4; ++i) {
		const float x = rng.next() * size.x, y = rng.next() * size.y, r = mm2px(2.f + rng.next() * 5.f);
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, r);
		nvgFillPaint(vg, nvgRadialGradient(vg, x, y, 0, r, nvgRGBAf(1, 1, 1, 0.025f), nvgRGBAf(1, 1, 1, 0)));
		nvgFill(vg);
	}
}

/** One scale position: `t` = the parameter value (normalised by JXMPModuleWidget::knob), a label for major marks. */
struct Mark {
	float t;
	std::string label;
};

/** Marks from `from` to `to` every `step`; every `labelEvery`-th mark gets a label from `fmt(value)`. */
template <class F>
inline std::vector<Mark> marks(float from, float to, float step, int labelEvery, F fmt) {
	std::vector<Mark> out;
	int i = 0;
	for (float v = from; v <= to + step * 0.01f; v += step, ++i)
		out.push_back({v, labelEvery > 0 && i % labelEvery == 0 ? fmt(v) : std::string()});
	return out;
}

/** Bipolar amount: −, 0, + with minor ticks every 10 %. */
inline std::vector<Mark> bipolar() {
	std::vector<Mark> out;
	for (int i = -10; i <= 10; ++i)
		out.push_back({i / 10.f, i == -10 ? "-" : i == 0 ? "0" : i == 10 ? "+" : ""});
	return out;
}

/** Knob with a ridged black body, a coloured cap and a white pointer. `diameterMm` sets its size. */
struct Knob : app::Knob {
	float diameterMm = 10.f;
	NVGcolor cap = nvgRGB(0xe8, 0xe6, 0xe0);

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

		// Coloured cap, semi-matt plastic with machining rings and a soft gloss
		const float cr = r * 0.72f;
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, cr);
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y - cr, c.x, c.y + cr, color::plus(cap, nvgRGBf(0.10f, 0.10f, 0.10f)), color::mult(cap, 0.72f)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.45f);
		for (int i = 1; i <= 3; ++i) {
			nvgBeginPath(vg);
			nvgCircle(vg, c.x, c.y, cr * (0.3f + 0.22f * i));
			nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.06f));
			nvgStroke(vg);
		}
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, cr);
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x - cr * 0.35f, c.y - cr * 0.45f, 0, cr * 0.95f, nvgRGBAf(1, 1, 1, 0.32f), nvgRGBAf(1, 1, 1, 0)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, cr);
		nvgStrokeWidth(vg, 0.6f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.55f));
		nvgStroke(vg);

		// Pointer: dark on light caps, white on dark ones
		const float a = angle() - M_PI / 2;
		const bool lightCap = (cap.r + cap.g + cap.b) / 3.f > 0.6f;
		nvgBeginPath(vg);
		nvgMoveTo(vg, c.x + std::cos(a) * r * 0.15f, c.y + std::sin(a) * r * 0.15f);
		nvgLineTo(vg, c.x + std::cos(a) * r * 0.92f, c.y + std::sin(a) * r * 0.92f);
		nvgStrokeWidth(vg, std::max(1.3f, r * 0.13f));
		nvgLineCap(vg, NVG_ROUND);
		nvgStrokeColor(vg, lightCap ? nvgRGB(0x18, 0x18, 0x1a) : nvgRGB(0xf4, 0xf4, 0xf4));
		nvgStroke(vg);

		app::Knob::draw(args);
	}
};


/** Chrome bat-handle toggle with an LED next to every position (top = highest value).
`positions` is 2 or 3; the LEDs sit to the right at the lever's end positions. */
struct Toggle : app::Switch {
	int positions = 2;

	Toggle() {
		box.size = mm2px(math::Vec(8.f, 12.f));
	}

	int index() {
		engine::ParamQuantity* const pq = getParamQuantity();
		if (!pq)
			return 0;
		return (int) std::round(pq->getValue() - pq->getMinValue());
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const math::Vec c = box.size.div(2);
		const float nut = mm2px(2.6f);
		const int idx = index();
		// -1 = up (highest value), 0 = centre, 1 = down
		const float dir = positions == 3 ? (idx == 2 ? -1.f : idx == 1 ? 0.f : 1.f) : (idx >= 1 ? -1.f : 1.f);

		// LEDs
		for (int p = 0; p < positions; ++p) {
			const float py = positions == 3 ? (p == 2 ? -1.f : p == 1 ? 0.f : 1.f) : (p >= 1 ? -1.f : 1.f);
			const math::Vec lp(c.x + mm2px(5.2f), c.y + py * mm2px(4.2f));
			const bool on = p == idx;
			nvgBeginPath(vg);
			nvgCircle(vg, lp.x, lp.y, mm2px(0.85f));
			nvgFillColor(vg, on ? nvgRGB(0x5d, 0xf0, 0x6a) : nvgRGB(0x1d, 0x2a, 0x1f));
			nvgFill(vg);
			nvgStrokeWidth(vg, 0.5f);
			nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.6f));
			nvgStroke(vg);
			if (on) {
				nvgBeginPath(vg);
				nvgCircle(vg, lp.x, lp.y, mm2px(2.2f));
				nvgFillPaint(vg, nvgRadialGradient(vg, lp.x, lp.y, mm2px(0.5f), mm2px(2.2f), nvgRGBAf(0.36f, 0.94f, 0.42f, 0.35f), nvgRGBAf(0.36f, 0.94f, 0.42f, 0)));
				nvgFill(vg);
			}
		}

		// Lever geometry: from the bushing the bat handle leans towards its position, seen slightly from above,
		// so an upward lever is long and a downward one foreshortened
		const float len = dir < 0.f ? mm2px(4.6f) : dir > 0.f ? mm2px(3.4f) : 0.f;
		const math::Vec tip(c.x, c.y + dir * len);
		const float baseW = mm2px(0.55f), tipW = mm2px(1.05f);

		// Shadow of the lever on the panel
		if (dir != 0.f) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, c.x + 1.5f, c.y + 2.f);
			nvgLineTo(vg, tip.x + 2.5f + dir * 0.f, tip.y + 3.5f);
			nvgStrokeWidth(vg, tipW * 1.6f);
			nvgLineCap(vg, NVG_ROUND);
			nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.35f));
			nvgStroke(vg);
		}

		// Washer
		nvgBeginPath(vg);
		nvgCircle(vg, c.x + 0.6f, c.y + 1.4f, nut * 1.25f);
		nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.4f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, nut * 1.18f);
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y - nut, c.x, c.y + nut, nvgRGB(0x8d, 0x91, 0x98), nvgRGB(0x3c, 0x3f, 0x45)));
		nvgFill(vg);

		// Hex nut
		nvgBeginPath(vg);
		for (int i = 0; i < 6; ++i) {
			const float a = i * M_PI / 3;
			if (i == 0) nvgMoveTo(vg, c.x + std::cos(a) * nut, c.y + std::sin(a) * nut);
			else nvgLineTo(vg, c.x + std::cos(a) * nut, c.y + std::sin(a) * nut);
		}
		nvgClosePath(vg);
		nvgFillPaint(vg, nvgLinearGradient(vg, c.x - nut, c.y - nut, c.x + nut, c.y + nut, nvgRGB(0xee, 0xef, 0xf1), nvgRGB(0x6c, 0x70, 0x77)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.6f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.6f));
		nvgStroke(vg);
		// Facet highlights
		for (int i = 0; i < 6; ++i) {
			const float a = i * M_PI / 3 + M_PI / 6;
			nvgBeginPath(vg);
			nvgMoveTo(vg, c.x + std::cos(a) * nut * 0.62f, c.y + std::sin(a) * nut * 0.62f);
			nvgLineTo(vg, c.x + std::cos(a) * nut * 0.86f, c.y + std::sin(a) * nut * 0.86f);
			nvgStrokeWidth(vg, 0.5f);
			nvgStrokeColor(vg, std::sin(a) < 0 ? nvgRGBAf(1, 1, 1, 0.35f) : nvgRGBAf(0, 0, 0, 0.25f));
			nvgStroke(vg);
		}

		// Threaded bushing
		const float bush = nut * 0.58f;
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, bush);
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x - bush * 0.3f, c.y - bush * 0.3f, 0, bush, nvgRGB(0xd8, 0xda, 0xde), nvgRGB(0x5a, 0x5e, 0x65)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.4f);
		for (int i = 1; i <= 2; ++i) {
			nvgBeginPath(vg);
			nvgCircle(vg, c.x, c.y, bush * (0.45f + 0.25f * i));
			nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.3f));
			nvgStroke(vg);
		}

		// Bat handle: tapered body with a cylindrical highlight, ending in a rounded tip
		if (dir != 0.f) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, c.x - baseW, c.y);
			nvgLineTo(vg, tip.x - tipW, tip.y);
			nvgLineTo(vg, tip.x + tipW, tip.y);
			nvgLineTo(vg, c.x + baseW, c.y);
			nvgClosePath(vg);
			NVGpaint body = nvgLinearGradient(vg, c.x - tipW, 0, c.x + tipW, 0, nvgRGB(0x70, 0x74, 0x7b), nvgRGB(0xfa, 0xfb, 0xfc));
			nvgFillPaint(vg, body);
			nvgFill(vg);
			nvgStrokeWidth(vg, 0.45f);
			nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.45f));
			nvgStroke(vg);
			// Highlight line along the body
			nvgBeginPath(vg);
			nvgMoveTo(vg, c.x + baseW * 0.25f, c.y + dir * mm2px(0.4f));
			nvgLineTo(vg, tip.x + tipW * 0.35f, tip.y - dir * mm2px(0.3f));
			nvgStrokeWidth(vg, 0.7f);
			nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, 0.7f));
			nvgStroke(vg);
		}
		// Rounded tip (seen end-on when centred)
		const float tr = dir == 0.f ? mm2px(1.15f) : tipW * 1.08f;
		nvgBeginPath(vg);
		nvgCircle(vg, tip.x, tip.y, tr);
		nvgFillPaint(vg, nvgRadialGradient(vg, tip.x - tr * 0.35f, tip.y - tr * 0.4f, 0, tr * 1.1f, nvgRGB(0xff, 0xff, 0xff), nvgRGB(0x76, 0x7a, 0x81)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.5f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.5f));
		nvgStroke(vg);
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

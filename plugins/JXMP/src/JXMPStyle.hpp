/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JXMP panel style: 80s-inspired anthracite panels, double colour stripe, framed sections,
 * knobs with coloured caps and scales, waveform icons. Everything is drawn in NanoVG.
 */

#pragma once

#include "plugin.hpp"
#include "PatinaLayout.hpp"

#include <cstdint>
#include <map>
#include <tuple>
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


/** Small deterministic random generator (the panel wear has its own in PatinaLayout.hpp) */
using Rng = patina::Rng;

/** Draws a wear layout (see PatinaLayout.hpp): worn lighter edges, scratches, grime, chipped paint. Drawn under the
legends and controls, so the text stays readable, and under the screws. Clipped to the panel: chips and scratches near
an edge must not spill onto the neighbouring modules. */
inline void drawPatina(NVGcontext* vg, math::Vec size, const patina::Layout& l) {
	nvgSave(vg);
	nvgIntersectScissor(vg, 0, 0, size.x, size.y);
	// Worn edges
	const float e = mm2px(2.5f);
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, size.x, size.y);
	nvgPathWinding(vg, NVG_CCW);
	nvgRect(vg, e, e, size.x - 2 * e, size.y - 2 * e);
	nvgPathWinding(vg, NVG_CW);
	nvgFillPaint(vg, nvgBoxGradient(vg, 0, 0, size.x, size.y, e, e * 1.5f, nvgRGBAf(1, 1, 1, 0), nvgRGBAf(1, 1, 1, 0.07f)));
	nvgFill(vg);

	nvgLineCap(vg, NVG_ROUND);
	auto stroke = [&](const patina::Scratch& s, float o) {
		const float mx = 0.5f * (s.a.x + s.b.x), my = 0.5f * (s.a.y + s.b.y);
		const float dx = s.b.x - s.a.x, dy = s.b.y - s.a.y, len = std::max(std::hypot(dx, dy), 1e-3f);
		nvgBeginPath(vg);
		nvgMoveTo(vg, s.a.x + o, s.a.y + o);
		nvgQuadTo(vg, mx - dy / len * s.bend + o, my + dx / len * s.bend + o, s.b.x + o, s.b.y + o);
	};
	// Light scratches
	for (const patina::Scratch& s : l.scratches) {
		if (s.deep)
			continue;
		stroke(s, 0.f);
		nvgStrokeWidth(vg, s.width);
		nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, s.alpha));
		nvgStroke(vg);
	}
	// Chipped paint: irregular flakes of bare aluminium with a dark rim where the paint breaks
	for (const patina::Chip& c : l.chips) {
		nvgBeginPath(vg);
		for (size_t v = 0; v < c.points.size(); ++v) {
			if (v == 0)
				nvgMoveTo(vg, c.points[v].x, c.points[v].y);
			else
				nvgLineTo(vg, c.points[v].x, c.points[v].y);
		}
		nvgClosePath(vg);
		nvgFillPaint(vg, nvgLinearGradient(vg, c.c.x - c.rad, c.c.y - c.rad, c.c.x + c.rad, c.c.y + c.rad, nvgRGBA(0xc4, 0xc7, 0xcc, 230), nvgRGBA(0x7e, 0x82, 0x89, 230)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.6f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.45f));
		nvgStroke(vg);
	}
	// Worn paint line along the very edge, broken in places
	nvgStrokeWidth(vg, 0.8f);
	for (const patina::EdgeLine& el : l.edgeLines) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, el.a.x, el.a.y);
		nvgLineTo(vg, el.b.x, el.b.y);
		nvgStrokeColor(vg, nvgRGBAf(0.75f, 0.77f, 0.8f, el.alpha));
		nvgStroke(vg);
	}
	// Grime and faint rubs
	for (const patina::Smudge& m : l.smudges) {
		nvgBeginPath(vg);
		nvgCircle(vg, m.c.x, m.c.y, m.r);
		const NVGcolor in = m.light ? nvgRGBAf(1, 1, 1, m.alpha) : nvgRGBAf(0, 0, 0, m.alpha);
		const NVGcolor out = m.light ? nvgRGBAf(1, 1, 1, 0) : nvgRGBAf(0, 0, 0, 0);
		nvgFillPaint(vg, nvgRadialGradient(vg, m.c.x, m.c.y, 0, m.r, in, out));
		nvgFill(vg);
	}
	// Deeper scratches: a dark groove with a light lip
	for (const patina::Scratch& s : l.scratches) {
		if (!s.deep)
			continue;
		for (int layer = 0; layer < 2; ++layer) {
			stroke(s, layer ? 0.7f : 0.f);
			nvgStrokeWidth(vg, layer ? 0.5f : s.width);
			nvgStrokeColor(vg, layer ? nvgRGBAf(1, 1, 1, 0.10f) : nvgRGBAf(0, 0, 0, s.alpha));
			nvgStroke(vg);
		}
	}
	// Small paint chips inside the panel
	for (const patina::Dot& d : l.dots) {
		nvgBeginPath(vg);
		nvgEllipse(vg, d.c.x, d.c.y, d.rx, d.ry);
		nvgFillColor(vg, nvgRGBA(0xa8, 0xac, 0xb2, 190));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.5f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.4f));
		nvgStroke(vg);
	}
	nvgRestore(vg);
}

/** Wear for a panel of `size` and a unit `seed` (its serial). `screws`: keep the four screw places clear. The layout
is made once per unit and size (UI thread only) and then only drawn. */
inline void patina(NVGcontext* vg, math::Vec size, uint32_t seed, bool screws = true) {
	struct Key {
		uint32_t seed;
		float w, h;
		bool screws;
		bool operator<(const Key& o) const {
			return std::tie(seed, w, h, screws) < std::tie(o.seed, o.w, o.h, o.screws);
		}
	};
	static std::map<Key, patina::Layout> cache;
	const Key key{seed, size.x, size.y, screws};
	auto it = cache.find(key);
	if (it == cache.end()) {
		if (cache.size() > 512)
			cache.clear();
		const std::vector<patina::Keepout> keep = screws ? patina::screwKeepouts(size.x, size.y) : std::vector<patina::Keepout>();
		it = cache.emplace(key, patina::make(size.x, size.y, seed, keep)).first;
	}
	drawPatina(vg, size, it->second);
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


/** Vacuum fluorescent display (VFD): every readout of a JXMP module uses it, glowing in the module colour. */
inline NVGcolor vfdLit() { return nvgRGB(0x5c, 0xff, 0xb8); }

/** The glow of a module colour: brightened to full intensity and a little towards white, so dark colours
(navy, indigo) still read as a lit display */
inline NVGcolor vfdTint(NVGcolor c) {
	const float m = std::max(std::max(c.r, c.g), std::max(c.b, 0.01f));
	auto ch = [&](float v) { return std::min(1.f, v / m) * 0.8f + 0.2f; };
	return nvgRGBf(ch(c.r), ch(c.g), ch(c.b));
}

/** Bezel width: 1 mm, thinner on small windows */
inline float vfdBezel(math::Vec size) { return std::min(mm2px(1.f), size.y * 0.12f); }

/** The VFD window: black bezel, dark glass tinted like the lit colour (green, or a module colour), the fine grid mesh and the filament wires behind it,
a soft reflection on top. Draw it first, then the digits and annunciators. */
inline void vfdWindow(NVGcontext* vg, math::Vec size, NVGcolor lit = vfdLit()) {
	const float bz = vfdBezel(size);
	// Bezel with a darker upper and a lighter lower edge (the window sits in the panel)
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0, 0, size.x, size.y, 2.5f);
	nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, size.y, nvgRGB(0x05, 0x05, 0x06), nvgRGB(0x24, 0x25, 0x29)));
	nvgFill(vg);
	// Glass
	const math::Rect g(math::Vec(bz, bz), size.minus(math::Vec(2 * bz, 2 * bz)));
	nvgBeginPath(vg);
	nvgRoundedRect(vg, RECT_ARGS(g), 1.5f);
	nvgFillPaint(vg, nvgLinearGradient(vg, 0, g.pos.y, 0, g.pos.y + g.size.y,
	                                   nvgRGBf(lit.r * 0.045f, lit.g * 0.045f, lit.b * 0.045f), nvgRGBf(lit.r * 0.08f, lit.g * 0.08f, lit.b * 0.08f)));
	nvgFill(vg);
	nvgSave(vg);
	nvgIntersectScissor(vg, RECT_ARGS(g));
	// Grid mesh
	const float pitch = mm2px(0.55f);
	nvgBeginPath(vg);
	for (float x = g.pos.x + pitch / 2; x < g.pos.x + g.size.x; x += pitch) {
		nvgMoveTo(vg, x, g.pos.y);
		nvgLineTo(vg, x, g.pos.y + g.size.y);
	}
	for (float y = g.pos.y + pitch / 2; y < g.pos.y + g.size.y; y += pitch) {
		nvgMoveTo(vg, g.pos.x, y);
		nvgLineTo(vg, g.pos.x + g.size.x, y);
	}
	nvgStrokeWidth(vg, 0.4f);
	nvgStrokeColor(vg, nvgRGBAf(lit.r, lit.g, lit.b, 0.035f));
	nvgStroke(vg);
	// Filament wires across the glass
	nvgBeginPath(vg);
	for (int i = 1; i <= 3; ++i) {
		const float y = g.pos.y + g.size.y * i / 4.f;
		nvgMoveTo(vg, g.pos.x, y);
		nvgLineTo(vg, g.pos.x + g.size.x, y + 0.6f);
	}
	nvgStrokeWidth(vg, 0.5f);
	nvgStrokeColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.45f));
	nvgStroke(vg);
	nvgRestore(vg);
}

/** The glass reflection, drawn over the digits */
inline void vfdGlare(NVGcontext* vg, math::Vec size) {
	const float bz = vfdBezel(size);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, bz, bz, size.x - 2 * bz, (size.y - 2 * bz) * 0.42f, 1.5f);
	nvgFillPaint(vg, nvgLinearGradient(vg, 0, bz, 0, bz + (size.y - 2 * bz) * 0.42f, nvgRGBAf(1, 1, 1, 0.07f), nvgRGBAf(1, 1, 1, 0.f)));
	nvgFill(vg);
}

/** Seven slanted segments, a-g, as in a VFD tube. Lit segments glow; unlit ones stay faintly visible. */
inline void vfdSegment(NVGcontext* vg, int seg, float w, float h, float t) {
	const float g = t * 0.18f, m = h / 2, ht = t / 2;
	nvgBeginPath(vg);
	auto hor = [&](float y) {
		nvgMoveTo(vg, g, y);
		nvgLineTo(vg, g + ht, y - ht);
		nvgLineTo(vg, w - g - ht, y - ht);
		nvgLineTo(vg, w - g, y);
		nvgLineTo(vg, w - g - ht, y + ht);
		nvgLineTo(vg, g + ht, y + ht);
		nvgClosePath(vg);
	};
	auto ver = [&](float x, float y0, float y1) {
		nvgMoveTo(vg, x, y0 + g);
		nvgLineTo(vg, x + ht, y0 + g + ht);
		nvgLineTo(vg, x + ht, y1 - g - ht);
		nvgLineTo(vg, x, y1 - g);
		nvgLineTo(vg, x - ht, y1 - g - ht);
		nvgLineTo(vg, x - ht, y0 + g + ht);
		nvgClosePath(vg);
	};
	switch (seg) {
	case 0: hor(0); break;
	case 1: ver(w, 0, m); break;
	case 2: ver(w, m, h); break;
	case 3: hor(h); break;
	case 4: ver(0, m, h); break;
	case 5: ver(0, 0, m); break;
	case 6: hor(m); break;
	}
}

/** Segment masks (bit 0 = a ... bit 6 = g) for 0-9; '-' and ' ' are handled by vfdDigits */
inline uint8_t vfdMask(char c) {
	static const uint8_t digits[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
	if (c >= '0' && c <= '9')
		return digits[c - '0'];
	return c == '-' ? 0x40 : 0x00;
}

/** VFD text in the dot-matrix style of the label font, with a soft glow */
inline void vfdText(NVGcontext* vg, math::Vec pos, const std::string& s, float size, int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                    NVGcolor lit = vfdLit()) {
	nvgFontBlur(vg, size * 0.25f);
	text(vg, pos, s, size, nvgRGBAf(lit.r, lit.g, lit.b, lit.a * 0.5f), FONT_LABEL, align, 0.8f);
	nvgFontBlur(vg, 0.f);
	text(vg, pos, s, size, lit, FONT_LABEL, align, 0.8f);
}

/** Draws a row of seven-segment digits right-aligned to `right` with their baseline at `bottom`. A '.' lights the
decimal point of the digit before it. `cells` digits are drawn in all, unused ones as unlit ghosts. */
inline void vfdDigits(NVGcontext* vg, float right, float bottom, float height, int cells, const std::string& s,
                      NVGcolor lit = vfdLit()) {
	struct Cell { uint8_t mask; bool dot; };
	std::vector<Cell> row;
	for (char c : s) {
		if (c == '.' && !row.empty())
			row.back().dot = true;
		else
			row.push_back(Cell{vfdMask(c), false});
	}
	while ((int) row.size() < cells)
		row.insert(row.begin(), Cell{0, false});
	const float w = height * 0.5f, t = height * 0.1f, pitch = w + height * 0.32f;
	const NVGcolor ghost = nvgRGBAf(lit.r, lit.g, lit.b, 0.06f);
	const int n = row.size();
	for (int i = 0; i < n; ++i) {
		nvgSave(vg);
		nvgTranslate(vg, right - (n - i) * pitch + height * 0.12f, bottom - height);
		// Lean the digits like a real tube (top to the right)
		nvgTranslate(vg, 0, height);
		nvgSkewX(vg, -0.14f);
		nvgTranslate(vg, 0, -height);
		for (int seg = 0; seg < 7; ++seg) {
			const bool on = row[i].mask & (1 << seg);
			vfdSegment(vg, seg, w, height, t);
			if (on) {
				// Glow: a wide faint halo, then the bright core
				nvgStrokeWidth(vg, t * 0.7f);
				nvgLineJoin(vg, NVG_ROUND);
				nvgStrokeColor(vg, nvgRGBAf(lit.r, lit.g, lit.b, lit.a * 0.12f));
				nvgStroke(vg);
				nvgFillColor(vg, lit);
			}
			else {
				nvgFillColor(vg, ghost);
			}
			nvgFill(vg);
		}
		nvgBeginPath(vg);
		nvgCircle(vg, w + height * 0.13f, height - t * 0.35f, t * 0.55f);
		nvgFillColor(vg, row[i].dot ? lit : ghost);
		nvgFill(vg);
		nvgRestore(vg);
	}
}

/** A fixed VFD legend (annunciator): lit when `on`, otherwise a faint print on the glass */
inline void vfdLegend(NVGcontext* vg, math::Vec pos, const std::string& s, float size, bool on, int align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                      NVGcolor lit = vfdLit()) {
	text(vg, pos, s, size, on ? lit : nvgRGBAf(lit.r, lit.g, lit.b, 0.09f), FONT_LABEL, align, 0.6f);
}

} // namespace style
} // namespace jxmp

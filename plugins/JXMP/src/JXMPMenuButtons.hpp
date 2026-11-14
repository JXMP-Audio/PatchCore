/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The menu buttons of the display modules (JX-SMPL, JX-NEST): ▲ ▼ ≡ ✓ icon buttons and the frame round them with
 * MENU in its top line, lit while the menu is open.
 */

#pragma once

#include "JXMPWidget.hpp"

#include <functional>

namespace jxmp {

/** The menu buttons: the same button as PLAY with an icon on its cap (▲ ▼ ≡ ✓). They act on press. */
struct IconButton : VCVButton {
	enum Icon { UP, DOWN, MENU, OK };
	Icon icon = UP;
	std::function<void()> action;

	void onDragStart(const DragStartEvent& e) override {
		VCVButton::onDragStart(e);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && action)
			action();
	}

	void draw(const DrawArgs& args) override {
		VCVButton::draw(args);
		NVGcontext* const vg = args.vg;
		const math::Vec c = box.size.div(2);
		const float s = box.size.x * 0.17f;
		const NVGcolor ic = nvgRGB(0xd2, 0xd4, 0xd9);
		nvgBeginPath(vg);
		switch (icon) {
		case UP:
		case DOWN: {
			const float d = icon == UP ? -1.f : 1.f;
			nvgMoveTo(vg, c.x, c.y + d * s * 0.8f);
			nvgLineTo(vg, c.x + s, c.y - d * s * 0.6f);
			nvgLineTo(vg, c.x - s, c.y - d * s * 0.6f);
			nvgClosePath(vg);
			nvgFillColor(vg, ic);
			nvgFill(vg);
			break;
		}
		case MENU:
			for (int k = -1; k <= 1; ++k) {
				nvgMoveTo(vg, c.x - s, c.y + k * s * 0.62f);
				nvgLineTo(vg, c.x + s, c.y + k * s * 0.62f);
			}
			nvgStrokeWidth(vg, 1.2f);
			nvgLineCap(vg, NVG_ROUND);
			nvgStrokeColor(vg, ic);
			nvgStroke(vg);
			break;
		case OK:
			nvgMoveTo(vg, c.x - s, c.y + s * 0.05f);
			nvgLineTo(vg, c.x - s * 0.3f, c.y + s * 0.72f);
			nvgLineTo(vg, c.x + s, c.y - s * 0.6f);
			nvgStrokeWidth(vg, 1.3f);
			nvgLineCap(vg, NVG_ROUND);
			nvgLineJoin(vg, NVG_ROUND);
			nvgStrokeColor(vg, ic);
			nvgStroke(vg);
			break;
		}
	}
};

/** A grey frame round the four menu buttons (like the section lines) with MENU in a gap of its top line. The word is
 * the menu's light: dark green while the menu is closed, lit (green with a glow, like the toggles' LEDs) while open. */
struct MenuFrame : widget::TransparentWidget {
	std::function<bool()> isOpen;  // the menu's light
	math::Rect frame;              // in this widget, px

	void draw(const DrawArgs& args) override {
		namespace st = jxmp::style;
		NVGcontext* const vg = args.vg;
		const bool on = isOpen && isOpen();
		const float size = 6.f, r = mm2px(1.2f);
		const math::Vec t(frame.pos.x + frame.size.x / 2, frame.pos.y);
		// The gap in the top line: the word's width and a little air
		float half = mm2px(5.f);
		if (std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL)) {
			nvgFontFaceId(vg, f->handle);
			nvgFontSize(vg, size);
			nvgTextLetterSpacing(vg, 0.8f);
			float bounds[4];
			const float w = nvgTextBounds(vg, 0, 0, "MENU", nullptr, bounds);
			half = w / 2 + mm2px(1.f);
		}
		const float x0 = frame.pos.x, y0 = frame.pos.y, x1 = x0 + frame.size.x, y1 = y0 + frame.size.y;
		nvgBeginPath(vg);
		nvgMoveTo(vg, t.x + half, y0);
		nvgArcTo(vg, x1, y0, x1, y1, r);
		nvgArcTo(vg, x1, y1, x0, y1, r);
		nvgArcTo(vg, x0, y1, x0, y0, r);
		nvgArcTo(vg, x0, y0, t.x - half, y0, r);
		nvgLineTo(vg, t.x - half, y0);
		nvgStrokeWidth(vg, 0.8f);
		nvgStrokeColor(vg, st::line());
		nvgStroke(vg);
		if (on) {
			nvgFontBlur(vg, 3.f);
			st::text(vg, t, "MENU", size, nvgRGBAf(0.36f, 0.94f, 0.42f, 0.8f), st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
			nvgFontBlur(vg, 0.f);
		}
		st::text(vg, t, "MENU", size, on ? nvgRGB(0x5d, 0xf0, 0x6a) : nvgRGB(0x1f, 0x4a, 0x27), st::FONT_LABEL,
		         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
	}
};

} // namespace jxmp

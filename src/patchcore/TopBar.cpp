/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "PatchCore.hpp"

#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <history.hpp>
#include <patch.hpp>
#include <settings.hpp>
#include <string.hpp>
#include <system.hpp>
#include <widget/OpaqueWidget.hpp>

namespace rack {
namespace patchcore {


float textWidth(std::shared_ptr<window::Font> font, float size, const std::string& s, float letterSpacing) {
	if (!font || font->handle < 0)
		return 0.f;
	NVGcontext* const vg = APP->window->vg;
	nvgSave(vg);
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, letterSpacing);
	const float w = nvgTextBounds(vg, 0, 0, s.c_str(), nullptr, nullptr);
	nvgRestore(vg);
	return w;
}


static bool isHovered(widget::Widget* const w) {
	return APP->event->hoveredWidget == w;
}


void drawBarBackground(NVGcontext* const vg, math::Vec size, bool topEdgeIsBorder) {
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, size.x, size.y);
	nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, size.y, nvgRGB(0x26, 0x28, 0x2c), nvgRGB(0x1c, 0x1d, 0x21)));
	nvgFill(vg);

	nvgStrokeWidth(vg, 1.f);
	// inner highlight
	nvgBeginPath(vg);
	nvgMoveTo(vg, 0, topEdgeIsBorder ? 1.5f : 0.5f);
	nvgLineTo(vg, size.x, topEdgeIsBorder ? 1.5f : 0.5f);
	nvgStrokeColor(vg, nvgRGB(0x34, 0x37, 0x3d));
	nvgStroke(vg);
	// black border
	nvgBeginPath(vg);
	if (topEdgeIsBorder) {
		nvgMoveTo(vg, 0, 0.5f);
		nvgLineTo(vg, size.x, 0.5f);
	}
	else {
		nvgMoveTo(vg, 0, size.y - 0.5f);
		nvgLineTo(vg, size.x, size.y - 0.5f);
	}
	nvgStrokeColor(vg, nvgRGB(0, 0, 0));
	nvgStroke(vg);
}


void drawMenuButton(NVGcontext* const vg, math::Vec size, const std::string& text, bool hovered) {
	const Theme& t = theme();
	if (hovered) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, size.x, size.y, 4.f);
		nvgFillColor(vg, t.chrome3);
		nvgFill(vg);
	}
	draw::text(vg, uiFont(), 13.f, hovered ? t.text : t.textDim, math::Vec(size.x * 0.5f, size.y * 0.5f),
	           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, text);
}


float menuButtonWidth(const std::string& text) {
	return textWidth(uiFont(), 13.f, text, 0.f) + 16.f;
}


/** App logo: a patch-cable glyph and the product name */
struct Logo : widget::Widget {
	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();
		const float cy = box.size.y * 0.5f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, cy - 11.f, 22.f, 22.f, 5.f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, cy - 11.f, 0, cy + 11.f, nvgRGB(0x3a, 0x3d, 0x44), nvgRGB(0x1c, 0x1d, 0x21)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGB(0, 0, 0));
		nvgStroke(vg);

		// cable between two jacks
		nvgBeginPath(vg);
		nvgMoveTo(vg, 6.f, cy + 4.f);
		nvgBezierTo(vg, 6.f, cy + 10.f, 16.f, cy - 10.f, 16.f, cy - 4.f);
		nvgStrokeWidth(vg, 2.6f);
		nvgLineCap(vg, NVG_ROUND);
		nvgStrokeColor(vg, t.accent);
		nvgStroke(vg);
		for (const math::Vec p : {math::Vec(6.f, cy + 4.f), math::Vec(16.f, cy - 4.f)}) {
			nvgBeginPath(vg);
			nvgCircle(vg, p.x, p.y, 2.6f);
			nvgFillColor(vg, t.text);
			nvgFill(vg);
		}

		draw::text(vg, labelFont(), 15.f, t.text, math::Vec(30.f, cy), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "PATCHCORE", 2.2f);
	}
};


/** Segmented view switch: Rack | Back | Browser | Mixer */
struct ViewSegment : widget::OpaqueWidget {
	struct Item {
		std::string label;
		std::string key;
		// set by step()
		float x;
		float w;
	};
	std::vector<Item> items = {
		{"Rack", "Tab"},
		{"Back", ""},
		{"Browser", "B"},
		{"Mixer", ""},
	};

	bool isOn(size_t i) const {
		switch (i) {
			case 0: return targetViewSide() == VIEW_FRONT;
			case 1: return targetViewSide() == VIEW_BACK;
			case 2: return settings.browserDocked;
			default: return false;
		}
	}

	bool isDisabled(size_t i) const {
		// The mixer view is a later milestone
		return i == 3;
	}

	void step() override {
		float x = 2.f;
		for (Item& item : items) {
			item.x = x;
			item.w = textWidth(uiFont(), 13.f, item.label, 0.f) + 22.f;
			if (!item.key.empty())
				item.w += textWidth(monoFont(), 10.f, item.key, 0.f) + 6.f;
			x += item.w;
		}
		box.size.x = x + 2.f;
		OpaqueWidget::step();
	}

	int itemAt(math::Vec pos) const {
		for (size_t i = 0; i < items.size(); ++i) {
			if (pos.x >= items[i].x && pos.x < items[i].x + items[i].w)
				return i;
		}
		return -1;
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		const int i = itemAt(e.pos);
		if (i < 0 || isDisabled(i))
			return;
		switch (i) {
			case 0: setViewSide(VIEW_FRONT); break;
			case 1: setViewSide(VIEW_BACK); break;
			case 2: toggleBrowser(); break;
		}
		e.consume(this);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();

		draw::insetBox(vg, box.zeroPos(), 6.f, nvgRGB(0x0d, 0x0e, 0x10));

		for (size_t i = 0; i < items.size(); ++i) {
			const Item& item = items[i];
			const math::Rect r(item.x, 2.f, item.w, box.size.y - 4.f);
			if (isOn(i))
				draw::bevelBox(vg, r, 4.f, nvgRGB(0x41, 0x44, 0x4b), nvgRGB(0x33, 0x36, 0x3c), nvgRGB(0, 0, 0));

			NVGcolor color = isOn(i) ? color::WHITE : t.textDim;
			if (isDisabled(i))
				color = color::alpha(t.textDim, 0.35f);

			float x = r.pos.x + 11.f;
			draw::text(vg, uiFont(), 13.f, color, math::Vec(x, r.pos.y + r.size.y * 0.5f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, item.label);
			if (!item.key.empty()) {
				x += textWidth(uiFont(), 13.f, item.label, 0.f) + 6.f;
				draw::text(vg, monoFont(), 10.f, t.textFaint, math::Vec(x, r.pos.y + r.size.y * 0.5f + 1.f),
				           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, item.key);
			}
		}
	}
};


/** LCD capsule with the patch file name and an "edited" flag */
struct PatchNameLcd : widget::Widget {
	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();
		draw::insetBox(vg, box.zeroPos(), 4.f, t.lcdBg);

		std::string name = APP->patch->path.empty() ? "Untitled patch" : system::getFilename(APP->patch->path);
		const bool edited = !APP->history->isSaved();

		nvgScissor(vg, 0, 0, box.size.x, box.size.y);
		const float cy = box.size.y * 0.5f + 1.f;
		draw::lcdText(vg, 13.f, math::Vec(12.f, cy), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, name, t.lcdFg);
		if (edited) {
			const float x = 12.f + textWidth(monoFont(), 13.f, name, 0.f) + 10.f;
			draw::lcdText(vg, 11.f, math::Vec(x, cy), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "* edited", t.lcdDim);
		}
		nvgResetScissor(vg);
	}
};


/** "SHEEN" label and a horizontal slider for the panel gloss amount */
struct SheenSlider : widget::OpaqueWidget {
	static constexpr const float kLabelWidth = 52.f;

	math::Rect trackRect() const {
		return math::Rect(kLabelWidth, box.size.y * 0.5f - 2.f, box.size.x - kLabelWidth - 4.f, 4.f);
	}

	void setFromX(float x) {
		const math::Rect track = trackRect();
		settings.sheen = math::clamp((x - track.pos.x) / track.size.x, 0.f, 1.f);
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
			setFromX(e.pos.x);
	}

	void onDragMove(const DragMoveEvent& e) override {
		const math::Rect track = trackRect();
		settings.sheen = math::clamp(settings.sheen + e.mouseDelta.x / track.size.x, 0.f, 1.f);
	}

	void onDragEnd(const DragEndEvent& e) override {
		saveSettings();
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();
		draw::text(vg, uiFont(), 11.f, t.textDim, math::Vec(0, box.size.y * 0.5f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "SHEEN", 0.8f);

		const math::Rect track = trackRect();
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(track), 2.f);
		nvgFillColor(vg, t.chrome4);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, track.pos.x, track.pos.y, track.size.x * settings.sheen, track.size.y, 2.f);
		nvgFillColor(vg, t.accent);
		nvgFill(vg);

		const math::Vec knob(track.pos.x + track.size.x * settings.sheen, track.pos.y + track.size.y * 0.5f);
		nvgBeginPath(vg);
		nvgCircle(vg, knob.x, knob.y, 7.f);
		nvgFillColor(vg, t.accent);
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 0x80));
		nvgStroke(vg);
	}
};


/** Selection summary, plus frame rate when the performance meters are on */
struct StatusText : widget::Widget {
	int frameCount = 0;
	double frameTotal = 0.0;
	double fps = 0.0;

	void step() override {
		const double frameDuration = APP->window->getLastFrameDuration();
		if (std::isfinite(frameDuration)) {
			frameTotal += frameDuration;
			++frameCount;
		}
		if (frameTotal >= 1.0) {
			fps = frameCount / frameTotal;
			frameTotal = 0.0;
			frameCount = 0;
		}
		Widget::step();
	}

	void draw(const DrawArgs& args) override {
		const Theme& t = theme();
		const size_t count = APP->scene->rack->getSelected().size();
		std::string text;
		if (count == 0)
			text = "No selection";
		else if (count == 1)
			text = "1 module selected";
		else
			text = string::f("%d modules selected", (int) count);
		if (rack::settings::cpuMeter)
			text = string::f("%.0f fps  ·  ", fps) + text;
		draw::text(args.vg, uiFont(), 12.f, t.textDim, math::Vec(box.size.x, box.size.y * 0.5f),
		           NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, text);
	}
};


struct TopBarCenter : widget::Widget {
	ViewSegment* segment;
	PatchNameLcd* lcd;

	TopBarCenter() {
		segment = new ViewSegment;
		addChild(segment);
		lcd = new PatchNameLcd;
		addChild(lcd);
	}

	void step() override {
		Widget::step();
		segment->box.pos = math::Vec(0, (box.size.y - 30.f) * 0.5f);
		segment->box.size.y = 30.f;
		// The LCD takes what is left of the width given by the menu bar
		lcd->box.pos = math::Vec(segment->box.size.x + 14.f, (box.size.y - 28.f) * 0.5f);
		lcd->box.size = math::Vec(std::min(300.f, box.size.x - lcd->box.pos.x), 28.f);
		lcd->setVisible(lcd->box.size.x >= 120.f);
	}
};


struct TopBarRight : widget::Widget {
	SheenSlider* sheen;
	StatusText* status;

	TopBarRight() {
		sheen = new SheenSlider;
		addChild(sheen);
		status = new StatusText;
		addChild(status);
		box.size.x = 360.f;
	}

	void step() override {
		sheen->box = math::Rect(0, 0, 120.f, box.size.y);
		status->box = math::Rect(130.f, 0, box.size.x - 130.f, box.size.y);
		status->setVisible(status->box.size.x >= 100.f);
		Widget::step();
	}
};


widget::Widget* createTopBarLogo() {
	Logo* const logo = new Logo;
	logo->box.size.x = 150.f;
	return logo;
}


widget::Widget* createTopBarCenter() {
	return new TopBarCenter;
}


widget::Widget* createTopBarRight() {
	return new TopBarRight;
}


} // namespace patchcore
} // namespace rack

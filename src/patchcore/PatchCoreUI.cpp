/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// PatchCore widgets that need the Cardinal plugin context.
// Built once per plugin variant (like MenuBar.cpp), through a symlink in each variant folder.

#include <app/Scene.hpp>
#include <context.hpp>
#include <engine/Engine.hpp>
#include <string.hpp>
#include <widget/OpaqueWidget.hpp>

#include "../CardinalCommon.hpp"
#include "../CardinalPluginContext.hpp"
#include "../patchcore/PatchCore.hpp"
#include "DistrhoPluginUtils.hpp"

namespace rack {
namespace patchcore {


static CardinalPluginContext* pluginContext() {
	return static_cast<CardinalPluginContext*>(APP);
}


bool transportIsInternal() {
	return pluginContext()->internalTransportActive;
}


void transportTogglePlay() {
	CardinalPluginContext* const context = pluginContext();
	if (context->internalTransportActive)
		context->internalTransportPlaying = !context->internalTransportPlaying;
}


static void transportStop() {
	CardinalPluginContext* const context = pluginContext();
	if (!context->internalTransportActive)
		return;
	// Stop when rolling, rewind when already stopped
	if (context->internalTransportPlaying)
		context->internalTransportPlaying = false;
	else
		context->internalTransportResetRequested = true;
}


enum TransportIcon {
	ICON_STOP,
	ICON_PLAY,
	ICON_REC,
	ICON_LOOP,
	ICON_CLICK,
};


struct TransportBar : widget::OpaqueWidget {
	math::Rect tempoRect, tempoUpRect, tempoDownRect, sigRect, buttonsRect, posRect, timeRect, dspRect, masterRect;
	math::Rect buttonRects[5];
	bool draggingTempo = false;

	void step() override {
		const float cy = box.size.y * 0.5f + 6.f;
		float x = 16.f;
		tempoRect = math::Rect(x, cy - 16.f, 120.f, 32.f);
		x += 120.f + 8.f;
		tempoUpRect = math::Rect(x, cy - 15.f, 16.f, 13.f);
		tempoDownRect = math::Rect(x, cy + 2.f, 16.f, 13.f);
		x += 16.f + 10.f;
		sigRect = math::Rect(x, cy - 14.f, 38.f, 28.f);
		x += 38.f + 18.f;

		buttonsRect = math::Rect(x, box.size.y * 0.5f - 24.f, 5 * 42.f + 4 * 6.f + 12.f, 48.f);
		for (int i = 0; i < 5; ++i)
			buttonRects[i] = math::Rect(x + 6.f + i * 48.f, buttonsRect.pos.y + 6.f, 42.f, 36.f);
		x += buttonsRect.size.x + 18.f;

		posRect = math::Rect(x, cy - 16.f, 146.f, 32.f);
		x += 146.f + 12.f;
		timeRect = math::Rect(x, cy - 14.f, 112.f, 28.f);

		const float right = box.size.x - 16.f;
		masterRect = math::Rect(right - 330.f, cy - 12.f, 150.f, 24.f);
		dspRect = math::Rect(masterRect.pos.x - 110.f, cy - 8.f, 82.f, 16.f);

		OpaqueWidget::step();
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS)
			return;

		CardinalPluginContext* const context = pluginContext();
		draggingTempo = false;

		if (!context->internalTransportActive) {
			e.consume(this);
			return;
		}

		if (buttonRects[ICON_STOP].contains(e.pos))
			transportStop();
		else if (buttonRects[ICON_PLAY].contains(e.pos))
			transportTogglePlay();
		else if (tempoUpRect.contains(e.pos))
			context->internalTransportBeatsPerMinute = std::min(999.0, (double)context->internalTransportBeatsPerMinute + 1.0);
		else if (tempoDownRect.contains(e.pos))
			context->internalTransportBeatsPerMinute = std::max(20.0, (double)context->internalTransportBeatsPerMinute - 1.0);
		else if (tempoRect.contains(e.pos))
			draggingTempo = true;
		else if (sigRect.contains(e.pos))
			context->internalTransportBeatsPerBar = context->internalTransportBeatsPerBar % 16 + 1;

		e.consume(this);
	}

	void onDragMove(const DragMoveEvent& e) override {
		CardinalPluginContext* const context = pluginContext();
		if (!draggingTempo || !context->internalTransportActive)
			return;
		const double bpm = context->internalTransportBeatsPerMinute - e.mouseDelta.y * 0.25;
		context->internalTransportBeatsPerMinute = std::max(20.0, std::min(999.0, bpm));
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		CardinalPluginContext* const context = pluginContext();
		if (draggingTempo && context->internalTransportActive)
			context->internalTransportBeatsPerMinute = 120.0;
	}

	void drawLabel(NVGcontext* const vg, const math::Rect& r, const std::string& label) {
		draw::text(vg, labelFont(), 9.f, theme().textDim, math::Vec(r.pos.x, r.pos.y - 7.f),
		           NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, label, 1.4f);
	}

	void drawLcd(NVGcontext* const vg, const math::Rect& r, float size, const std::string& text, bool active) {
		const Theme& t = theme();
		draw::insetBox(vg, r, 4.f, t.lcdBg);
		draw::lcdText(vg, size, math::Vec(r.pos.x + r.size.x - 10.f, r.pos.y + r.size.y * 0.5f + 1.f),
		              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, text, active ? t.lcdFg : t.lcdDim);
	}

	void drawIcon(NVGcontext* const vg, TransportIcon icon, math::Vec c, NVGcolor color) {
		nvgFillColor(vg, color);
		nvgStrokeColor(vg, color);
		nvgStrokeWidth(vg, 2.f);
		nvgBeginPath(vg);
		switch (icon) {
			case ICON_STOP:
				nvgRect(vg, c.x - 6.f, c.y - 6.f, 12.f, 12.f);
				nvgFill(vg);
				break;
			case ICON_PLAY:
				nvgMoveTo(vg, c.x - 5.f, c.y - 8.f);
				nvgLineTo(vg, c.x + 8.f, c.y);
				nvgLineTo(vg, c.x - 5.f, c.y + 8.f);
				nvgClosePath(vg);
				nvgFill(vg);
				break;
			case ICON_REC:
				nvgCircle(vg, c.x, c.y, 6.5f);
				nvgFill(vg);
				break;
			case ICON_LOOP:
				nvgRoundedRect(vg, c.x - 8.f, c.y - 5.f, 16.f, 10.f, 4.f);
				nvgStroke(vg);
				break;
			case ICON_CLICK:
				nvgMoveTo(vg, c.x - 6.f, c.y + 8.f);
				nvgLineTo(vg, c.x - 2.f, c.y - 8.f);
				nvgLineTo(vg, c.x + 2.f, c.y - 8.f);
				nvgLineTo(vg, c.x + 6.f, c.y + 8.f);
				nvgClosePath(vg);
				nvgMoveTo(vg, c.x, c.y + 3.f);
				nvgLineTo(vg, c.x + 6.f, c.y - 6.f);
				nvgStroke(vg);
				break;
		}
	}

	void drawSegments(NVGcontext* const vg, math::Vec pos, int count, float value, bool colorZones) {
		const int lit = (int) std::round(math::clamp(value, 0.f, 1.f) * count);
		for (int i = 0; i < count; ++i) {
			NVGcolor color = nvgRGB(0x1d, 0x2a, 0x20);
			if (i < lit) {
				color = nvgRGB(0x55, 0xe0, 0x7c);
				if (colorZones && i >= count * 0.85f)
					color = nvgRGB(0xef, 0x4a, 0x3c);
				else if (colorZones && i >= count * 0.7f)
					color = nvgRGB(0xf2, 0xc2, 0x30);
			}
			nvgBeginPath(vg);
			nvgRoundedRect(vg, pos.x + i * 7.f, pos.y, 5.f, 10.f, 1.f);
			nvgFillColor(vg, color);
			nvgFill(vg);
		}
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();
		CardinalPluginContext* const context = pluginContext();
		const bool internal = context->internalTransportActive;
		const bool valid = context->bbtValid;

		drawBarBackground(vg, box.size, true);

		// Tempo and signature
		drawLabel(vg, tempoRect, "TEMPO");
		drawLcd(vg, tempoRect, 22.f, valid ? string::f("%.3f", context->beatsPerMinute) : "---.---", true);
		for (int i = 0; i < 2; ++i) {
			const bool up = i == 0;
			const math::Rect& r = up ? tempoUpRect : tempoDownRect;
			draw::bevelBox(vg, r, 2.f, nvgRGB(0x45, 0x48, 0x4f), nvgRGB(0x30, 0x33, 0x3a), nvgRGB(0, 0, 0));
			const math::Vec c = r.getCenter();
			nvgBeginPath(vg);
			nvgMoveTo(vg, c.x - 3.f, c.y + (up ? 1.5f : -1.5f));
			nvgLineTo(vg, c.x + 3.f, c.y + (up ? 1.5f : -1.5f));
			nvgLineTo(vg, c.x, c.y + (up ? -2.f : 2.f));
			nvgClosePath(vg);
			nvgFillColor(vg, internal ? t.textDim : t.textFaint);
			nvgFill(vg);
		}
		drawLabel(vg, sigRect, "SIG");
		drawLcd(vg, sigRect, 15.f, valid ? string::f("%d/%d", context->beatsPerBar, context->beatType) : "-/-", true);

		// Transport buttons
		draw::insetBox(vg, buttonsRect, 8.f, nvgRGB(0x12, 0x13, 0x15));
		for (int i = 0; i < 5; ++i) {
			const math::Rect& r = buttonRects[i];
			const bool playOn = i == ICON_PLAY && context->playing;
			// Record, loop and click have no engine behind them yet
			const bool available = internal && (i == ICON_STOP || i == ICON_PLAY);
			if (playOn)
				draw::bevelBox(vg, r, 5.f, nvgRGB(0x3e, 0x9f, 0x60), nvgRGB(0x25, 0x6b, 0x3e), nvgRGB(0, 0, 0));
			else
				draw::bevelBox(vg, r, 5.f, nvgRGB(0x4a, 0x4d, 0x54), nvgRGB(0x2f, 0x32, 0x37), nvgRGB(0, 0, 0));

			NVGcolor color = color::WHITE;
			if (i == ICON_REC)
				color = t.rec;
			else if (i == ICON_LOOP || i == ICON_CLICK)
				color = t.accent;
			if (!available && !playOn)
				color = color::alpha(color, internal ? 0.3f : 0.55f);
			drawIcon(vg, (TransportIcon) i, r.getCenter(), color);
		}

		// Position and time
		drawLabel(vg, posRect, "POSITION · BARS");
		std::string pos = "-.-.-.---";
		if (valid && context->ticksPerBeat > 0.0) {
			const double ticksPer16th = context->ticksPerBeat / 4.0;
			const int sixteenth = (int) (context->tick / ticksPer16th);
			const int rest = (int) ((context->tick - sixteenth * ticksPer16th) / ticksPer16th * 1000.0);
			pos = string::f("%d.%d.%d.%03d", context->bar, context->beat, sixteenth + 1, rest);
		}
		drawLcd(vg, posRect, 22.f, pos, true);

		drawLabel(vg, timeRect, "TIME");
		const double seconds = context->sampleRate > 0.0 ? context->frame / context->sampleRate : 0.0;
		const int ms = (int) (std::fmod(seconds, 1.0) * 1000.0);
		const int whole = (int) seconds;
		drawLcd(vg, timeRect, 15.f, string::f("%02d:%02d:%02d.%03d", whole / 3600, (whole / 60) % 60, whole % 60, ms), true);

		// DSP load
		draw::text(vg, labelFont(), 9.f, t.textDim, math::Vec(dspRect.pos.x, dspRect.pos.y - 5.f),
		           NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, "DSP", 1.4f);
		drawSegments(vg, math::Vec(dspRect.pos.x, dspRect.pos.y + 3.f), 12, APP->engine->getMeterAverage(), true);

		// Master output level, -60..0 dB
		draw::text(vg, labelFont(), 9.f, t.textDim, math::Vec(masterRect.pos.x, masterRect.pos.y - 3.f),
		           NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, "MASTER", 1.4f);
		for (int c = 0; c < 2; ++c) {
			const float peak = context->outputPeaks[c];
			const float db = peak > 0.f ? 20.f * std::log10(peak) : -60.f;
			drawSegments(vg, math::Vec(masterRect.pos.x, masterRect.pos.y + 2.f + c * 13.f), 20, (db + 60.f) / 60.f, true);
		}

		// Engine and host info
		const std::string rate = string::f("%g kHz  ·  %u smp", context->sampleRate / 1000.0, context->bufferSize);
		const std::string host = internal ? "Standalone  ·  Internal clock"
		                                  : string::f("%s  ·  Host sync", getPluginFormatName());
		draw::text(vg, uiFont(), 11.f, t.textFaint, math::Vec(box.size.x - 16.f, box.size.y * 0.5f - 8.f),
		           NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, rate);
		draw::text(vg, uiFont(), 11.f, t.textFaint, math::Vec(box.size.x - 16.f, box.size.y * 0.5f + 10.f),
		           NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, host);
	}
};


widget::Widget* createTransportBar() {
	TransportBar* const bar = new TransportBar;
	bar->box.size.y = 74.f;
	return bar;
}


} // namespace patchcore
} // namespace rack

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
#include <system.hpp>
#include <widget/OpaqueWidget.hpp>

#include "../CardinalCommon.hpp"
#include "../CardinalPluginContext.hpp"
#include "../patchcore/PatchCore.hpp"
#include "../patchcore/TransportLayout.hpp"
#include "DistrhoPluginUtils.hpp"
#include "DistrhoStandaloneUtils.hpp"

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
	math::Rect tempoRect, tempoUpRect, tempoDownRect, sigRect, buttonsRect, posRect, timeRect, dspRect, masterRect, playRect;
	/** Standalone: full transport; plugin: thin read-only bar (the DAW owns transport and audio) */
	const TransportLayout layout = transportLayout(rack::isStandalone());
	math::Rect buttonRects[5];
	bool draggingTempo = false;
	bool showDsp = true, showMaster = true;

	/** Master peak meter in dBFS: instant attack, falls 20 dB in 1.7 s, peak held 1.5 s, CLIP lit 2 s */
	float level[2] = {}, hold[2] = {}, holdTime[2] = {}, clipTime[2] = {};
	double lastTime = 0.0;

	void updateMeters() {
		const double now = system::getTime();
		const float dt = lastTime > 0.0 ? (float) std::min(now - lastTime, 0.5) : 0.f;
		lastTime = now;
		CardinalPluginContext* const context = pluginContext();
		for (int c = 0; c < 2; ++c) {
			const float peak = context->outputPeaks[c];
			context->outputPeaks[c] = 0.f;
			level[c] = std::max(peak, level[c] * std::pow(10.f, -dt / 1.7f));
			if (peak >= hold[c]) {
				hold[c] = peak;
				holdTime[c] = 1.5f;
			}
			else if ((holdTime[c] -= dt) <= 0.f)
				hold[c] = level[c];
			clipTime[c] = peak >= 1.f ? 2.f : std::max(0.f, clipTime[c] - dt);
		}
	}

	static float toDb(float a) {
		return a > 0.f ? std::max(20.f * std::log10(a), -60.f) : -60.f;
	}

	void step() override {
		if (!layout.buttons) {
			stepThin();
			return;
		}
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

		// Right side: engine info text (about 200 px), the master meter, the DSP meter; whatever does not fit
		// next to the time display is left out
		const float right = box.size.x - 16.f;
		masterRect = math::Rect(right - 215.f - kMasterW, cy - 12.f, kMasterW, 24.f);
		dspRect = math::Rect(masterRect.pos.x - 110.f, cy - 8.f, 82.f, 16.f);
		const float minX = timeRect.pos.x + timeRect.size.x + 20.f;
		showMaster = masterRect.pos.x >= minX;
		showDsp = showMaster && dspRect.pos.x >= minX;
		updateMeters();

		OpaqueWidget::step();
	}

	/** One row, everything centred: tempo, signature, position, playing lamp; DSP and MASTER on the right */
	void stepThin() {
		const float cy = box.size.y * 0.5f;
		float x = 16.f;
		tempoRect = math::Rect(x, cy - 11.f, 108.f, 22.f);
		x += 108.f + 10.f;
		sigRect = math::Rect(x, cy - 11.f, 64.f, 22.f);
		x += 64.f + 10.f;
		posRect = math::Rect(x, cy - 11.f, 136.f, 22.f);
		x += 136.f + 12.f;
		playRect = math::Rect(x, cy - 6.f, 12.f, 12.f);
		const float right = box.size.x - 16.f;
		masterRect = math::Rect(right - 130.f - kMasterW, cy - 10.f, kMasterW, 20.f);
		dspRect = math::Rect(masterRect.pos.x - 150.f, cy - 5.f, 82.f, 10.f);
		const float minX = playRect.pos.x + playRect.size.x + 70.f;
		showMaster = masterRect.pos.x >= minX;
		showDsp = showMaster && dspRect.pos.x >= minX;
		updateMeters();
		OpaqueWidget::step();
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		// Plugin: read-only, the host starts and stops
		if (!layout.buttons)
			return;
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

	/** 24 segments of 2 dB (−48..0 dBFS) and the CLIP lamp */
	static constexpr float kMasterW = 24 * 7.f + 26.f;

	void drawMaster(NVGcontext* const vg, const Theme& t) {
		const int count = 24;
		const bool thin = !layout.buttons;
		if (thin)
			draw::text(vg, labelFont(), 9.f, t.textDim, math::Vec(masterRect.pos.x - 8.f, masterRect.getCenter().y),
			           NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, "MASTER", 1.4f);
		else
			draw::text(vg, labelFont(), 9.f, t.textDim, math::Vec(masterRect.pos.x, masterRect.pos.y - 3.f),
			           NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, "MASTER  dBFS", 1.4f);
		for (int c = 0; c < 2; ++c) {
			const float y = masterRect.pos.y + (thin ? 0.5f : 2.f) + c * (thin ? 10.f : 12.f);
			const float db = toDb(level[c]), holdDb = toDb(hold[c]);
			const int holdSeg = (int) std::ceil((holdDb + 48.f) / 2.f) - 1;
			for (int i = 0; i < count; ++i) {
				const float segDb = -48.f + 2.f * (i + 1);  // upper edge of the segment
				NVGcolor color = segDb > -6.f ? nvgRGB(0xef, 0x4a, 0x3c) : segDb > -18.f ? nvgRGB(0xf2, 0xc2, 0x30) : nvgRGB(0x55, 0xe0, 0x7c);
				const bool on = db > segDb - 2.f || (i == holdSeg && holdDb > -48.f);
				if (!on)
					color = nvgRGB(0x1d, 0x2a, 0x20);
				nvgBeginPath(vg);
				nvgRoundedRect(vg, masterRect.pos.x + i * 7.f, y, 5.f, 9.f, 1.f);
				nvgFillColor(vg, color);
				nvgFill(vg);
			}
			// CLIP: the output reached 0 dBFS
			nvgBeginPath(vg);
			nvgRoundedRect(vg, masterRect.pos.x + count * 7.f + 3.f, y, 18.f, 9.f, 1.5f);
			nvgFillColor(vg, clipTime[c] > 0.f ? nvgRGB(0xff, 0x3b, 0x2f) : nvgRGB(0x3a, 0x1c, 0x1a));
			nvgFill(vg);
		}
		if (thin)
			return;
		draw::text(vg, labelFont(), 7.f, t.textFaint, math::Vec(masterRect.pos.x + count * 7.f + 12.f, masterRect.pos.y - 3.f),
		           NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM, "CLIP", 0.8f);
		// Scale under the bars
		const float marks[] = {-48.f, -36.f, -24.f, -18.f, -12.f, -6.f, 0.f};
		for (const float m : marks) {
			const float x = masterRect.pos.x + (m + 48.f) / 2.f * 7.f - (m > -48.f ? 1.f : -2.5f);
			draw::text(vg, labelFont(), 7.f, t.textFaint, math::Vec(x, masterRect.pos.y + 26.f), NVG_ALIGN_CENTER | NVG_ALIGN_TOP,
			           string::f("%d", (int) m), 0.f);
		}
	}

	/** An LCD with its label inside on the left (the thin bar has no room above) */
	void drawInlineLcd(NVGcontext* const vg, const math::Rect& r, const char* label, const std::string& text) {
		drawLcd(vg, r, 14.f, text, true);
		draw::text(vg, labelFont(), 8.f, theme().textDim, math::Vec(r.pos.x + 7.f, r.getCenter().y),
		           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, label, 1.2f);
	}

	void drawThin(NVGcontext* const vg, const Theme& t, CardinalPluginContext* const context) {
		const bool valid = context->bbtValid;
		drawInlineLcd(vg, tempoRect, "BPM", valid ? string::f("%.2f", context->beatsPerMinute) : "---.--");
		drawInlineLcd(vg, sigRect, "SIG", valid ? string::f("%d/%d", context->beatsPerBar, context->beatType) : "-/-");
		std::string pos = "-.-.-";
		if (valid)
			pos = string::f("%d.%d.%d", context->bar, context->beat, (int) (context->tick / std::max(1.0, context->ticksPerBeat / 4.0)) + 1);
		drawInlineLcd(vg, posRect, "BARS", pos);

		// Playing lamp: the host's transport state
		nvgBeginPath(vg);
		nvgCircle(vg, playRect.getCenter().x, playRect.getCenter().y, 5.f);
		nvgFillColor(vg, context->playing ? nvgRGB(0x55, 0xe0, 0x7c) : nvgRGB(0x1d, 0x2a, 0x20));
		nvgFill(vg);

		if (showDsp) {
			const float load = APP->engine->getMeterAverage();
			draw::text(vg, labelFont(), 9.f, t.textDim, math::Vec(dspRect.pos.x - 8.f, dspRect.getCenter().y),
			           NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, string::f("DSP %d %%", (int) std::round(load * 100.f)), 1.2f);
			drawSegments(vg, dspRect.pos, 12, load, true);
		}
		if (showMaster)
			drawMaster(vg, t);
		draw::text(vg, uiFont(), 11.f, t.textFaint, math::Vec(box.size.x - 16.f, box.size.y * 0.5f),
		           NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, string::f("%s  ·  Host sync", getPluginFormatName()));
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();
		CardinalPluginContext* const context = pluginContext();
		const bool internal = context->internalTransportActive;
		const bool valid = context->bbtValid;

		drawBarBackground(vg, box.size, true);
		if (!layout.buttons) {
			drawThin(vg, t, context);
			return;
		}

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

		// DSP load: share of the audio block time the engine needs
		if (showDsp) {
			const float load = APP->engine->getMeterAverage();
			draw::text(vg, labelFont(), 9.f, t.textDim, math::Vec(dspRect.pos.x, dspRect.pos.y - 5.f),
			           NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, string::f("DSP  %d %%", (int) std::round(load * 100.f)), 1.4f);
			drawSegments(vg, math::Vec(dspRect.pos.x, dspRect.pos.y + 3.f), 12, load, true);
		}

		// Master output peak level
		if (showMaster)
			drawMaster(vg, t);

		// Engine info: rate, buffer and driver; the sound card (or the clock) below; red when the device was lost
		const ::patchcore_audio::AudioSetup audio = DISTRHO_NAMESPACE::getAudioSetup();
		std::string line1 = string::f("%g kHz  ·  %u smp  ·  %s", context->sampleRate / 1000.0, context->bufferSize,
		                              ::patchcore_audio::driverName(audio.driver));
		std::string line2 = !audio.output.empty() ? audio.output : internal ? "Internal clock" : "JACK transport";
		NVGcolor color = t.textFaint;
		if (!DISTRHO_NAMESPACE::isAudioRunning()) {
			line1 = "Audio stopped";
			line2 = "Engine → Audio Settings…";
			color = t.rec;
		}
		const float maxW = 196.f;
		while (line2.size() > 4 && textWidth(uiFont(), 11.f, line2, 0.f) > maxW)
			line2 = line2.substr(0, line2.size() - 4) + "…";
		draw::text(vg, uiFont(), 11.f, color, math::Vec(box.size.x - 16.f, box.size.y * 0.5f - 8.f),
		           NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, line1);
		draw::text(vg, uiFont(), 11.f, color, math::Vec(box.size.x - 16.f, box.size.y * 0.5f + 10.f),
		           NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, line2);
	}
};


widget::Widget* createTransportBar() {
	TransportBar* const bar = new TransportBar;
	bar->box.size.y = bar->layout.height;
	return bar;
}


} // namespace patchcore
} // namespace rack

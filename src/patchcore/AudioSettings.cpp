/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Audio Settings dialog of the standalone (driver, devices, sample rate, buffer, test tone), the notice strip
// under the top bar, and the restart that switches the audio driver.
// Built once per plugin variant (like PatchCoreUI.cpp), through a symlink in each variant folder: it calls
// DPF's standalone utilities, which are empty in plugins.

#include <app/Scene.hpp>
#include <asset.hpp>
#include <context.hpp>
#include <helpers.hpp>
#include <history.hpp>
#include <logger.hpp>
#include <patch.hpp>
#include <settings.hpp>
#include <string.hpp>
#include <system.hpp>
#include <ui/Menu.hpp>
#include <widget/OpaqueWidget.hpp>
#include <window/Window.hpp>

#include <algorithm>
#include <cmath>

#ifdef ARCH_WIN
# include <windows.h>
#else
# include <unistd.h>
#endif

#include "../AsyncDialog.hpp"
#include "../CardinalCommon.hpp"
#include "../CardinalPluginContext.hpp"
#include "../patchcore/PatchCore.hpp"
#include "DistrhoStandaloneUtils.hpp"

namespace rack {
namespace patchcore {

namespace pa = ::patchcore_audio;

static const unsigned kStandardRates[] = {44100, 48000, 88200, 96000};

/** Extra notices (besides the audio start-up notice), shown in the strip under the top bar */
static std::string sNotice;
static bool sNoticeDismissed = false;


std::string audioNotice() {
	if (sNoticeDismissed)
		return "";
	if (!sNotice.empty())
		return sNotice;
	return DISTRHO_NAMESPACE::getAudioNotice();
}


void showNotice(const std::string& text) {
	sNotice = text;
	sNoticeDismissed = false;
}


void restartApp() {
	// The patch goes through a temporary file; the original path and the unsaved state come back after the restart
	const std::string tmp = system::join(asset::user(""), "patchcore-restart.vcv");
	const std::string original = "path:" + APP->patch->path;
	const bool modified = !APP->history->isSaved();
	try {
		APP->patch->save(tmp);
	}
	catch (Exception& e) {
		showNotice(std::string("Could not keep the patch for the restart: ") + e.what());
		return;
	}
#ifdef ARCH_WIN
	SetEnvironmentVariableA("PATCHCORE_DEV_PATCH", tmp.c_str());
	SetEnvironmentVariableA("PATCHCORE_RESTORE_PATH", original.c_str());
	SetEnvironmentVariableA("PATCHCORE_RESTORE_MODIFIED", modified ? "1" : "0");
	// The new instance waits for this one to exit (and free the audio device) before it opens audio
	SetEnvironmentVariableA("PATCHCORE_RESTART_WAIT_PID", std::to_string(GetCurrentProcessId()).c_str());
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring cmd = L"\"";
	cmd += exe;
	cmd += L"\"";
	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		APP->window->close();
		return;
	}
#else
	// exec() skips the normal shutdown: save the app settings first
	try {
		settings::save();
	}
	catch (Exception& e) {
		WARN("PatchCore: could not save the settings before the restart: %s", e.what());
	}
	saveSettings();
	setenv("PATCHCORE_DEV_PATCH", tmp.c_str(), 1);
	setenv("PATCHCORE_RESTORE_PATH", original.c_str(), 1);
	setenv("PATCHCORE_RESTORE_MODIFIED", modified ? "1" : "0", 1);
	char exe[4096];
	const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = '\0';
		execl(exe, exe, (char*) nullptr);
	}
#endif
	showNotice("Could not restart PatchCore. Close and start it again to switch the audio driver.");
}


// ---------------------------------------------------------------------------------------------------------
// Dialog

struct AudioSettingsDialog : widget::OpaqueWidget {
	enum Row { ROW_DRIVER, ROW_OUTPUT, ROW_INPUT, ROW_RATE, ROW_BUFFER, NUM_ROWS };
	enum Button { BTN_TEST, BTN_ASIO, BTN_APPLY, BTN_CLOSE, NUM_BUTTONS };

	pa::AudioSetup edit;
	pa::Driver runningDriver;
	std::vector<pa::DeviceInfo> devices;
	std::string error;
	math::Rect rowRects[NUM_ROWS];
	math::Rect buttonRects[NUM_BUTTONS];

	static constexpr float kW = 440.f, kH = 340.f, kRowH = 34.f;

	AudioSettingsDialog() {
		box.size = math::Vec(kW, kH);
		edit = DISTRHO_NAMESPACE::getAudioSetup();
		runningDriver = edit.driver;
		devices = DISTRHO_NAMESPACE::getAudioDevices(edit.driver);
		for (int i = 0; i < NUM_ROWS; ++i)
			rowRects[i] = math::Rect(150.f, 58.f + i * kRowH, kW - 170.f, 26.f);
		const float by = kH - 48.f;
		buttonRects[BTN_TEST] = math::Rect(20.f, by, 90.f, 30.f);
		buttonRects[BTN_ASIO] = math::Rect(118.f, by, 100.f, 30.f);
		buttonRects[BTN_APPLY] = math::Rect(kW - 200.f, by, 86.f, 30.f);
		buttonRects[BTN_CLOSE] = math::Rect(kW - 106.f, by, 86.f, 30.f);
	}

	const pa::DeviceInfo* device(const std::string& name) const {
		for (const pa::DeviceInfo& d : devices)
			if (d.name == name)
				return &d;
		return nullptr;
	}

	std::vector<unsigned> rates() const {
		std::vector<unsigned> out;
		const pa::DeviceInfo* const d = edit.output.empty() ? nullptr : device(edit.output);
		for (const unsigned r : kStandardRates) {
			if (d == nullptr || d->sampleRates.empty() || std::find(d->sampleRates.begin(), d->sampleRates.end(), r) != d->sampleRates.end())
				out.push_back(r);
		}
		return out;
	}

	std::string rowLabel(int row) const {
		switch (row) {
			case ROW_DRIVER: return "Driver";
			case ROW_OUTPUT: return "Output";
			case ROW_INPUT: return "Input";
			case ROW_RATE: return "Sample rate";
			default: return "Buffer";
		}
	}

	std::string rowValue(int row) const {
		switch (row) {
			case ROW_DRIVER: return pa::driverName(edit.driver);
			case ROW_OUTPUT:
				if (edit.driver == pa::Driver::Jack)
					return "PipeWire / JACK default output";
				return edit.output.empty() ? "System default" : edit.output;
			case ROW_INPUT:
				if (edit.driver == pa::Driver::Jack)
					return "PipeWire / JACK";
				return edit.input.empty() ? "Off" : edit.input;
			case ROW_RATE: return string::f("%g kHz", edit.sampleRate / 1000.0);
			default: return string::f("%u samples  (%.1f ms)", edit.bufferSize, pa::latencyMs(edit));
		}
	}

	/** The device rows list nothing for PipeWire/JACK (the server routes) and for a driver that is not running yet */
	bool rowEnabled(int row) const {
		if (row == ROW_OUTPUT || row == ROW_INPUT)
			return edit.driver != pa::Driver::Jack && edit.driver == runningDriver;
		// PipeWire/JACK: the server sets the rate
		if (row == ROW_RATE)
			return edit.driver != pa::Driver::Jack;
		return true;
	}

	void openRowMenu(int row) {
		ui::Menu* const menu = createMenu();
		switch (row) {
			case ROW_DRIVER:
				for (const pa::Driver d : DISTRHO_NAMESPACE::getAvailableAudioDrivers()) {
					menu->addChild(createCheckMenuItem(pa::driverName(d), "", [=]() { return edit.driver == d; }, [=]() {
						edit.driver = d;
						if (d != runningDriver) {
							edit.output.clear();
							edit.input.clear();
						}
					}));
				}
				break;
			case ROW_OUTPUT:
				menu->addChild(createCheckMenuItem("System default", "", [=]() { return edit.output.empty(); }, [=]() { edit.output.clear(); }));
				for (const pa::DeviceInfo& dev : devices) {
					if (dev.outputs == 0)
						continue;
					const std::string name = dev.name;
					menu->addChild(createCheckMenuItem(name, "", [=]() { return edit.output == name; }, [=]() {
						edit.output = name;
						const std::vector<unsigned> r = rates();
						edit.sampleRate = pa::nearest(r, edit.sampleRate);
					}));
				}
				break;
			case ROW_INPUT:
				menu->addChild(createCheckMenuItem("Off", "", [=]() { return edit.input.empty(); }, [=]() { edit.input.clear(); }));
				for (const pa::DeviceInfo& dev : devices) {
					if (dev.inputs == 0)
						continue;
					const std::string name = dev.name;
					menu->addChild(createCheckMenuItem(name, "", [=]() { return edit.input == name; }, [=]() { edit.input = name; }));
				}
				break;
			case ROW_RATE:
				for (const unsigned r : rates())
					menu->addChild(createCheckMenuItem(string::f("%g kHz", r / 1000.0), "", [=]() { return edit.sampleRate == r; },
					                                   [=]() { edit.sampleRate = r; }));
				break;
			default:
				for (const unsigned b : pa::kBufferSizes)
					menu->addChild(createCheckMenuItem(string::f("%u samples", b), "", [=]() { return edit.bufferSize == b; },
					                                   [=]() { edit.bufferSize = b; }));
				break;
		}
	}

	void apply() {
		std::string err;
		if (DISTRHO_NAMESPACE::requestAudioSetup(edit, &err)) {
			error.clear();
			// A new audio setup starts the song again from 1.1.1 (00:00); the frame count kept across a rate change
			// would show a time that jumped only part of the way back
			static_cast<CardinalPluginContext*>(APP)->internalTransportResetRequested = true;
			edit = DISTRHO_NAMESPACE::getAudioSetup();
			devices = DISTRHO_NAMESPACE::getAudioDevices(edit.driver);
			return;
		}
		if (err == "restart") {
			const pa::AudioSetup wanted = edit;
			asyncDialog::create("PatchCore restarts to switch the audio driver. The patch stays open.", [wanted]() {
				if (!pa::saveSetup(wanted)) {
					showNotice("Could not save the audio settings to " + pa::configPath());
					return;
				}
				restartApp();
			});
			return;
		}
		error = err.empty() ? "The audio settings could not be applied" : err;
		edit = DISTRHO_NAMESPACE::getAudioSetup();  // back to what runs
	}

	void testTone() {
		CardinalPluginContext* const context = static_cast<CardinalPluginContext*>(APP);
		context->testToneFrames = (int32_t) (context->sampleRate * 0.5);
	}

	/** Closes the whole dialog (the overlay that holds this panel) */
	void close() {
		if (parent != nullptr)
			parent->requestDelete();
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		e.consume(this);
		for (int i = 0; i < NUM_ROWS; ++i) {
			if (rowRects[i].contains(e.pos) && rowEnabled(i)) {
				openRowMenu(i);
				return;
			}
		}
		if (buttonRects[BTN_TEST].contains(e.pos))
			testTone();
		else if (buttonRects[BTN_ASIO].contains(e.pos) && edit.driver == pa::Driver::Asio && runningDriver == pa::Driver::Asio)
			DISTRHO_NAMESPACE::showAsioPanel();
		else if (buttonRects[BTN_APPLY].contains(e.pos))
			apply();
		else if (buttonRects[BTN_CLOSE].contains(e.pos))
			close();
	}


	void drawButton(NVGcontext* const vg, const math::Rect& r, const std::string& label, bool enabled, bool primary) {
		const Theme& t = theme();
		if (primary)
			draw::bevelBox(vg, r, 5.f, nvgRGB(0xf0, 0x9a, 0x3c), nvgRGB(0xc8, 0x76, 0x22), nvgRGB(0, 0, 0));
		else
			draw::bevelBox(vg, r, 5.f, nvgRGB(0x4a, 0x4d, 0x54), nvgRGB(0x2f, 0x32, 0x37), nvgRGB(0, 0, 0));
		draw::text(vg, uiFont(), 12.f, enabled ? (primary ? nvgRGB(0x16, 0x12, 0x0c) : t.text) : t.textFaint, r.getCenter(),
		           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, label);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();

		draw::bevelBox(vg, box.zeroPos(), 8.f, t.chrome2, t.chrome1, nvgRGB(0, 0, 0));
		draw::text(vg, labelFont(), 11.f, t.textDim, math::Vec(20.f, 26.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "AUDIO SETTINGS", 1.6f);

		for (int i = 0; i < NUM_ROWS; ++i) {
			const math::Rect& r = rowRects[i];
			const bool enabled = rowEnabled(i);
			draw::text(vg, uiFont(), 12.5f, t.textDim, math::Vec(20.f, r.getCenter().y), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, rowLabel(i));
			draw::insetBox(vg, r, 4.f, nvgRGB(0x12, 0x13, 0x15));
			nvgSave(vg);
			nvgIntersectScissor(vg, r.pos.x + 8.f, r.pos.y, r.size.x - 30.f, r.size.y);
			draw::text(vg, uiFont(), 12.f, enabled ? t.text : t.textFaint, math::Vec(r.pos.x + 10.f, r.getCenter().y),
			           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, rowValue(i));
			nvgRestore(vg);
			if (enabled) {
				// Drop-down arrow
				const math::Vec c(r.pos.x + r.size.x - 13.f, r.getCenter().y);
				nvgBeginPath(vg);
				nvgMoveTo(vg, c.x - 4.f, c.y - 2.f);
				nvgLineTo(vg, c.x + 4.f, c.y - 2.f);
				nvgLineTo(vg, c.x, c.y + 3.f);
				nvgClosePath(vg);
				nvgFillColor(vg, t.textDim);
				nvgFill(vg);
			}
		}

		// A hint or the last error
		std::string hint;
		NVGcolor hintColor = t.textFaint;
		if (!error.empty()) {
			hint = error;
			hintColor = t.rec;
		}
		else if (edit.driver != runningDriver)
			hint = std::string("Apply restarts PatchCore with ") + pa::driverName(edit.driver) + "; choose its devices then.";
		else if (edit.driver == pa::Driver::Jack)
			hint = "PipeWire / JACK sets the rate; the buffer applies to all its apps.";
		draw::text(vg, uiFont(), 11.f, hintColor, math::Vec(20.f, kH - 70.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, hint);

		drawButton(vg, buttonRects[BTN_TEST], "Test tone", true, false);
		if (edit.driver == pa::Driver::Asio)
			drawButton(vg, buttonRects[BTN_ASIO], "ASIO panel…", runningDriver == pa::Driver::Asio, false);
		drawButton(vg, buttonRects[BTN_APPLY], "Apply", true, true);
		drawButton(vg, buttonRects[BTN_CLOSE], "Close", true, false);

		OpaqueWidget::draw(args);
	}
};


/** Full-scene overlay: dims the app and swallows every click and key, so the dialog is modal */
struct AudioSettingsOverlay : widget::OpaqueWidget {
	static AudioSettingsOverlay* current;
	AudioSettingsDialog* panel;

	AudioSettingsOverlay() {
		panel = new AudioSettingsDialog;
		addChild(panel);
		current = this;
	}

	~AudioSettingsOverlay() {
		if (current == this)
			current = nullptr;
	}

	void step() override {
		if (parent != nullptr)
			box = math::Rect(math::Vec(), parent->box.size);
		panel->box.pos = box.size.minus(panel->box.size).div(2).round();
		OpaqueWidget::step();
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		e.consume(this);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			requestDelete();
			e.consume(this);
			return;
		}
		OpaqueWidget::onHoverKey(e);
		e.consume(this);  // keys do not reach the rack
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		e.consume(this);
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGBAf(0, 0, 0, 0.45f));
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}
};

AudioSettingsOverlay* AudioSettingsOverlay::current = nullptr;


void showAudioSettings() {
	if (!isStandalone() || AudioSettingsOverlay::current != nullptr)
		return;
	APP->scene->addChild(new AudioSettingsOverlay);
}


// ---------------------------------------------------------------------------------------------------------
// Notice strip under the top bar

struct NoticeStrip : widget::OpaqueWidget {
	math::Rect closeRect;

	void step() override {
		const std::string text = audioNotice();
		visible = !text.empty();
		if (APP->scene->menuBar != nullptr)
			box.pos = math::Vec(0, APP->scene->menuBar->box.size.y);
		box.size = math::Vec(APP->scene->box.size.x, 26.f);
		closeRect = math::Rect(box.size.x - 30.f, 0, 30.f, box.size.y);
		OpaqueWidget::step();
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && closeRect.contains(e.pos))
			sNoticeDismissed = true;
		e.consume(this);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(vg, nvgRGB(0x5a, 0x40, 0x12));
		nvgFill(vg);
		draw::text(vg, uiFont(), 12.f, nvgRGB(0xff, 0xd8, 0x8a), math::Vec(16.f, box.size.y / 2), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
		           audioNotice() + "   ·   Engine → Audio Settings…");
		draw::text(vg, uiFont(), 14.f, nvgRGB(0xff, 0xd8, 0x8a), closeRect.getCenter(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "×");
	}
};


widget::Widget* createNoticeStrip() {
	return new NoticeStrip;
}

} // namespace patchcore
} // namespace rack

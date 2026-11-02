/*
 * DISTRHO Cardinal Plugin
 * Copyright (C) 2021-2024 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the LICENSE file.
 */

/**
 * This file is an edited version of VCVRack's app/MenuBar.cpp
 * Copyright (C) 2016-2023 VCV.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 */

#include <thread>
#include <utility>

#include <app/MenuBar.hpp>
#include <app/TipWindow.hpp>
#include <widget/OpaqueWidget.hpp>
#include <ui/Button.hpp>
#include <ui/MenuItem.hpp>
#include <ui/MenuOverlay.hpp>
#include <ui/MenuSeparator.hpp>
#include <ui/SequentialLayout.hpp>
#include <ui/Slider.hpp>
#include <ui/TextField.hpp>
#include <ui/ProgressBar.hpp>
#include <ui/Label.hpp>
#include <engine/Engine.hpp>
#include <widget/FramebufferWidget.hpp>
#include <window/Window.hpp>
#include <asset.hpp>
#include <context.hpp>
#include <settings.hpp>
#include <helpers.hpp>
#include <system.hpp>
#include <plugin.hpp>
#include <patch.hpp>
#include <library.hpp>

#include "../CardinalCommon.hpp"
#include "../CardinalRemote.hpp"
#include "../CardinalPluginContext.hpp"
#include "../patchcore/PatchCore.hpp"
#include "DistrhoPlugin.hpp"
#include "DistrhoStandaloneUtils.hpp"

#ifdef DISTRHO_OS_WASM
# include <emscripten/emscripten.h>
# undef HAVE_LIBLO
#endif

#ifdef HAVE_LIBLO
# include <lo/lo.h>
#endif

namespace rack {
namespace asset {
std::string patchesPath();
}
namespace engine {
void Engine_setRemoteDetails(Engine*, remoteUtils::RemoteDetails*);
}

namespace app {
namespace menuBar {


struct MenuButton : ui::Button {
	// PatchCore: a menu bar as in desktop apps. The open menu covers the window, so a click on another title only
	// closes it; that title then opens right away (one click, not two). Nothing opens by just hovering.
	static MenuButton*& openButton() {
		static MenuButton* b = nullptr;
		return b;
	}
	static widget::Widget*& openOverlay() {
		static widget::Widget* o = nullptr;
		return o;
	}
	/** Set for a moment when the menu closed: the title under the mouse then opens */
	static int& switchFrames() {
		static int n = 0;
		return n;
	}
	static MenuButton*& closedButton() {
		static MenuButton* b = nullptr;
		return b;
	}

	/** Remembers the menu this button just opened (the newest overlay in the scene) */
	void rememberMenu() {
		openButton() = nullptr;
		openOverlay() = nullptr;
		for (auto it = APP->scene->children.rbegin(); it != APP->scene->children.rend(); ++it) {
			if (dynamic_cast<ui::MenuOverlay*>(*it)) {
				openButton() = this;
				openOverlay() = *it;
				return;
			}
		}
	}

	static bool menuStillOpen() {
		if (openOverlay() == nullptr)
			return false;
		for (widget::Widget* const w : APP->scene->children)
			if (w == openOverlay())
				return true;
		return false;
	}

	bool mouseOver() {
		return math::Rect(getAbsoluteOffset(math::Vec()), box.size).contains(APP->scene->getMousePos());
	}

	void onDragDrop(const DragDropEvent& e) override {
		ui::Button::onDragDrop(e);
		if (e.origin == this)
			rememberMenu();
	}

	void step() override {
		if (patchcore::enabled())
			box.size.x = patchcore::menuButtonWidth(text);
		else
			box.size.x = bndLabelWidth(APP->window->vg, -1, text.c_str()) + 1.0;

		// The menu just closed (a click outside it): remember for two frames which title it belonged to
		if (openButton() != nullptr && !menuStillOpen()) {
			closedButton() = openButton();
			openButton() = nullptr;
			openOverlay() = nullptr;
			switchFrames() = 2;
		}
		static double lastFrame = -1.0;
		if (APP->window->getFrameTime() != lastFrame) {
			lastFrame = APP->window->getFrameTime();
			if (switchFrames() > 0)
				--switchFrames();
		}
		if (switchFrames() > 0 && closedButton() != this && mouseOver()) {
			switchFrames() = 0;
			ActionEvent eAction;
			onAction(eAction);
			rememberMenu();
		}
		Widget::step();
	}

	void draw(const DrawArgs& args) override {
		if (patchcore::enabled()) {
			// While a menu is open the overlay takes the hover: highlight its title and the one under the mouse
			const bool menuOpen = openButton() != nullptr && menuStillOpen();
			const bool hovered = APP->event->hoveredWidget == this || APP->event->draggedWidget == this
			                  || (menuOpen && (openButton() == this || mouseOver()));
			patchcore::drawMenuButton(args.vg, box.size, text, hovered);
			Widget::draw(args);
			return;
		}
		BNDwidgetState state = BND_DEFAULT;
		if (APP->event->hoveredWidget == this)
			state = BND_HOVER;
		if (APP->event->draggedWidget == this)
			state = BND_ACTIVE;
		bndMenuItem(args.vg, 0.0, 0.0, box.size.x, box.size.y, state, -1, text.c_str());
		Widget::draw(args);
	}
};


////////////////////
// File
////////////////////


struct FileButton : MenuButton {
	const bool isStandalone;
	std::vector<std::string> demoPatches;

#ifdef DISTRHO_OS_WASM
	static void WebBrowserDataSaved(const int err)
	{
		err ? async_dialog_message("Error, could not save web browser data!")
		    : async_dialog_message("Web browser data saved!");
	}

	static void wasmSaveAs()
	{
		async_dialog_text_input("Filename", nullptr, [](char* const filename) {
			if (filename == nullptr)
				return;

			APP->patch->path = asset::user("patches");
			system::createDirectories(APP->patch->path);

			APP->patch->path += filename;
			if (rack::system::getExtension(filename) != ".vcv")
				APP->patch->path += ".vcv";

			patchUtils::saveDialog(APP->patch->path);
			std::free(filename);
		});
	}
#endif

	FileButton(const bool standalone)
		: MenuButton(),  isStandalone(standalone)
	{
#if CARDINAL_VARIANT_MINI
		const std::string patchesDir = asset::patchesPath() + DISTRHO_OS_SEP_STR "mini";
#else
		const std::string patchesDir = asset::patchesPath() + DISTRHO_OS_SEP_STR "examples";
#endif

		if (system::isDirectory(patchesDir))
		{
			demoPatches = system::getEntries(patchesDir);
			std::sort(demoPatches.begin(), demoPatches.end(), [](const std::string& a, const std::string& b){
				return string::lowercase(a) < string::lowercase(b);
			});
		}
	}

	void onAction(const ActionEvent& e) override {
		ui::Menu* menu = createMenu();
		menu->cornerFlags = BND_CORNER_TOP;
		menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));

#ifndef DISTRHO_OS_WASM
		constexpr const char* const NewShortcut = RACK_MOD_CTRL_NAME "+N";
#else
		constexpr const char* const NewShortcut = "";
#endif
		menu->addChild(createMenuItem("New", NewShortcut, []() {
			patchUtils::loadTemplateDialog(false);
		}));

#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
#ifndef DISTRHO_OS_WASM
		constexpr const char* const OpenName = "Open...";
#else
		constexpr const char* const OpenName = "Import patch...";
#endif
		menu->addChild(createMenuItem(OpenName, RACK_MOD_CTRL_NAME "+O", []() {
			patchUtils::loadDialog();
		}));

		const std::string patchesDir = asset::user("patches");
		const std::vector<std::string> patches = system::isDirectory(patchesDir) ? system::getEntries(patchesDir) : std::vector<std::string>();
		menu->addChild(createSubmenuItem("Open local patch", "", [patches](ui::Menu* menu) {
			for (const std::string& path : patches) {
				std::string name = system::getStem(path);
				menu->addChild(createMenuItem(name, "", [=]() {
					patchUtils::loadPathDialog(path, false);
				}));
			}
		}, patches.empty()));

		menu->addChild(createSubmenuItem("Open recent", "", [](ui::Menu* menu) {
			for (const std::string& path : settings::recentPatchPaths) {
				std::string name = system::getStem(path);
				menu->addChild(createMenuItem(name, "", [=]() {
					patchUtils::loadPathDialog(path, false);
				}));
			}
		}, settings::recentPatchPaths.empty()));
#endif

		if (!demoPatches.empty())
		{
			menu->addChild(createSubmenuItem("Open demo / example project", "", [=](ui::Menu* const menu) {
				for (std::string path : demoPatches) {
					std::string label = system::getStem(path);

					for (size_t i=0, len=label.size(); i<len; ++i) {
						if (label[i] == '_')
							label[i] = ' ';
					}

					menu->addChild(createMenuItem(label, "", [path]() {
						patchUtils::loadPathDialog(path, true);
					}));
				}

			}));
		}

#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
		menu->addChild(createMenuItem("Import selection...", "", [=]() {
			patchUtils::loadSelectionDialog();
		}, false, true));

		menu->addChild(new ui::MenuSeparator);

#ifndef DISTRHO_OS_WASM
		menu->addChild(createMenuItem("Save", RACK_MOD_CTRL_NAME "+S", []() {
			// NOTE: for plugin versions it will do nothing if path is empty, intentionally
			patchUtils::saveDialog(APP->patch->path);
		}, APP->patch->path.empty() && !isStandalone));

		menu->addChild(createMenuItem("Save as / Export...", RACK_MOD_CTRL_NAME "+Shift+S", []() {
			patchUtils::saveAsDialog();
		}));
#else
		menu->addChild(createMenuItem("Save", "", []() {
			if (APP->patch->path.empty())
				wasmSaveAs();
			else
				patchUtils::saveDialog(APP->patch->path);
		}));

		menu->addChild(createMenuItem("Save as...", "", []() {
			wasmSaveAs();
		}));

		menu->addChild(createMenuItem("Save and download compressed", "", []() {
			patchUtils::saveAsDialog();
		}));

		menu->addChild(createMenuItem("Save and download uncompressed", "", []() {
			patchUtils::saveAsDialogUncompressed();
		}));
#endif
#endif

		menu->addChild(createMenuItem("Revert", RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+O", []() {
			patchUtils::revertDialog();
		}, APP->patch->path.empty()));

#ifndef DISTRHO_OS_WASM
		if (isStandalone) {
			menu->addChild(new ui::MenuSeparator);

			menu->addChild(createMenuItem("Quit", RACK_MOD_CTRL_NAME "+Q", []() {
				APP->window->close();
			}));
		}
#endif
	}
};


////////////////////
// Edit
////////////////////


struct EditButton : MenuButton {
	void onAction(const ActionEvent& e) override {
		ui::Menu* menu = createMenu();
		menu->cornerFlags = BND_CORNER_TOP;
		menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));

		struct UndoItem : ui::MenuItem {
			void step() override {
				text = "Undo " + APP->history->getUndoName();
				disabled = !APP->history->canUndo();
				MenuItem::step();
			}
			void onAction(const ActionEvent& e) override {
				APP->history->undo();
			}
		};
		menu->addChild(createMenuItem<UndoItem>("", RACK_MOD_CTRL_NAME "+Z"));

		struct RedoItem : ui::MenuItem {
			void step() override {
				text = "Redo " + APP->history->getRedoName();
				disabled = !APP->history->canRedo();
				MenuItem::step();
			}
			void onAction(const ActionEvent& e) override {
				APP->history->redo();
			}
		};
		menu->addChild(createMenuItem<RedoItem>("", RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+Z"));

		menu->addChild(createMenuItem("Clear cables", "", [=]() {
			APP->patch->disconnectDialog();
		}));

		menu->addChild(new ui::MenuSeparator);

		patchUtils::appendSelectionContextMenu(menu);
	}
};


////////////////////
// View
////////////////////


struct ZoomQuantity : Quantity {
	void setValue(float value) override {
		APP->scene->rackScroll->setZoom(std::pow(2.f, value));
	}
	float getValue() override {
		return std::log2(APP->scene->rackScroll->getZoom());
	}
	float getMinValue() override {
		return -2.f;
	}
	float getMaxValue() override {
		return 2.f;
	}
	float getDefaultValue() override {
		return 0.0;
	}
	float getDisplayValue() override {
		return std::round(std::pow(2.f, getValue()) * 100);
	}
	void setDisplayValue(float displayValue) override {
		setValue(std::log2(displayValue / 100));
	}
	std::string getLabel() override {
		return "Zoom";
	}
	std::string getUnit() override {
		return "%";
	}
};
struct ZoomSlider : ui::Slider {
	ZoomSlider() {
		quantity = new ZoomQuantity;
	}
	~ZoomSlider() {
		delete quantity;
	}
};


struct CableOpacityQuantity : Quantity {
	void setValue(float value) override {
		settings::cableOpacity = math::clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override {
		return settings::cableOpacity;
	}
	float getDefaultValue() override {
		return 0.5;
	}
	float getDisplayValue() override {
		return getValue() * 100;
	}
	void setDisplayValue(float displayValue) override {
		setValue(displayValue / 100);
	}
	std::string getLabel() override {
		return "Cable opacity";
	}
	std::string getUnit() override {
		return "%";
	}
};
struct CableOpacitySlider : ui::Slider {
	CableOpacitySlider() {
		quantity = new CableOpacityQuantity;
	}
	~CableOpacitySlider() {
		delete quantity;
	}
};


struct CableTensionQuantity : Quantity {
	void setValue(float value) override {
		settings::cableTension = math::clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override {
		return settings::cableTension;
	}
	float getDefaultValue() override {
		return 0.75;
	}
	float getDisplayValue() override {
		return getValue() * 100;
	}
	void setDisplayValue(float displayValue) override {
		setValue(displayValue / 100);
	}
	std::string getLabel() override {
		return "Cable tension";
	}
	std::string getUnit() override {
		return "%";
	}
};
struct CableTensionSlider : ui::Slider {
	CableTensionSlider() {
		quantity = new CableTensionQuantity;
	}
	~CableTensionSlider() {
		delete quantity;
	}
};


struct RackBrightnessQuantity : Quantity {
	void setValue(float value) override {
		settings::rackBrightness = math::clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override {
		return settings::rackBrightness;
	}
	float getDefaultValue() override {
		return 1.0;
	}
	float getDisplayValue() override {
		return getValue() * 100;
	}
	void setDisplayValue(float displayValue) override {
		setValue(displayValue / 100);
	}
	std::string getUnit() override {
		return "%";
	}
	std::string getLabel() override {
		return "Room brightness";
	}
	int getDisplayPrecision() override {
		return 3;
	}
};
struct RackBrightnessSlider : ui::Slider {
	RackBrightnessSlider() {
		quantity = new RackBrightnessQuantity;
	}
	~RackBrightnessSlider() {
		delete quantity;
	}
};


struct HaloBrightnessQuantity : Quantity {
	void setValue(float value) override {
		settings::haloBrightness = math::clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override {
		return settings::haloBrightness;
	}
	float getDefaultValue() override {
		return 0.25;
	}
	float getDisplayValue() override {
		return getValue() * 100;
	}
	void setDisplayValue(float displayValue) override {
		setValue(displayValue / 100);
	}
	std::string getUnit() override {
		return "%";
	}
	std::string getLabel() override {
		return "Light bloom";
	}
	int getDisplayPrecision() override {
		return 3;
	}
};
struct HaloBrightnessSlider : ui::Slider {
	HaloBrightnessSlider() {
		quantity = new HaloBrightnessQuantity;
	}
	~HaloBrightnessSlider() {
		delete quantity;
	}
};


struct KnobScrollSensitivityQuantity : Quantity {
	void setValue(float value) override {
		value = math::clamp(value, getMinValue(), getMaxValue());
		settings::knobScrollSensitivity = std::pow(2.f, value);
	}
	float getValue() override {
		return std::log2(settings::knobScrollSensitivity);
	}
	float getMinValue() override {
		return std::log2(1e-4f);
	}
	float getMaxValue() override {
		return std::log2(1e-2f);
	}
	float getDefaultValue() override {
		return std::log2(1e-3f);
	}
	float getDisplayValue() override {
		return std::pow(2.f, getValue() - getDefaultValue());
	}
	void setDisplayValue(float displayValue) override {
		setValue(std::log2(displayValue) + getDefaultValue());
	}
	std::string getLabel() override {
		return "Scroll wheel knob sensitivity";
	}
	int getDisplayPrecision() override {
		return 2;
	}
};
struct KnobScrollSensitivitySlider : ui::Slider {
	KnobScrollSensitivitySlider() {
		quantity = new KnobScrollSensitivityQuantity;
	}
	~KnobScrollSensitivitySlider() {
		delete quantity;
	}
};


#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
static void setAllFramebufferWidgetsDirty(widget::Widget* const widget)
{
	for (widget::Widget* child : widget->children)
	{
		if (widget::FramebufferWidget* const fbw = dynamic_cast<widget::FramebufferWidget*>(child))
			fbw->setDirty();

		setAllFramebufferWidgetsDirty(child);
	}
}
#endif


struct ViewButton : MenuButton {
	void onAction(const ActionEvent& e) override {
		ui::Menu* menu = createMenu();
		menu->cornerFlags = BND_CORNER_TOP;
		menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));

		menu->addChild(createMenuLabel("Appearance"));

		menu->addChild(createBoolPtrMenuItem("Show tooltips", "", &settings::tooltips));

		ZoomSlider* zoomSlider = new ZoomSlider;
		zoomSlider->box.size.x = 250.0;
		menu->addChild(zoomSlider);

		CableOpacitySlider* cableOpacitySlider = new CableOpacitySlider;
		cableOpacitySlider->box.size.x = 250.0;
		menu->addChild(cableOpacitySlider);

		CableTensionSlider* cableTensionSlider = new CableTensionSlider;
		cableTensionSlider->box.size.x = 250.0;
		menu->addChild(cableTensionSlider);

		RackBrightnessSlider* rackBrightnessSlider = new RackBrightnessSlider;
		rackBrightnessSlider->box.size.x = 250.0;
		menu->addChild(rackBrightnessSlider);

		if (!isMini()) {
			menu->addChild(new ui::MenuSeparator);
			menu->addChild(createMenuLabel("PatchCore"));

			menu->addChild(createCheckMenuItem("Mouse wheel turns knobs", "",
				[]() {return patchcore::settings.wheelKnobs;},
				[]() {patchcore::settings.wheelKnobs ^= true; patchcore::saveSettings();},
				!patchcore::enabled()
			));
			{
				static const char* const names[3] = {"Fine", "Normal", "Coarse"};
				menu->addChild(createSubmenuItem("Wheel sensitivity", names[patchcore::settings.wheelSensitivity], [](ui::Menu* menu) {
					for (int i = 0; i < 3; ++i) {
						menu->addChild(createCheckMenuItem(names[i], "",
							[=]() {return patchcore::settings.wheelSensitivity == i;},
							[=]() {patchcore::settings.wheelSensitivity = i; patchcore::saveSettings();}
						));
					}
				}, !patchcore::settings.wheelKnobs));
			}
			menu->addChild(createCheckMenuItem("Navigator", "",
				[]() {return patchcore::settings.navigatorVisible;},
				[]() {patchcore::settings.navigatorVisible ^= true; patchcore::saveSettings();},
				!patchcore::enabled()
			));
			menu->addChild(createCheckMenuItem("Transport bar", "",
				[]() {return patchcore::settings.transportVisible;},
				[]() {patchcore::settings.transportVisible ^= true; patchcore::saveSettings();},
				!patchcore::enabled()
			));
		}

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Modules"));

		menu->addChild(createBoolPtrMenuItem("Lock module positions", "", &settings::lockModules));

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Knobs"));

#ifdef DISTRHO_OS_WASM
		menu->addChild(createBoolPtrMenuItem("Lock cursor while dragging", "", &settings::allowCursorLock));
#endif

		static const std::vector<std::string> knobModeLabels = {
			"Linear",
			"Scaled linear",
			"Absolute rotary",
			"Relative rotary",
		};
		static const std::vector<int> knobModes = {0, 2, 3};
		menu->addChild(createSubmenuItem("Knob mode", knobModeLabels[settings::knobMode], [=](ui::Menu* menu) {
			for (int knobMode : knobModes) {
				menu->addChild(createCheckMenuItem(knobModeLabels[knobMode], "",
					[=]() {return settings::knobMode == knobMode;},
					[=]() {settings::knobMode = (settings::KnobMode) knobMode;}
				));
			}
		}));

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Window"));

#ifdef DISTRHO_OS_WASM
		const bool fullscreen = APP->window->isFullScreen();
		std::string rightText = "F11";
		if (fullscreen)
			rightText += " " CHECKMARK_STRING;
		menu->addChild(createMenuItem("Fullscreen", rightText, [=]() {
			APP->window->setFullScreen(!fullscreen);
		}));
#endif

		menu->addChild(createBoolPtrMenuItem("Invert zoom", "", &settings::invertZoom));
	}
};


////////////////////
// Engine
////////////////////


struct EngineButton : MenuButton {
#ifdef HAVE_LIBLO
	bool remoteServerStarted = false;
#endif

	void onAction(const ActionEvent& e) override {
		ui::Menu* menu = createMenu();
		menu->cornerFlags = BND_CORNER_TOP;
		menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));

// PatchCore: no OSC remote control; Performance meters are in the transport bar (DSP)

		// PatchCore: driver, devices, rate and buffer in one dialog (the standalone only; a plugin follows its host)
		if (isStandalone()) {
			menu->addChild(createMenuItem("Audio Settings…", "", []() {
				patchcore::showAudioSettings();
			}));
		}

		if (isUsingNativeAudio() && supportsMIDI()) {
			std::string rightText;
			if (isMIDIEnabled())
				rightText = CHECKMARK_STRING;
			menu->addChild(createMenuItem("Enable/Reconnect MIDI", rightText, []() {
				requestMIDI();
			}));
		}
	}

#ifdef HAVE_LIBLO
	void step() override {
		MenuButton::step();

		if (remoteServerStarted) {
			CardinalPluginContext* const context = static_cast<CardinalPluginContext*>(APP);
			CardinalBasePlugin* const plugin = static_cast<CardinalBasePlugin*>(context->plugin);
			plugin->stepRemoteServer();
		}
	}
#endif
};


////////////////////
// Help
////////////////////


struct HelpButton : MenuButton {
	void onAction(const ActionEvent& e) override {
		ui::Menu* menu = createMenu();
		menu->cornerFlags = BND_CORNER_TOP;
		menu->box.pos = getAbsoluteOffset(math::Vec(0, box.size.y));

		// PatchCore: interactive lessons (research_docs/15)
		menu->addChild(createSubmenuItem("Tutorials", "", [](ui::Menu* menu) {
			const std::vector<patchcore::TutorialInfo> list = patchcore::tutorialList();
			if (list.empty())
				menu->addChild(createMenuLabel("No lessons found"));
			for (const patchcore::TutorialInfo& t : list) {
				const std::string id = t.id;
				ui::MenuItem* const item = createMenuItem(t.title, t.done ? CHECKMARK_STRING : "", [id]() {
					patchcore::openTutorial(id);
				});
				item->disabled = !t.available;
				menu->addChild(item);
				menu->addChild(createMenuLabel("    " + t.description));
			}
		}));

		menu->addChild(new ui::MenuSeparator);

		menu->addChild(createMenuItem("PatchCore on GitHub", "", [=]() {
			patchUtils::openBrowser("https://github.com/JXMP-Audio/PatchCore");
		}));

		menu->addChild(new ui::MenuSeparator);

#ifndef DISTRHO_OS_WASM
		menu->addChild(createMenuItem("Open user folder", "", [=]() {
			system::openDirectory(asset::user(""));
		}));

		menu->addChild(new ui::MenuSeparator);
#endif

		menu->addChild(createMenuLabel("PatchCore by JXMP Audio · based on Cardinal / Rack " + APP_VERSION));
	}
};


////////////////////
// MenuBar
////////////////////


struct InfoLabel : ui::Label {
	int frameCount = 0;
	double frameDurationTotal = 0.0;
	double frameDurationAvg = NAN;
	// double uiLastTime = 0.0;
	// double uiLastThreadTime = 0.0;
	// double uiFrac = 0.0;

	void step() override {
		// Compute frame rate
		double frameDuration = APP->window->getLastFrameDuration();
		if (std::isfinite(frameDuration)) {
			frameDurationTotal += frameDuration;
			frameCount++;
		}
		if (frameDurationTotal >= 1.0) {
			frameDurationAvg = frameDurationTotal / frameCount;
			frameDurationTotal = 0.0;
			frameCount = 0;
		}

		// Compute UI thread CPU
		// double time = system::getTime();
		// double uiDuration = time - uiLastTime;
		// if (uiDuration >= 1.0) {
		// 	double threadTime = system::getThreadTime();
		// 	uiFrac = (threadTime - uiLastThreadTime) / uiDuration;
		// 	uiLastThreadTime = threadTime;
		// 	uiLastTime = time;
		// }

		text = "";

		if (box.size.x >= 400) {
			double fps = std::isfinite(frameDurationAvg) ? 1.0 / frameDurationAvg : 0.0;
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
			double meterAverage = APP->engine->getMeterAverage();
			double meterMax = APP->engine->getMeterMax();
			text = string::f("%.1f fps  %.1f%% avg  %.1f%% max", fps, meterAverage * 100, meterMax * 100);
#else
			text = string::f("%.1f fps", fps);
#endif
			text += "     ";
		}

		text += "Cardinal " + APP_EDITION + " " + CARDINAL_VERSION;

		Label::step();
	}
};


struct MenuBar : widget::OpaqueWidget {
	static constexpr const float kPatchCoreHeight = 46.f;

	ui::SequentialLayout* layout;
	HelpButton* helpButton;
	InfoLabel* infoLabel;
	widget::Widget* patchcoreLogo;
	widget::Widget* patchcoreCenter;
	widget::Widget* patchcoreRight;

	MenuBar(const bool isStandalone)
		: widget::OpaqueWidget()
	{
		const float margin = 5;
		box.size.y = BND_WIDGET_HEIGHT + 2 * margin;

		patchcoreLogo = patchcore::createTopBarLogo();
		addChild(patchcoreLogo);
		patchcoreCenter = patchcore::createTopBarCenter();
		addChild(patchcoreCenter);
		patchcoreRight = patchcore::createTopBarRight();
		addChild(patchcoreRight);

		layout = new ui::SequentialLayout;
		layout->margin = math::Vec(margin, margin);
		layout->spacing = math::Vec(0, 0);
		addChild(layout);

		FileButton* fileButton = new FileButton(isStandalone);
		fileButton->text = "File";
		layout->addChild(fileButton);

		EditButton* editButton = new EditButton;
		editButton->text = "Edit";
		layout->addChild(editButton);

		ViewButton* viewButton = new ViewButton;
		viewButton->text = "View";
		layout->addChild(viewButton);

#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
		EngineButton* engineButton = new EngineButton;
		engineButton->text = "Engine";
		layout->addChild(engineButton);
#endif

		helpButton = new HelpButton;
		helpButton->text = "Help";
		layout->addChild(helpButton);

		infoLabel = new InfoLabel;
		infoLabel->box.size.x = 600;
		infoLabel->alignment = ui::Label::RIGHT_ALIGNMENT;
		layout->addChild(infoLabel);
	}

	void draw(const DrawArgs& args) override {
		if (patchcore::enabled()) {
			patchcore::drawBarBackground(args.vg, box.size, false);
			Widget::draw(args);
			return;
		}

		bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_ALL);
		bndBevel(args.vg, 0.0, 0.0, box.size.x, box.size.y);

		Widget::draw(args);
	}

	void step() override {
		const bool patchcoreEnabled = patchcore::enabled();
		const float margin = 5;
		patchcoreLogo->setVisible(patchcoreEnabled);
		patchcoreCenter->setVisible(patchcoreEnabled);
		patchcoreRight->setVisible(patchcoreEnabled);
		infoLabel->setVisible(!patchcoreEnabled);

		if (patchcoreEnabled) {
			box.size.y = kPatchCoreHeight;
			patchcoreLogo->box.pos = math::Vec(12, 0);
			patchcoreLogo->box.size.y = box.size.y;
			layout->box.pos = math::Vec(patchcoreLogo->box.getRight(), (box.size.y - BND_WIDGET_HEIGHT) * 0.5f - margin);
		}
		else {
			box.size.y = BND_WIDGET_HEIGHT + 2 * margin;
			layout->box.pos = math::Vec(0, 0);
		}

		Widget::step();

		if (patchcoreEnabled) {
			// Selection status only fits on wide windows
			patchcoreRight->box.size = math::Vec(box.size.x >= 1400 ? 360 : 130, box.size.y);
			patchcoreRight->box.pos = math::Vec(box.size.x - patchcoreRight->box.size.x - 12, 0);
			patchcoreCenter->box.pos = math::Vec(layout->box.pos.x + helpButton->box.getRight() + 14, 0);
			patchcoreCenter->box.size = math::Vec(patchcoreRight->box.pos.x - patchcoreCenter->box.pos.x - 24, box.size.y);
			return;
		}

		infoLabel->box.size.x = box.size.x - infoLabel->box.pos.x - 5;
		// Setting 50% alpha prevents Label from using the default UI theme color, so set the color manually here.
		infoLabel->color = color::alpha(bndGetTheme()->regularTheme.textColor, 0.5);
	}
};


} // namespace menuBar


widget::Widget* createMenuBar() {
	menuBar::MenuBar* menuBar = new menuBar::MenuBar(isStandalone());
	return menuBar;
}


} // namespace app
} // namespace rack

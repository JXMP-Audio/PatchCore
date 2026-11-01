/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Mixer view and one-per-project modules. The core shows the view; the console module (JX-CONSOLE in
 * the JXMP plugin) builds its content.
 */

#pragma once

#include <string>

namespace rack {
namespace plugin {
struct Model;
}
namespace widget {
struct Widget;
}

namespace patchcore {

/** Implemented by the widget of a console module, next to app::ModuleWidget. The first active one in the
rack is shown by the Mixer view; double-clicking such a module opens the view. */
struct MixerProvider {
	virtual ~MixerProvider() {}
	/** Builds the content of the Mixer view. The core owns it, sets its box every frame and deletes it
	when the view closes or the module goes away. */
	virtual widget::Widget* createMixerView() = 0;
	/** False for a second console that stays inactive. */
	virtual bool mixerActive() {
		return true;
	}
};

/** Opens or closes the Mixer view (it closes by itself when no console is in the rack). Defined in
src/patchcore/MixerView.cpp. */
void showMixer(bool show);
bool mixerShown();
/** True when the rack has an active console, so the Mixer view can open. */
bool mixerAvailable();

/** Models of which only one module may be in the project. Models with the same `group` count as one
(Console 4/8/16/32). The quick search does not add a second one. */
void registerSingletonModel(plugin::Model* model, const std::string& group);
/** True when `model` is a singleton and a module of its group is already in the rack. */
bool singletonTaken(plugin::Model* model);

} // namespace patchcore
} // namespace rack

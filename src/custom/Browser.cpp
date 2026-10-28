/*
 * DISTRHO Cardinal Plugin
 * Copyright (C) 2021-2026 Filipe Coelho <falktx@falktx.com>
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

// little hack to force browser yellow color to cardinal red
#define SCHEME_YELLOW SCHEME_YELLOW_OldVCV
#include "componentlibrary.hpp"
#undef SCHEME_YELLOW
namespace rack {
namespace componentlibrary {
static const NVGcolor SCHEME_YELLOW = nvgRGBf(0.76f, 0.11f, 0.22f);
}
}

#include "../src/app/Browser.cpp"

// PatchCore docked browser access to the module search database and module creation

#include "../patchcore/PatchCore.hpp"

namespace rack {
namespace patchcore {

std::vector<plugin::Model*> searchModels(const std::string& search) {
	std::vector<plugin::Model*> models;
	for (auto& result : app::browser::modelDb.search(search))
		models.push_back(result.key);
	return models;
}

app::ModuleWidget* addModelAtMouse(plugin::Model* const model) {
	return app::browser::chooseModel(model);
}

app::ModuleWidget* addModelAt(plugin::Model* const model, const math::Vec pos) {
	// Same as chooseModel, but placed at the nearest free slot to `pos` instead of the mouse
	settings::ModuleInfo& mi = settings::moduleInfos[model->plugin->slug][model->slug];
	mi.added++;
	mi.lastAdded = system::getUnixTime();

	INFO("Creating module %s", model->getFullName().c_str());
	engine::Module* const module = model->createModule();
	APP->engine->addModule(module);

	INFO("Creating module widget %s", model->getFullName().c_str());
	app::ModuleWidget* const moduleWidget = model->createModuleWidget(module);
	APP->scene->rack->addModule(moduleWidget);
	APP->scene->rack->setModulePosNearest(moduleWidget, pos);
	moduleWidget->loadTemplate();

	history::ModuleAdd* const h = new history::ModuleAdd;
	h->setModule(moduleWidget);
	APP->history->push(h);

	return moduleWidget;
}

} // namespace patchcore
} // namespace rack

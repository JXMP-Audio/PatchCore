/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// PATCHCORE_DEV_CHECK=1: a self-check of every JXMP module before a release. Each model gets a module and its panel;
// the log lists params and ports without a widget or without a name / description, widgets for ids that do not
// exist, and controls that overlap each other or stick out of the panel (front and back checked separately).
// Then the app closes. Nothing is added to the rack.

#include <app/ModuleWidget.hpp>
#include <app/ParamWidget.hpp>
#include <app/PortWidget.hpp>
#include <context.hpp>
#include <logger.hpp>
#include <asset.hpp>
#include <plugin.hpp>
#include <system.hpp>
#include <window/Window.hpp>

#include <chrono>
#include <cstdlib>
#include <map>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "PatchCore.hpp"

namespace rack {
namespace asset {
std::string patchesPath();  // CardinalCommon.cpp
}
namespace patchcore {

namespace {

struct Item {
	std::string what;
	math::Rect box;
};

/** Widget boxes shrunk a little: touching edges are fine, real overlaps are not */
bool overlaps(const math::Rect& a, const math::Rect& b) {
	const float m = 1.f;
	return a.pos.x + m < b.pos.x + b.size.x - m && b.pos.x + m < a.pos.x + a.size.x - m && a.pos.y + m < b.pos.y + b.size.y - m &&
	       b.pos.y + m < a.pos.y + a.size.y - m;
}

/** Saving and loading: toJson of a module with moved knobs, fromJson into a fresh module, toJson again: the two
must match (anything the module forgets to load shows up here) */
int roundTrip(plugin::Model* model, engine::Module* module) {
	for (int i = 0; i < (int) module->params.size(); ++i)
		if (engine::ParamQuantity* const pq = module->paramQuantities[i])
			pq->setScaledValue(0.37f);
	json_t* const a = module->toJson();
	engine::Module* const copy = model->createModule();
	copy->id = module->id;
	copy->fromJson(a);
	json_t* const b = copy->toJson();
	char* const sa = json_dumps(a, JSON_SORT_KEYS | JSON_COMPACT);
	char* const sb = json_dumps(b, JSON_SORT_KEYS | JSON_COMPACT);
	const bool same = sa && sb && std::string(sa) == std::string(sb);
	if (!same)
		WARN("DEV CHECK %s: save/load differs:\n  saved  %s\n  loaded %s", model->slug.c_str(), sa ? sa : "", sb ? sb : "");
	free(sa);
	free(sb);
	json_decref(a);
	json_decref(b);
	delete copy;
	return same ? 0 : 1;
}


/** The patches we ship: every module exists, every cable goes to a port that exists */
int checkPatches() {
	int problems = 0;
	std::vector<std::string> files;
	for (const std::string& f : system::getEntries(asset::patchesPath()))
		if (system::getFilename(f).compare(0, 5, "jxmp-") == 0 && system::getExtension(f) == ".vcv")
			files.push_back(f);
	if (plugin::Plugin* const p = plugin::getPlugin("JXMP"))
		for (const std::string& dir : system::getEntries(system::join(p->path, "res/tutorials")))
			if (system::isFile(system::join(dir, "start.vcv")))
				files.push_back(system::join(dir, "start.vcv"));
	for (const std::string& f : files) {
		json_error_t err;
		json_t* const root = json_load_file(f.c_str(), 0, &err);
		if (!root) {
			WARN("DEV CHECK patch %s: %s", f.c_str(), err.text);
			++problems;
			continue;
		}
		std::map<int64_t, engine::Module*> mods;
		json_t* const modules = json_object_get(root, "modules");
		for (size_t i = 0; i < json_array_size(modules); ++i) {
			json_t* const m = json_array_get(modules, i);
			const char* const ps = json_string_value(json_object_get(m, "plugin"));
			const char* const ms = json_string_value(json_object_get(m, "model"));
			plugin::Model* const model = ps && ms ? plugin::getModel(ps, ms) : nullptr;
			if (model == nullptr) {
				WARN("DEV CHECK patch %s: no module %s/%s", system::getFilename(f).c_str(), ps ? ps : "?", ms ? ms : "?");
				++problems;
				continue;
			}
			mods[json_integer_value(json_object_get(m, "id"))] = model->createModule();
		}
		json_t* const cables = json_object_get(root, "cables");
		for (size_t i = 0; i < json_array_size(cables); ++i) {
			json_t* const c = json_array_get(cables, i);
			const int64_t om = json_integer_value(json_object_get(c, "outputModuleId"));
			const int64_t im = json_integer_value(json_object_get(c, "inputModuleId"));
			const int oid = (int) json_integer_value(json_object_get(c, "outputId"));
			const int iid = (int) json_integer_value(json_object_get(c, "inputId"));
			if (!mods.count(om) || !mods.count(im) || oid < 0 || oid >= (int) mods[om]->outputs.size() || iid < 0 ||
			    iid >= (int) mods[im]->inputs.size()) {
				WARN("DEV CHECK patch %s: cable %d points at a missing module or port", system::getFilename(f).c_str(), (int) i);
				++problems;
			}
		}
		INFO("DEV CHECK patch %s: %d module(s), %d cable(s)", system::getFilename(f).c_str(), (int) mods.size(),
		     (int) json_array_size(cables));
		json_decref(root);
	}
	return problems;
}


/** The DSP under random inputs and knob positions: outputs finite and below 20 V, a NaN burst at the inputs does not
stick, and the CPU time per sample. The module is not in the engine: process() is called directly. */
int fuzz(plugin::Model* model, engine::Module* module) {
	int problems = 0;
	auto report = [&](const std::string& msg) {
		WARN("DEV CHECK %s: DSP: %s", model->slug.c_str(), msg.c_str());
		++problems;
	};
	const float fs = 48000.f;
	engine::Module::SampleRateChangeEvent src;
	src.sampleRate = fs;
	src.sampleTime = 1.f / fs;
	module->onSampleRateChange(src);
	engine::Module::ProcessArgs args;
	args.sampleRate = fs;
	args.sampleTime = 1.f / fs;
	std::mt19937 rng(1234);
	std::uniform_real_distribution<float> volts(-10.f, 10.f), unit(0.f, 1.f);
	const int nIn = (int) module->inputs.size(), nOut = (int) module->outputs.size();
	std::vector<bool> badOut(nOut, false), hugeOut(nOut, false);

	auto randomize = [&](bool nan) {
		for (int i = 0; i < nIn; ++i) {
			const int ch = unit(rng) < 0.3f ? 0 : 1 + (int) (unit(rng) * 15.99f);
			module->inputs[i].setChannels(ch);
			for (int c = 0; c < ch; ++c)
				module->inputs[i].setVoltage(nan && unit(rng) < 0.5f ? NAN : volts(rng), c);
		}
	};
	const auto t0 = std::chrono::steady_clock::now();
	const int total = (int) fs * 2;
	for (int n = 0; n < total; ++n) {
		args.frame = n;
		if (n % 4800 == 0) {
			// New knob positions and new input patterns, ten times a second
			for (int i = 0; i < (int) module->params.size(); ++i) {
				engine::ParamQuantity* const pq = module->paramQuantities[i];
				if (pq != nullptr)
					pq->setScaledValue(unit(rng));
			}
			randomize(false);
		}
		else if (n % 7 == 0) {
			for (int i = 0; i < nIn; ++i)
				for (int c = 0; c < module->inputs[i].getChannels(); ++c)
					module->inputs[i].setVoltage(volts(rng), c);
		}
		module->process(args);
		for (int o = 0; o < nOut; ++o) {
			for (int c = 0; c < module->outputs[o].getChannels(); ++c) {
				const float v = module->outputs[o].getVoltage(c);
				if (!std::isfinite(v))
					badOut[o] = true;
				else if (std::fabs(v) > 20.f)
					hugeOut[o] = true;
			}
		}
	}
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	for (int o = 0; o < nOut; ++o) {
		const std::string name = module->outputInfos[o] ? module->outputInfos[o]->name : "?";
		if (badOut[o])
			report("output " + name + " gave NaN/Inf from finite inputs");
		if (hugeOut[o])
			report("output " + name + " went past ±20 V");
	}
	// CPU: one module should use well under 1 % of a core per channel set (2 s of audio here)
	const double load = seconds / 2.0 * 100.0;
	INFO("DEV CHECK %s: DSP load %.2f %% of a core (random polyphony)", model->slug.c_str(), load);
	if (load > 5.0)
		report(string::f("heavy: %.1f %% of a core", load));

	// A NaN burst, then finite inputs again: the outputs must be finite after a second
	for (int n = 0; n < 480; ++n) {
		randomize(true);
		module->process(args);
	}
	randomize(false);
	bool stuck = false;
	for (int n = 0; n < (int) fs; ++n) {
		module->process(args);
		if (n > (int) fs / 2)
			for (int o = 0; o < nOut; ++o)
				for (int c = 0; c < module->outputs[o].getChannels(); ++c)
					stuck = stuck || !std::isfinite(module->outputs[o].getVoltage(c));
	}
	if (stuck)
		report("a NaN at the inputs stays in the module (outputs still NaN a second later)");
	return problems;
}


int checkModel(plugin::Model* model) {
	int problems = 0;
	auto report = [&](const std::string& msg) {
		WARN("DEV CHECK %s: %s", model->slug.c_str(), msg.c_str());
		++problems;
	};
	engine::Module* const module = model->createModule();
	app::ModuleWidget* const mw = model->createModuleWidget(module);
	const math::Rect panel(math::Vec(), mw->box.size);

	// Every param and port has a widget, a name and a description
	for (int i = 0; i < (int) module->params.size(); ++i) {
		engine::ParamQuantity* const pq = module->paramQuantities[i];
		// The consoles keep most controls in the Mixer view, not on the panel
		if (mw->getParam(i) == nullptr && model->slug.compare(0, 7, "Console") != 0)
			report(string::f("param %d (%s) has no widget", i, pq ? pq->name.c_str() : "?"));
		if (pq == nullptr || pq->name.empty())
			report(string::f("param %d has no name", i));
	}
	for (int out = 0; out < 2; ++out) {
		const int n = (int) (out ? module->outputs.size() : module->inputs.size());
		for (int i = 0; i < n; ++i) {
			engine::PortInfo* const info = out ? module->outputInfos[i] : module->inputInfos[i];
			const char* const kind = out ? "output" : "input";
			if ((out ? mw->getOutput(i) : mw->getInput(i)) == nullptr)
				report(string::f("%s %d (%s) has no widget", kind, i, info ? info->name.c_str() : "?"));
			if (info == nullptr || info->name.empty())
				report(string::f("%s %d has no name", kind, i));
			else if (info->description.empty())
				report(string::f("%s %d (%s) has no description (tooltip)", kind, i, info->name.c_str()));
		}
	}

	// Widgets for ids that exist; then overlaps and the panel edge, per side
	std::vector<Item> front, back;
	for (app::ParamWidget* pw : mw->getParams()) {
		if (pw->paramId < 0 || pw->paramId >= (int) module->params.size()) {
			report(string::f("a param widget points at id %d", pw->paramId));
			continue;
		}
		front.push_back({"param " + module->paramQuantities[pw->paramId]->name, pw->box});
	}
	for (int out = 0; out < 2; ++out) {
		for (app::PortWidget* p : out ? mw->getOutputs() : mw->getInputs()) {
			const int n = (int) (out ? module->outputs.size() : module->inputs.size());
			if (p->portId < 0 || p->portId >= n) {
				report(string::f("a port widget points at id %d", p->portId));
				continue;
			}
			engine::PortInfo* const info = out ? module->outputInfos[p->portId] : module->inputInfos[p->portId];
			const std::string what = std::string(out ? "output " : "input ") + (info ? info->name : "?");
			(portSide(p) == SIDE_BACK ? back : front).push_back({what, p->box});
		}
	}
	for (std::vector<Item>* side : {&front, &back}) {
		const char* const sideName = side == &front ? "front" : "back";
		for (size_t a = 0; a < side->size(); ++a) {
			const Item& ia = (*side)[a];
			if (!panel.contains(ia.box.pos) || !panel.contains(ia.box.getBottomRight().minus(math::Vec(1.f, 1.f))))
				report(string::f("%s: %s sticks out of the panel", sideName, ia.what.c_str()));
			for (size_t b = a + 1; b < side->size(); ++b) {
				// A Patchbay's "smart jack": the input and the output of a channel share one place on purpose
				const std::string& wa = ia.what;
				const std::string& wb = (*side)[b].what;
				const bool pair = wa.compare(0, 6, "input ") == 0 && wb.compare(0, 7, "output ") == 0 && wa.substr(6) == wb.substr(7);
				if (!pair && overlaps(ia.box, (*side)[b].box))
					report(string::f("%s: %s overlaps %s", sideName, ia.what.c_str(), (*side)[b].what.c_str()));
			}
		}
	}
	problems += roundTrip(model, module);
	problems += fuzz(model, module);
	// The module and its widget are left to the closing app (they were never added to the engine or the rack)
	INFO("DEV CHECK %s: %d problem(s)", model->slug.c_str(), problems);
	return problems;
}

} // namespace


void devCheck() {
	plugin::Plugin* const p = plugin::getPlugin("JXMP");
	if (p == nullptr) {
		WARN("DEV CHECK: no JXMP plugin");
		return;
	}
	int total = checkPatches();
	for (plugin::Model* model : p->models)
		total += checkModel(model);
	INFO("DEV CHECK DONE: %d model(s), %d problem(s)", (int) p->models.size(), total);
	APP->window->close();
}

} // namespace patchcore
} // namespace rack

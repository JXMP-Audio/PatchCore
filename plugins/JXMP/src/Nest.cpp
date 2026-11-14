/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-NEST: one VST3 / CLAP / LV2 plugin inside the rack, through Carla's host library (as Cardinal's Ildaeil).
 * Each module owns a Carla "patchbay 32" engine: engine audio input k feeds the plugin's input k, its output k goes to
 * engine output k, and all jack <-> port mapping (the roles of the 12 + 12 universal jacks) happens here, filling and
 * reading the engine's buffers in blocks of 64 frames. 8 CV slots move plugin parameters; notes come from the DAW /
 * PatchCore MIDI and from GATE / V/OCT jacks. Plugins are found only in the standard folders and the user's own
 * folders, scanned by carla-discovery in a separate process (shipped in JXMP/res/carla).
 */

#include "JXMPWidget.hpp"
#include "JXMPMenuButtons.hpp"
#include "plugincontext.hpp"
#include "dsp/NestIO.hpp"
#include "dsp/NestCv.hpp"
#include "dsp/NestNotes.hpp"
#include "dsp/NestPlugins.hpp"
#include "dsp/NestMenu.hpp"
#include "dsp/NestBypass.hpp"

#include "CarlaNativePlugin.h"
#include "CarlaBackendUtils.hpp"
#include "CarlaEngine.hpp"
#include "CarlaPlugin.hpp"
#include "CarlaStateUtils.hpp"
#include "CarlaLv2Utils.hpp"
#include "water/streams/MemoryOutputStream.h"
#include "water/xml/XmlDocument.h"
#include "water/xml/XmlElement.h"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <chrono>
#include <map>

namespace rack {
std::string homeDir();
}

using namespace CARLA_BACKEND_NAMESPACE;

namespace {

namespace nest = jxmp::nest;
using jxmp::IconButton;
using jxmp::MenuFrame;
const NVGcolor kColour = nvgRGB(0x3f, 0xc1, 0xb0);
constexpr int kBlock = 64;
static_assert(kBlock == jxmp::nest::kBlockFrames, "BYPASS delays the dry input by one block");
constexpr int kPorts = nest::kMaxPorts;  // the engine is "patchbay 32"
constexpr int kMidiMax = 512;
constexpr float kFullScale = 5.f;        // ±5 V = 1.0

#ifdef ARCH_WIN
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

PluginType pluginType(nest::Format f) {
	switch (f) {
		case nest::F_VST2: return PLUGIN_VST2;
		case nest::F_VST3: return PLUGIN_VST3;
		case nest::F_CLAP: return PLUGIN_CLAP;
		default: return PLUGIN_LV2;
	}
}

std::string discoveryTool() {
	const std::string p = asset::plugin(pluginInstance, kWindows ? "res/carla/carla-discovery-native.exe" : "res/carla/carla-discovery-native");
	return system::exists(p) ? p : std::string();
}

std::string envOr(const char* name) {
	const char* v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

std::vector<std::string> readLines(const std::string& path) {
	std::vector<std::string> out;
	std::ifstream f(path);
	std::string line;
	while (std::getline(f, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
			line.pop_back();
		if (!line.empty())
			out.push_back(line);
	}
	return out;
}

void writeLines(const std::string& path, const std::vector<std::string>& lines) {
	std::ofstream f(path, std::ios::trunc);
	for (const std::string& l : lines)
		f << l << "\n";
}

// ---------------------------------------------------------------------------------------------------------------
// The plugin library: one for the whole program, shared by every JX-NEST. Lives on the UI thread.

struct Library {
	std::vector<nest::PluginEntry> entries;
	std::vector<std::string> folders;  // the user's own
	bool loaded = false, scanning = false;
	std::vector<nest::PluginEntry> found;
	std::vector<std::pair<nest::Format, std::string>> queue;  // formats still to scan, their folders joined
	CarlaPluginDiscoveryHandle handle = nullptr;
	nest::Format scanFormat = nest::F_VST3;
	std::string scanPaths;  // the folders being scanned now
	std::string error;
	int scans = 0;             // finished scans: modules wait for the one that includes their dropped plugin
	std::string extraNext;     // a folder dropped during a scan: scanned right after it

	static Library& get() {
		static Library lib;
		return lib;
	}
	static std::string cacheFile() { return asset::user("JXMP-nest-plugins.txt"); }
	static std::string foldersFile() { return asset::user("JXMP-nest-folders.txt"); }

	void ensureLoaded() {
		if (loaded)
			return;
		loaded = true;
		folders = readLines(foldersFile());
		for (const std::string& l : readLines(cacheFile())) {
			nest::PluginEntry e;
			if (nest::parseCacheLine(l, e))
				entries.push_back(e);
		}
		if (!system::exists(cacheFile()))
			rescan();
	}

	void save() {
		std::vector<std::string> lines;
		for (const nest::PluginEntry& e : entries)
			lines.push_back(nest::cacheLine(e));
		writeLines(cacheFile(), lines);
		writeLines(foldersFile(), folders);
	}

	std::vector<nest::Folder> searchFolders() const {
		return nest::withUserFolders(nest::standardFolders(kWindows, kWindows ? std::string() : rack::homeDir(), envOr("COMMONPROGRAMFILES"),
		                                                   envOr("LOCALAPPDATA")),
		                             folders, kWindows);
	}

	/** The existing folders of format `fm` in `f`, joined as Carla takes them (':', ';' on Windows) */
	static std::string joinExisting(const std::vector<nest::Folder>& f, nest::Format fm) {
		std::string joined;
		for (const nest::Folder& x : f)
			if (x.format == fm && system::isDirectory(x.path))
				joined += (joined.empty() ? "" : (kWindows ? ";" : ":")) + x.path;
		return joined;
	}
	std::string pathsFor(nest::Format fm) const { return joinExisting(searchFolders(), fm); }

	/** Scans every format in its folders; `extra` (a dropped plugin's folder) is searched too. Returns the scan count
	 * after which `extra` has been searched (a scan running now finishes first), or -1 when nothing can scan. */
	int rescan(const std::string& extra = std::string()) {
		if (scanning) {
			if (extra.empty())
				return scans + 1;
			extraNext = extra;
			return scans + 2;
		}
		error.clear();
		if (discoveryTool().empty()) {
			error = "SCANNER MISSING";
			return -1;
		}
		std::vector<nest::Folder> f = searchFolders();
		if (!extra.empty())
			for (nest::Format fm : {nest::F_VST2, nest::F_VST3, nest::F_CLAP, nest::F_LV2})
				f.push_back(nest::Folder(fm, extra));
		queue.clear();
		for (nest::Format fm : {nest::F_VST3, nest::F_CLAP, nest::F_LV2, nest::F_VST2}) {
			const std::string joined = joinExisting(f, fm);
			if (!joined.empty())
				queue.push_back(std::make_pair(fm, joined));
		}
		found.clear();
		scanning = true;
		INFO("JX-NEST: scanning plugins with %s", discoveryTool().c_str());
		return scans + 1;
	}

	static void onFound(void* ptr, const CarlaPluginDiscoveryInfo* info, const char*) {
		if (!info)
			return;
		Library* const lib = static_cast<Library*>(ptr);
		// Only what JX-NEST can host: audio + at most one MIDI in, no CV ports
		if (info->io.cvIns || info->io.cvOuts || info->io.midiIns > 1)
			return;
		if (info->filename && nest::isOwnBundle(info->filename))
			return;
		nest::PluginEntry e;
		e.format = lib->scanFormat;
		e.label = info->label ? info->label : "";
		e.id = lib->scanFormat == nest::F_VST2 ? std::to_string(info->uniqueId) : e.label;
		e.name = info->metadata.name ? info->metadata.name : "";
		e.maker = info->metadata.maker ? info->metadata.maker : "";
		e.file = info->filename ? info->filename : "";
		if (e.format == nest::F_LV2)  // only the bundle's name: its full path, for drops and "is it still there"
			e.file = nest::resolveBundle(e.file, lib->scanPaths, kWindows, [](const std::string& x) { return system::exists(x); });
		e.instrument = (info->metadata.hints & PLUGIN_IS_SYNTH) != 0 || info->metadata.category == PLUGIN_CATEGORY_SYNTH;
		e.ins = (int) info->io.audioIns;
		e.outs = (int) info->io.audioOuts;
		e.midiIn = info->io.midiIns > 0;
		if (!e.id.empty() && !e.file.empty() && !e.name.empty())
			lib->found.push_back(e);
	}
	static bool checkCache(void*, const char*, const char*) { return false; }

	/** Called from every JX-NEST widget's step; runs one scanning step */
	void idle() {
		if (!scanning)
			return;
		if (handle) {
			if (carla_plugin_discovery_idle(handle))
				return;
			carla_plugin_discovery_stop(handle);
			handle = nullptr;
		}
		while (!queue.empty() && !handle) {
			scanFormat = queue.front().first;
			const std::string paths = queue.front().second;
			scanPaths = paths;
			queue.erase(queue.begin());
			handle = carla_plugin_discovery_start(discoveryTool().c_str(), BINARY_NATIVE, pluginType(scanFormat), paths.c_str(), onFound,
			                                      checkCache, this);
		}
		if (handle)
			return;
		// Done: what was found, and dropped plugins elsewhere that still exist
		std::vector<nest::PluginEntry> next = nest::uniqueEntries(found);
		for (const nest::PluginEntry& old : entries)
			if (nest::findById(next, old.format, old.id) < 0 && system::exists(old.file))
				next.push_back(old);
		entries = next;
		scanning = false;
		save();
		INFO("JX-NEST: %d plugins", (int) entries.size());
		scans++;
		if (!extraNext.empty()) {
			const std::string extra = extraNext;
			extraNext.clear();
			rescan(extra);
		}
	}

	void addFolder(const std::string& path) {
		const std::string key = nest::folderKey(path, kWindows);
		for (const std::string& f : folders)
			if (nest::folderKey(f, kWindows) == key)
				return;
		folders.push_back(nest::folderKey(path, false));
		save();
		rescan();
	}
};

// ---------------------------------------------------------------------------------------------------------------
// What the audio thread needs, posted from the UI thread as one plain block

struct Routing {
	int inAudio[nest::kJacks];   // engine channel fed by the jack, -1 none
	int outAudio[nest::kJacks];  // engine channel sent to the jack, -1 none
	int inCv[nest::kJacks];      // CV slot the jack drives, -1 none
	int bypassSrc[nest::kJacks]; // BYPASS: the input jack with the same audio port, -1 none
	nest::IOTable notes;         // kinds and notes only (names empty: copying allocates nothing)
	bool midiIn = false;
	int channel = 0;
	int slotParam[nest::kSlots];
	float slotBase[nest::kSlots], slotLo[nest::kSlots], slotHi[nest::kSlots], pMin[nest::kSlots], pMax[nest::kSlots];
	// For setting parameters from the audio thread. Held: Carla deletes a removed plugin (on the UI thread) only once
	// nothing else holds it, so the audio thread never touches a freed one, even when it takes the routing late.
	CarlaPluginPtr plugin;
	bool lv2 = false;
	bool active = false;
	Routing() {
		for (int k = 0; k < nest::kJacks; ++k)
			inAudio[k] = outAudio[k] = inCv[k] = bypassSrc[k] = -1;
		for (int k = 0; k < nest::kSlots; ++k) {
			slotParam[k] = -1;
			slotBase[k] = slotLo[k] = pMin[k] = 0.f;
			slotHi[k] = pMax[k] = 1.f;
		}
		for (int k = 0; k < nest::kJacks; ++k)
			notes.in[k] = notes.out[k] = nest::Role();
	}
};

struct ParamInfo {
	std::string name;
	float min = 0.f, max = 1.f;
};

struct Nest : engine::Module {
	enum ParamIds {
		DEPTH_PARAM,
		UP_PARAM = DEPTH_PARAM + nest::kSlots, DOWN_PARAM, MENU_PARAM, OK_PARAM, OPEN_UI_PARAM, BYPASS_PARAM,
		NUM_PARAMS
	};
	enum InputIds { IN_INPUT, NUM_INPUTS = IN_INPUT + nest::kJacks };
	enum OutputIds { OUT_OUTPUT, NUM_OUTPUTS = OUT_OUTPUT + nest::kJacks };
	enum LightIds { BYPASS_LIGHT, NUM_LIGHTS };

	CardinalPluginContext* const pcontext;

	// Carla
	const NativePluginDescriptor* desc = nullptr;
	NativePluginHandle handle = nullptr;
	NativeHostDescriptor hostDesc = {};
	CarlaHostHandle host = nullptr;
	NativeTimeInfo timeInfo = {};

	// UI thread
	nest::PluginEntry plugin;
	bool hasPlugin = false, uiShown = false;
	std::string missingName;   // the patch's plugin is not on this computer
	json_t* missingJ = nullptr;  // its saved data, written back unchanged
	nest::PortInfo ports;
	nest::IOTable io;
	nest::CvSlot slots[nest::kSlots];
	std::vector<ParamInfo> params_;
	int midiChannel = 0;
	int learnSlot = -1;
	nest::Menu menu, slotMenu{nest::Menu::SLOT};

	// Patchbay bookkeeping (engine callbacks during a refresh)
	struct Port {
		uint group, id, hints;
		std::string name;
	};
	std::vector<Port> cbPorts;
	std::map<uint, int> cbGroupPlugin;  // group -> plugin id (-1 = engine's own)
	std::mutex paramMutex;
	std::vector<std::pair<int, float>> paramChanges;  // from the plugin's window, for the UI thread

	// Audio thread
	Routing pending, rt;
	std::mutex routeMutex;
	std::atomic<bool> routeDirty{false};
	float inBuf[kPorts][kBlock] = {}, outBuf[kPorts][kBlock] = {};
	int fill = 0;
	float cvNow[nest::kSlots] = {};
	bool cvPatched[nest::kSlots] = {};
	float rtLast[nest::kSlots];
	nest::NoteMaker noteMaker;
	std::vector<nest::NoteEvent> releaseEv;  // note ends, sent first in the next block
	nest::BypassFade bypassFade;
	bool pluginPlayed = false;  // outBuf holds the plugin's real output (it ran last block)
	nest::DryDelay dryDelay;
	std::vector<nest::NoteEvent> notes;
	struct HostEv {
		uint64_t at;
		uint8_t size, data[4];
	};
	HostEv hostEv[kMidiMax];
	int hostEvCount = 0;
	NativeMidiEvent midiOut[kMidiMax];
	uint32_t lastCounter = 0;
	uint64_t absFrame = 0;
	float peakIn = 0.f, peakOut = 0.f;
	std::atomic<float> meterIn{0.f}, meterOut{0.f};
	dsp::ClockDivider lightDivider;

	Nest() : pcontext(static_cast<CardinalPluginContext*>(APP)) {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int k = 0; k < nest::kSlots; ++k)
			configParam(DEPTH_PARAM + k, -1.f, 1.f, 0.f, string::f("CV slot %d depth", k + 1), "%", 0.f, 100.f);
		configButton(UP_PARAM, "Up");
		configButton(DOWN_PARAM, "Down");
		configButton(MENU_PARAM, "Menu");
		configButton(OK_PARAM, "OK");
		configButton(OPEN_UI_PARAM, "Open the plugin's window");
		configSwitch(BYPASS_PARAM, 0.f, 1.f, 0.f, "Bypass", {"Off", "On"});
		for (int k = 0; k < nest::kJacks; ++k) {
			configInput(IN_INPUT + k, string::f("In %d", k + 1))->description = "Its role: MENU > I/O > TABLE.";
			configOutput(OUT_OUTPUT + k, string::f("Out %d", k + 1))->description = "Its role: MENU > I/O > TABLE.";
		}
		for (int k = 0; k < nest::kSlots; ++k)
			rtLast[k] = -1.f;
		notes.reserve(1024);
		releaseEv.reserve(64);
		lightDivider.setDivision(512);
		startHost();
	}

	~Nest() override {
		closeUi();
		rt.plugin.reset();
		pending.plugin.reset();
		if (missingJ)
			json_decref(missingJ);
		if (handle)
			desc->deactivate(handle);
		if (host)
			carla_host_handle_free(host);
		if (handle)
			desc->cleanup(handle);
	}

	// ---- Carla host ------------------------------------------------------------------------------------------

	static uint32_t hGetBufferSize(NativeHostHandle) { return kBlock; }
	static double hGetSampleRate(NativeHostHandle h) {
		const Nest* m = static_cast<Nest*>(h);
		return m->pcontext ? m->pcontext->sampleRate : 48000.0;
	}
	static bool hIsOffline(NativeHostHandle) { return false; }
	static const NativeTimeInfo* hGetTimeInfo(NativeHostHandle h) { return &static_cast<Nest*>(h)->timeInfo; }
	static bool hWriteMidi(NativeHostHandle, const NativeMidiEvent*) { return false; }
	static void hUiParam(NativeHostHandle, uint32_t, float) {}
	static void hUiProgram(NativeHostHandle, uint8_t, uint32_t, uint32_t) {}
	static void hUiData(NativeHostHandle, const char*, const char*) {}
	static void hUiClosed(NativeHostHandle h) { static_cast<Nest*>(h)->uiShown = false; }
	static const char* hOpenFile(NativeHostHandle, bool, const char*, const char*) { return nullptr; }
	static const char* hSaveFile(NativeHostHandle, bool, const char*, const char*) { return nullptr; }
	static intptr_t hDispatcher(NativeHostHandle, NativeHostDispatcherOpcode, int32_t, intptr_t, void*, float) { return 0; }

	static void engineCallback(void* ptr, EngineCallbackOpcode action, uint pluginId, int v1, int v2, int, float vf, const char* str) {
		Nest* const m = static_cast<Nest*>(ptr);
		switch (action) {
			case ENGINE_CALLBACK_PATCHBAY_CLIENT_ADDED:
				m->cbGroupPlugin[pluginId] = v2;
				break;
			case ENGINE_CALLBACK_PATCHBAY_PORT_ADDED: {
				Port p;
				p.group = pluginId;
				p.id = (uint) v1;
				p.hints = (uint) v2;
				p.name = str ? str : "";
				m->cbPorts.push_back(p);
				break;
			}
			case ENGINE_CALLBACK_PARAMETER_VALUE_CHANGED:
				if (pluginId == 0 && v1 >= 0) {
					std::lock_guard<std::mutex> lock(m->paramMutex);
					if (m->paramChanges.size() < 4096)
						m->paramChanges.push_back(std::make_pair(v1, vf));
				}
				break;
			case ENGINE_CALLBACK_UI_STATE_CHANGED:
				if (pluginId == 0 && v1 != 1)
					m->uiShown = false;
				break;
			default:
				break;
		}
	}

	void startHost() {
		desc = carla_get_native_patchbay32_plugin();
		if (!desc)
			return;
		hostDesc.handle = this;
		hostDesc.resourceDir = carla_get_library_folder();
		hostDesc.uiName = "JX-NEST";
		hostDesc.uiParentId = 0;
		hostDesc.get_buffer_size = hGetBufferSize;
		hostDesc.get_sample_rate = hGetSampleRate;
		hostDesc.is_offline = hIsOffline;
		hostDesc.get_time_info = hGetTimeInfo;
		hostDesc.write_midi_event = hWriteMidi;
		hostDesc.ui_parameter_changed = hUiParam;
		hostDesc.ui_midi_program_changed = hUiProgram;
		hostDesc.ui_custom_data_changed = hUiData;
		hostDesc.ui_closed = hUiClosed;
		hostDesc.ui_open_file = hOpenFile;
		hostDesc.ui_save_file = hSaveFile;
		hostDesc.dispatcher = hDispatcher;
		handle = desc->instantiate(&hostDesc);
		if (!handle)
			return;
		host = carla_create_native_plugin_host_handle(desc, handle);
		if (!host)
			return;
		carla_set_engine_callback(host, engineCallback, this);
		carla_set_engine_option(host, ENGINE_OPTION_PREFER_PLUGIN_BRIDGES, 0, nullptr);
		carla_set_engine_option(host, ENGINE_OPTION_PREFER_UI_BRIDGES, 0, nullptr);
		desc->activate(handle);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		if (!handle)
			return;
		desc->deactivate(handle);
		desc->dispatcher(handle, NATIVE_PLUGIN_OPCODE_SAMPLE_RATE_CHANGED, 0, 0, nullptr, e.sampleRate);
		desc->activate(handle);
	}

	CarlaPluginPtr carlaPluginPtr() {
		if (!host)
			return CarlaPluginPtr();
		CarlaEngine* const engine = carla_get_engine_from_handle(host);
		if (!engine || engine->getCurrentPluginCount() == 0)
			return CarlaPluginPtr();
		return engine->getPlugin(0);
	}
	CarlaPlugin* carlaPlugin() { return carlaPluginPtr().get(); }

	// ---- routing (UI thread -> audio thread) ------------------------------------------------------------------

	void post(bool withPlugin = true) {
		Routing r;
		r.active = hasPlugin;
		r.midiIn = ports.midiIn;
		r.channel = midiChannel;
		for (int k = 0; k < nest::kJacks; ++k) {
			const nest::Role& a = io.in[k];
			const bool ok = hasPlugin && nest::roleValid(a, true, ports);
			if (ok && a.kind == nest::R_AUDIO)
				r.inAudio[k] = a.index;
			if (ok && a.kind == nest::R_CV)
				r.inCv[k] = a.index;
			if (ok && nest::takesNotes(a.kind)) {
				r.notes.in[k].kind = a.kind;
				r.notes.in[k].note = a.note;
			}
			const nest::Role& b = io.out[k];
			if (hasPlugin && b.kind == nest::R_AUDIO && nest::roleValid(b, false, ports))
				r.outAudio[k] = b.index;
		}
		// BYPASS: an output plays the input feeding the same port number (L to L, R to R)
		for (int o = 0; o < nest::kJacks; ++o) {
			if (io.out[o].kind != nest::R_AUDIO)
				continue;
			for (int i = 0; i < nest::kJacks; ++i)
				if (io.in[i].kind == nest::R_AUDIO && io.in[i].index == io.out[o].index) {
					r.bypassSrc[o] = i;
					break;
				}
		}
		for (int s = 0; s < nest::kSlots; ++s) {
			const nest::CvSlot& c = slots[s];
			if (!hasPlugin || c.param < 0 || c.param >= (int) params_.size())
				continue;
			r.slotParam[s] = c.param;
			r.slotBase[s] = c.base;
			r.slotLo[s] = c.rangeMin;
			r.slotHi[s] = c.rangeMax;
			r.pMin[s] = params_[c.param].min;
			r.pMax[s] = params_[c.param].max;
		}
		if (withPlugin && hasPlugin)
			r.plugin = carlaPluginPtr();
		r.lv2 = plugin.format == nest::F_LV2;
		{
			std::lock_guard<std::mutex> lock(routeMutex);
			pending = r;
			routeDirty = true;
		}
	}

	void takeRouting() {
		if (!routeDirty.load(std::memory_order_acquire))
			return;
		std::unique_lock<std::mutex> lock(routeMutex, std::try_to_lock);
		if (!lock.owns_lock())
			return;
		for (int s = 0; s < nest::kSlots; ++s)
			if (pending.slotParam[s] != rt.slotParam[s] || pending.slotBase[s] != rt.slotBase[s] || pending.plugin != rt.plugin)
				rtLast[s] = -1.f;
		// Notes routed differently (roles, channel, plugin): held notes end, none hangs
		bool notesMoved = pending.channel != rt.channel || pending.plugin != rt.plugin || pending.midiIn != rt.midiIn;
		for (int k = 0; k < nest::kJacks; ++k)
			notesMoved = notesMoved || pending.notes.in[k].kind != rt.notes.in[k].kind || pending.notes.in[k].note != rt.notes.in[k].note;
		if (notesMoved)
			noteMaker.releaseAll(0, releaseEv);
		rt = pending;
		noteMaker.channel = rt.channel;
		routeDirty = false;
	}

	// ---- plugin load / remove ---------------------------------------------------------------------------------

	void closeUi() {
		if (host && hasPlugin && uiShown)
			carla_show_custom_ui(host, 0, false);
		uiShown = false;
	}

	/** Takes the plugin out (the audio thread lets go of it first) */
	void dropPlugin() {
		closeUi();
		hasPlugin = false;
		post(false);
		if (host && carlaPlugin())
			carla_remove_all_plugins(host);
		ports = nest::PortInfo();
		params_.clear();
		learnSlot = -1;
	}

	/** Connects engine in k -> plugin in k, plugin out k -> engine out k, MIDI in -> plugin, and reads the ports */
	void wire() {
		cbPorts.clear();
		cbGroupPlugin.clear();
		carla_patchbay_refresh(host, false);
		int pluginGroup = -1;
		for (auto& g : cbGroupPlugin)
			if (g.second == 0)
				pluginGroup = (int) g.first;
		auto collect = [&](bool own, uint wantHints, uint mask) {
			std::vector<Port> v;
			for (const Port& p : cbPorts) {
				const auto g = cbGroupPlugin.find(p.group);
				const bool isOwn = g == cbGroupPlugin.end() || g->second < 0;
				if (isOwn != own || (!own && (int) p.group != pluginGroup))
					continue;
				if ((p.hints & mask) == wantHints)
					v.push_back(p);
			}
			std::sort(v.begin(), v.end(), [](const Port& a, const Port& b) { return a.group != b.group ? a.group < b.group : a.id < b.id; });
			return v;
		};
		const uint mask = PATCHBAY_PORT_IS_INPUT | PATCHBAY_PORT_TYPE_AUDIO | PATCHBAY_PORT_TYPE_MIDI;
		const std::vector<Port> plugIn = collect(false, PATCHBAY_PORT_IS_INPUT | PATCHBAY_PORT_TYPE_AUDIO, mask);
		const std::vector<Port> plugOut = collect(false, PATCHBAY_PORT_TYPE_AUDIO, mask);
		const std::vector<Port> plugMidi = collect(false, PATCHBAY_PORT_IS_INPUT | PATCHBAY_PORT_TYPE_MIDI, mask);
		const std::vector<Port> sysIn = collect(true, PATCHBAY_PORT_TYPE_AUDIO, mask);                            // engine inputs: sources
		const std::vector<Port> sysOut = collect(true, PATCHBAY_PORT_IS_INPUT | PATCHBAY_PORT_TYPE_AUDIO, mask);  // engine outputs: sinks
		const std::vector<Port> sysMidi = collect(true, PATCHBAY_PORT_TYPE_MIDI, mask);
		ports = nest::PortInfo();
		for (size_t k = 0; k < plugIn.size() && k < sysIn.size(); ++k) {
			carla_patchbay_connect(host, false, sysIn[k].group, sysIn[k].id, plugIn[k].group, plugIn[k].id);
			ports.ins.push_back(plugIn[k].name);
		}
		for (size_t k = 0; k < plugOut.size() && k < sysOut.size(); ++k) {
			carla_patchbay_connect(host, false, plugOut[k].group, plugOut[k].id, sysOut[k].group, sysOut[k].id);
			ports.outs.push_back(plugOut[k].name);
		}
		// Carla gives every plugin an events input; it takes notes only if discovery says it has a MIDI input
		if (!plugMidi.empty() && !sysMidi.empty() && plugin.midiIn) {
			carla_patchbay_connect(host, false, sysMidi[0].group, sysMidi[0].id, plugMidi[0].group, plugMidi[0].id);
			ports.midiIn = true;
		}
		INFO("JX-NEST: %s: %d in, %d out, MIDI %s", plugin.name.c_str(), (int) ports.ins.size(), (int) ports.outs.size(), ports.midiIn ? "yes" : "no");
	}

	void readParams() {
		params_.clear();
		const uint32_t n = carla_get_parameter_count(host, 0);
		for (uint32_t k = 0; k < n; ++k) {
			ParamInfo p;
			const ParameterData* d = carla_get_parameter_data(host, 0, k);
			const CarlaParameterInfo* info = carla_get_parameter_info(host, 0, k);
			const ParameterRanges* r = carla_get_parameter_ranges(host, 0, k);
			if (info && info->name)
				p.name = info->name;
			if (r) {
				p.min = r->min;
				p.max = r->max;
			}
			// Only parameters one can move: inputs, enabled, not read-only
			if (d && (d->type != PARAMETER_INPUT || (d->hints & PARAMETER_IS_ENABLED) == 0 || (d->hints & PARAMETER_IS_READ_ONLY) != 0))
				p.name.clear();
			params_.push_back(p);
		}
	}

	float normalised(int param, float value) const {
		if (param < 0 || param >= (int) params_.size())
			return 0.f;
		const ParamInfo& p = params_[param];
		return p.max > p.min ? math::clamp((value - p.min) / (p.max - p.min), 0.f, 1.f) : 0.f;
	}

	/** LV2 plugins come from lilv's world, one for the whole program, filled once from the LV2 folders. It gets our
	 * folders; a bundle in a folder added later is read into it on its own. */
	void prepareLv2(const nest::PluginEntry& e) {
		const std::string paths = Library::get().pathsFor(nest::F_LV2);
		carla_set_engine_option(host, ENGINE_OPTION_PLUGIN_PATH, PLUGIN_LV2, paths.c_str());
		Lv2WorldClass& world = Lv2WorldClass::getInstance();
		world.initIfNeeded(paths.c_str());
		if (world.getPluginFromURI(e.label.c_str()) || !system::isDirectory(e.file))
			return;
		std::string bundle = e.file;
		if (bundle.back() != '/' && bundle.back() != '\\')
			bundle += kWindows ? "\\" : "/";
		LilvNode* const uri = lilv_new_file_uri(world.me, nullptr, bundle.c_str());
		if (!uri)
			return;
		lilv_world_load_bundle(world.me, uri);
		lilv_node_free(uri);
		INFO("JX-NEST: LV2 bundle %s read", e.file.c_str());
	}

	/** Loads `e`; keeps the roles (renamed to the new ports) and the slots that fit */
	bool loadPlugin(const nest::PluginEntry& e, const std::string& state = std::string()) {
		if (!host)
			return false;
		dropPlugin();
		plugin = e;
		missingName.clear();
		if (missingJ) {
			json_decref(missingJ);
			missingJ = nullptr;
		}
		const int64_t uid = e.format == nest::F_VST2 ? std::atoll(e.id.c_str()) : 0;
		if (e.format == nest::F_LV2)
			prepareLv2(e);
		if (!carla_add_plugin(host, BINARY_NATIVE, pluginType(e.format), e.file.c_str(), e.name.c_str(), e.label.c_str(), uid, nullptr,
		                      PLUGIN_OPTIONS_NULL)) {
			WARN("JX-NEST: %s did not load: %s", e.name.c_str(), carla_get_last_error(host));
			return false;
		}
		if (!state.empty())
			loadState(state);
		hasPlugin = true;
		wire();
		readParams();
		nest::renamePorts(io, ports);
		for (nest::CvSlot& s : slots) {
			if (s.param >= (int) params_.size() || (s.param >= 0 && params_[s.param].name != s.paramName))
				s = nest::CvSlot();
			s.last = -1.f;
		}
		post();
		return true;
	}

	std::string saveState() {
		CarlaPlugin* const p = carlaPlugin();
		if (!p)
			return std::string();
		water::MemoryOutputStream out, inner;
		p->getStateSave(true).dumpToMemoryStream(inner);
		out << "<?xml version='1.0' encoding='UTF-8'?>\n<!DOCTYPE CARLA-PRESET>\n<CARLA-PRESET VERSION='2.0'>\n";
		out << inner;
		out << "</CARLA-PRESET>\n";
		return std::string((const char*) out.getData(), out.getDataSize());
	}

	void loadState(const std::string& xml) {
		CarlaPlugin* const p = carlaPlugin();
		if (!p)
			return;
		water::XmlDocument doc{water::String(xml.c_str())};
		std::unique_ptr<water::XmlElement> el(doc.getDocumentElement(false));
		if (!el)
			return;
		CarlaStateSave ss;
		if (ss.fillFromXmlElement(el.get()))
			p->loadStateSave(ss);
	}

	// ---- audio ------------------------------------------------------------------------------------------------

	void process(const ProcessArgs& args) override {
		if (fill == 0)
			takeRouting();
		// The DAW's MIDI of this host block, placed on our running frame count
		if (pcontext && pcontext->processCounter != lastCounter) {
			lastCounter = pcontext->processCounter;
			for (uint32_t k = 0; k < pcontext->midiEventCount && hostEvCount < kMidiMax; ++k) {
				const CardinalDISTRHO::MidiEvent& ev = pcontext->midiEvents[k];
				if (ev.size == 0 || ev.size > 4)
					continue;
				const uint8_t* d = ev.size > CardinalDISTRHO::MidiEvent::kDataSize ? ev.dataExt : ev.data;
				if (!nest::channelPasses(d[0], rt.channel))
					continue;
				HostEv& h = hostEv[hostEvCount++];
				h.at = absFrame + ev.frame;
				h.size = (uint8_t) ev.size;
				for (uint32_t b = 0; b < 4; ++b)
					h.data[b] = b < ev.size ? d[b] : 0;
				h.data[0] = nest::toChannel(h.data[0], rt.channel);
			}
		}

		float v[nest::kJacks];
		bool patched[nest::kJacks];
		for (int j = 0; j < nest::kJacks; ++j) {
			patched[j] = inputs[IN_INPUT + j].isConnected();
			v[j] = patched[j] ? inputs[IN_INPUT + j].getVoltageSum() : 0.f;
			if (!patched[j])
				continue;
			if (rt.inAudio[j] >= 0) {
				inBuf[rt.inAudio[j]][fill] += v[j] / kFullScale;
				peakIn = std::max(peakIn, std::fabs(v[j]) / kFullScale);
			}
			if (rt.inCv[j] >= 0) {
				cvNow[rt.inCv[j]] = v[j];
				cvPatched[rt.inCv[j]] = true;
			}
		}
		if (rt.midiIn)
			noteMaker.process(rt.notes, v, patched, fill, notes);

		// BYPASS: the dry input one block late (as late as the plugin), blended in over 10 ms
		const bool bypass = params[BYPASS_PARAM].getValue() > 0.5f;
		bypassFade.step(bypass, args.sampleTime, pluginPlayed || !rt.active);
		const float mix = bypassFade.mix();
		float dry[nest::kJacks];
		for (int j = 0; j < nest::kJacks; ++j)
			dry[j] = dryDelay.swap(j, fill, v[j]);
		for (int j = 0; j < nest::kJacks; ++j) {
			float plug = 0.f;
			if (rt.outAudio[j] >= 0 && mix < 1.f) {
				const float s = outBuf[rt.outAudio[j]][fill];
				peakOut = std::max(peakOut, std::fabs(s));
				plug = s * kFullScale;
			}
			const float d = rt.bypassSrc[j] >= 0 ? dry[rt.bypassSrc[j]] : 0.f;
			outputs[OUT_OUTPUT + j].setVoltage(mix <= 0.f ? plug : mix >= 1.f ? d : nest::blend(plug, d, mix));
		}
		++absFrame;
		if (++fill == kBlock) {
			fill = 0;
			runBlock(bypassFade.pluginNeeded(bypass));
		}
		if (lightDivider.process())
			lights[BYPASS_LIGHT].setBrightness(bypass ? 1.f : 0.f);
	}

	void updateTime() {
		if (!pcontext)
			return;
		timeInfo.playing = pcontext->playing;
		timeInfo.frame = pcontext->frame;
		timeInfo.bbt.valid = pcontext->bbtValid;
		timeInfo.bbt.bar = pcontext->bar;
		timeInfo.bbt.beat = pcontext->beat;
		timeInfo.bbt.tick = pcontext->tick;
		timeInfo.bbt.barStartTick = pcontext->barStartTick;
		timeInfo.bbt.beatsPerBar = pcontext->beatsPerBar;
		timeInfo.bbt.beatType = pcontext->beatType;
		timeInfo.bbt.ticksPerBeat = pcontext->ticksPerBeat;
		timeInfo.bbt.beatsPerMinute = pcontext->beatsPerMinute;
	}

	void runBlock(bool runPlugin) {
		// Parameters from the CV slots
		for (int s = 0; s < nest::kSlots; ++s) {
			if (rt.slotParam[s] < 0 || !rt.plugin) {
				cvPatched[s] = false;
				continue;
			}
			nest::CvSlot c;
			c.param = rt.slotParam[s];
			c.base = rt.slotBase[s];
			c.rangeMin = rt.slotLo[s];
			c.rangeMax = rt.slotHi[s];
			c.last = rtLast[s];
			const float t = nest::slotTarget(c, params[DEPTH_PARAM + s].getValue(), cvNow[s], cvPatched[s]);
			if (nest::cvMoved(c, t)) {
				// Carla's VST3 / CLAP take a change from outside their process only as a "pending" value (the RT one
				// is dropped at the start of their next block); LV2 writes its port directly. No callbacks: the slot's
				// base must not follow the CV. Our engine is a plugin engine, so Carla allows this on the audio thread.
				const float value = rt.pMin[s] + t * (rt.pMax[s] - rt.pMin[s]);
				if (rt.lv2)
					rt.plugin->setParameterValueRT((uint32_t) c.param, value, 0, false);
				else
					rt.plugin->setParameterValue((uint32_t) c.param, value, false, false, false);
				rtLast[s] = t;
			}
			cvPatched[s] = false;
		}
		// MIDI: note ends first (routing changed, or the plugin comes back from a full bypass and missed some), then the
		// DAW's events due in this block, then our notes
		const bool plays = rt.active && runPlugin && handle;
		if (plays && !pluginPlayed)
			noteMaker.releaseAll(0, releaseEv);
		int n = 0;
		for (const nest::NoteEvent& ne : releaseEv) {
			if (n >= kMidiMax)
				break;
			NativeMidiEvent& e = midiOut[n++];
			e.time = 0;
			e.port = 0;
			e.size = 3;
			e.data[0] = ne.status;
			e.data[1] = ne.data1;
			e.data[2] = ne.data2;
			e.data[3] = 0;
		}
		releaseEv.clear();
		const uint64_t start = absFrame - kBlock;
		int keep = 0;
		for (int k = 0; k < hostEvCount; ++k) {
			const HostEv& h = hostEv[k];
			if (h.at < absFrame && n < kMidiMax) {
				NativeMidiEvent& e = midiOut[n++];
				e.time = (uint32_t) (h.at > start ? h.at - start : 0);
				e.port = 0;
				e.size = h.size;
				for (int b = 0; b < 4; ++b)
					e.data[b] = h.data[b];
			}
			else if (h.at >= absFrame)
				hostEv[keep++] = h;
		}
		hostEvCount = keep;
		for (const nest::NoteEvent& ne : notes) {
			if (n >= kMidiMax)
				break;
			NativeMidiEvent& e = midiOut[n++];
			e.time = (uint32_t) ne.frame;
			e.port = 0;
			e.size = 3;
			e.data[0] = ne.status;
			e.data[1] = ne.data1;
			e.data[2] = ne.data2;
			e.data[3] = 0;
		}
		notes.clear();
		// Events must be in time order
		std::stable_sort(midiOut, midiOut + n, [](const NativeMidiEvent& a, const NativeMidiEvent& b) { return a.time < b.time; });

		pluginPlayed = plays;
		if (pluginPlayed) {
			updateTime();
			float* ins[kPorts];
			float* outs[kPorts];
			for (int k = 0; k < kPorts; ++k) {
				ins[k] = inBuf[k];
				outs[k] = outBuf[k];
			}
			desc->process(handle, ins, outs, kBlock, midiOut, (uint32_t) n);
		}
		else
			std::memset(outBuf, 0, sizeof(outBuf));
		std::memset(inBuf, 0, sizeof(inBuf));
		meterIn = std::max(meterIn.load() * 0.f, peakIn);
		meterOut = std::max(meterOut.load() * 0.f, peakOut);
		peakIn = peakOut = 0.f;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		io.reset();
		for (nest::CvSlot& s : slots)
			s = nest::CvSlot();
		midiChannel = 0;
		post();
	}

	// ---- patch storage ----------------------------------------------------------------------------------------

	json_t* roleJ(const nest::Role& r) const {
		json_t* j = json_object();
		json_object_set_new(j, "kind", json_integer(r.kind));
		json_object_set_new(j, "index", json_integer(r.index));
		json_object_set_new(j, "name", json_string(r.name.c_str()));
		json_object_set_new(j, "note", json_integer(r.note));
		return j;
	}
	static nest::Role roleFrom(json_t* j) {
		nest::Role r;
		if (!j)
			return r;
		r.kind = (nest::RoleKind) json_integer_value(json_object_get(j, "kind"));
		r.index = (int) json_integer_value(json_object_get(j, "index"));
		if (json_t* n = json_object_get(j, "name"))
			r.name = json_string_value(n) ? json_string_value(n) : "";
		if (json_t* n = json_object_get(j, "note"))
			r.note = (int) json_integer_value(n);
		if (r.kind < nest::R_NONE || r.kind > nest::R_GATE_NOTE)
			r = nest::Role();
		return r;
	}

	json_t* dataToJson() override {
		json_t* root = settingsJson();
		// A plugin missing here: its saved entry and state go back unchanged (roles and slots are the current ones)
		if (json_t* p = missingJ ? json_object_get(missingJ, "plugin") : nullptr)
			json_object_set_new(root, "plugin", json_deep_copy(p));
		else if (hasPlugin) {
			json_t* p = json_object();
			json_object_set_new(p, "format", json_string(nest::formatName(plugin.format)));
			json_object_set_new(p, "id", json_string(plugin.id.c_str()));
			json_object_set_new(p, "label", json_string(plugin.label.c_str()));
			json_object_set_new(p, "name", json_string(plugin.name.c_str()));
			json_object_set_new(p, "file", json_string(plugin.file.c_str()));
			json_object_set_new(p, "state", json_string(saveState().c_str()));
			json_object_set_new(root, "plugin", p);
		}
		return root;
	}

	/** Roles, slots and the MIDI channel: everything but the plugin */
	json_t* settingsJson() {
		json_t* root = json_object();
		json_object_set_new(root, "version", json_integer(1));
		json_t* ins = json_array();
		json_t* outs = json_array();
		for (int k = 0; k < nest::kJacks; ++k) {
			json_array_append_new(ins, roleJ(io.in[k]));
			json_array_append_new(outs, roleJ(io.out[k]));
		}
		json_object_set_new(root, "in", ins);
		json_object_set_new(root, "out", outs);
		json_t* sl = json_array();
		for (const nest::CvSlot& s : slots) {
			json_t* j = json_object();
			json_object_set_new(j, "param", json_integer(s.param));
			json_object_set_new(j, "name", json_string(s.paramName.c_str()));
			json_object_set_new(j, "base", json_real(s.base));
			json_object_set_new(j, "min", json_real(s.rangeMin));
			json_object_set_new(j, "max", json_real(s.rangeMax));
			json_array_append_new(sl, j);
		}
		json_object_set_new(root, "slots", sl);
		json_object_set_new(root, "midiChannel", json_integer(midiChannel));
		return root;
	}

	static nest::PluginEntry entryFrom(json_t* p) {
		nest::PluginEntry e;
		const std::string fmt = json_string_value(json_object_get(p, "format")) ? json_string_value(json_object_get(p, "format")) : "";
		e.format = fmt == "VST2" ? nest::F_VST2 : fmt == "CLAP" ? nest::F_CLAP : fmt == "LV2" ? nest::F_LV2 : nest::F_VST3;
		auto str = [&](const char* k) { json_t* j = json_object_get(p, k); return j && json_string_value(j) ? std::string(json_string_value(j)) : std::string(); };
		e.id = str("id");
		e.label = str("label");
		e.name = str("name");
		e.file = str("file");
		e.midiIn = true;  // not known without the library: take the plugin's events port as MIDI
		return e;
	}

	/** The saved plugin of a module whose plugin is missing, as the library has it now (-1: still missing) */
	int missingFound() {
		json_t* p = missingJ ? json_object_get(missingJ, "plugin") : nullptr;
		if (!p)
			return -1;
		const nest::PluginEntry e = entryFrom(p);
		return nest::findById(Library::get().entries, e.format, e.id);
	}

	void dataFromJson(json_t* root) override {
		json_t* ins = json_object_get(root, "in");
		json_t* outs = json_object_get(root, "out");
		for (int k = 0; k < nest::kJacks; ++k) {
			if (ins)
				io.in[k] = roleFrom(json_array_get(ins, k));
			if (outs)
				io.out[k] = roleFrom(json_array_get(outs, k));
		}
		if (json_t* sl = json_object_get(root, "slots"))
			for (int s = 0; s < nest::kSlots; ++s)
				if (json_t* j = json_array_get(sl, s)) {
					nest::CvSlot c;
					c.param = (int) json_integer_value(json_object_get(j, "param"));
					if (json_t* n = json_object_get(j, "name"))
						c.paramName = json_string_value(n) ? json_string_value(n) : "";
					c.base = (float) json_number_value(json_object_get(j, "base"));
					c.rangeMin = (float) json_number_value(json_object_get(j, "min"));
					c.rangeMax = (float) json_number_value(json_object_get(j, "max"));
					slots[s] = c;
				}
		midiChannel = clamp((int) json_integer_value(json_object_get(root, "midiChannel")), 0, 16);
		// Undo of a role / slot / channel change: the plugin stays as it is (no reload, its window stays open)
		if (json_is_true(json_object_get(root, "keepPlugin"))) {
			post();
			return;
		}
		json_t* p = json_object_get(root, "plugin");
		if (!p) {
			dropPlugin();
			missingName.clear();
			if (missingJ) {
				json_decref(missingJ);
				missingJ = nullptr;
			}
			post();
			return;
		}
		const nest::PluginEntry e = entryFrom(p);
		const std::string state = json_string_value(json_object_get(p, "state")) ? json_string_value(json_object_get(p, "state")) : "";
		Library& lib = Library::get();
		lib.ensureLoaded();
		const int at = nest::findById(lib.entries, e.format, e.id);
		const bool ok = at >= 0 ? loadPlugin(lib.entries[at], state) : (system::exists(e.file) && loadPlugin(e, state));
		if (!ok) {
			dropPlugin();
			missingName = e.name.empty() ? e.id : e.name;
			if (missingJ)
				json_decref(missingJ);
			missingJ = json_deep_copy(root);
			post();
			return;
		}
		// The slots' parameters back to their base values (the saved state holds whatever CV had set)
		for (nest::CvSlot& s : slots)
			if (s.param >= 0 && s.param < (int) params_.size())
				carla_set_parameter_value(host, 0, (uint32_t) s.param, params_[s.param].min + s.base * (params_[s.param].max - params_[s.param].min));
		post();
	}

	/** UI thread: parameter changes from the plugin's window -> slot bases, LEARN */
	void uiIdle() {
		std::vector<std::pair<int, float>> ch;
		{
			std::lock_guard<std::mutex> lock(paramMutex);
			ch.swap(paramChanges);
		}
		bool changed = false;
		for (const auto& c : ch) {
			if (c.first >= (int) params_.size())
				continue;
			if (learnSlot >= 0 && !params_[c.first].name.empty()) {
				nest::CvSlot& s = slots[learnSlot];
				s = nest::CvSlot();
				s.param = c.first;
				s.paramName = params_[c.first].name;
				s.base = normalised(c.first, c.second);
				learnSlot = -1;
				changed = true;
				continue;
			}
			for (nest::CvSlot& s : slots)
				if (s.param == c.first) {
					s.base = normalised(c.first, c.second);
					changed = true;
				}
		}
		if (changed)
			post();
		if (handle && desc && desc->ui_idle)
			desc->ui_idle(handle);
	}
};

// ---------------------------------------------------------------------------------------------------------------
// Undo: the whole module data before and after a change

struct NestChange : history::ModuleAction {
	json_t* before = nullptr;
	json_t* after = nullptr;
	~NestChange() override {
		if (before)
			json_decref(before);
		if (after)
			json_decref(after);
	}
	void apply(json_t* j) {
		Nest* m = dynamic_cast<Nest*>(APP->engine->getModule(moduleId));
		if (m && j)
			m->dataFromJson(j);
	}
	void undo() override { apply(before); }
	void redo() override { apply(after); }
};

/** Runs `change` on the module and records it for undo. Only a plugin load / remove keeps the plugin and its state
 * (`whole`); the other changes keep the settings, so undoing them does not reload the plugin. */
template <typename F>
void withUndo(Nest* m, const char* name, F change, bool whole = false) {
	auto snap = [m, whole]() {
		if (whole)
			return m->dataToJson();
		json_t* j = m->settingsJson();
		json_object_set_new(j, "keepPlugin", json_true());
		return j;
	};
	NestChange* h = new NestChange;
	h->moduleId = m->id;
	h->name = name;
	h->before = snap();
	change();
	h->after = snap();
	APP->history->push(h);
}

// ---------------------------------------------------------------------------------------------------------------
// The display

std::string fitText(NVGcontext* vg, std::string s, float size, float width) {
	namespace st = jxmp::style;
	std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL);
	if (!f)
		return s;
	nvgFontFaceId(vg, f->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, 0.8f);
	float b[4];
	for (size_t max = s.size(); max > 4; --max) {
		const std::string t = jxmp::sampler::shortenMiddle(s, max);
		if (nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, b) <= width)
			return t;
	}
	return s;
}

struct NestDisplay : widget::OpaqueWidget {
	enum Screen { MAIN, BROWSE, IO, ROLES, PARAMS, RANGE };
	Nest* module = nullptr;
	Screen screen = MAIN;
	int slotSel = 0, slotTop = 0;
	// browser
	nest::ListTab tab = nest::T_ALL;
	int brSel = 0, brTop = 0;
	// I/O table and the role list
	int ioSel = 0, ioTop = 0;
	std::vector<nest::Choice> roleChoices;
	int roleSel = 0, roleTop = 0;
	// parameter list
	std::vector<int> paramIdx;
	int parSel = 0, parTop = 0;
	// RANGE
	bool rangeMax = false;
	float rangeValue = 0.f;
	int menuTop = 0;
	std::string msg;
	double msgUntil = 0.0;

	static constexpr int kRows = 4;

	void say(const std::string& s) {
		msg = s;
		msgUntil = system::getTime() + 2.5;
	}

	std::vector<int> browseList() const { return nest::listFor(Library::get().entries, tab); }

	// ---- buttons ----

	void press(int which) {
		if (!module)
			return;
		nest::Menu& m = module->menu;
		nest::Menu& sm = module->slotMenu;
		if (m.open || sm.open) {
			nest::Menu& cur = m.open ? m : sm;
			if (which == Nest::MENU_PARAM)
				cur.pressMenu();
			else if (which == Nest::UP_PARAM)
				cur.up();
			else if (which == Nest::DOWN_PARAM)
				cur.down();
			else if (which == Nest::OK_PARAM)
				run(cur.ok());
			return;
		}
		if (module->learnSlot >= 0) {
			if (which == Nest::MENU_PARAM || which == Nest::OK_PARAM)
				module->learnSlot = -1;
			return;
		}
		switch (screen) {
			case MAIN:
				if (which == Nest::MENU_PARAM) {
					m.midiNow = module->midiChannel;
					m.pressMenu();
					menuTop = 0;
				}
				else if (which == Nest::UP_PARAM)
					slotSel = (slotSel + nest::kSlots - 1) % nest::kSlots;
				else if (which == Nest::DOWN_PARAM)
					slotSel = (slotSel + 1) % nest::kSlots;
				else if (which == Nest::OK_PARAM && module->hasPlugin) {
					sm.pressMenu();
					menuTop = 0;
				}
				break;
			case BROWSE: {
				const std::vector<int> l = browseList();
				if (which == Nest::MENU_PARAM)
					screen = MAIN;
				else if (which == Nest::UP_PARAM) {
					if (brSel <= 0) {
						tab = (nest::ListTab) ((tab + 2) % 3);
						brSel = brTop = 0;
					}
					else
						--brSel;
				}
				else if (which == Nest::DOWN_PARAM) {
					if (brSel + 1 < (int) l.size())
						++brSel;
				}
				else if (which == Nest::OK_PARAM && brSel < (int) l.size()) {
					const nest::PluginEntry e = Library::get().entries[l[brSel]];
					bool ok = false;
					withUndo(module, "JX-NEST load plugin", [&]() { ok = module->loadPlugin(e); }, true);
					say(ok ? "LOADED " + e.name : "DID NOT LOAD: " + e.name);
					screen = MAIN;
				}
				break;
			}
			case IO:
				if (which == Nest::MENU_PARAM)
					screen = MAIN;
				else if (which == Nest::UP_PARAM)
					ioSel = (ioSel + 2 * nest::kJacks - 1) % (2 * nest::kJacks);
				else if (which == Nest::DOWN_PARAM)
					ioSel = (ioSel + 1) % (2 * nest::kJacks);
				else if (which == Nest::OK_PARAM)
					openRoles();
				break;
			case ROLES:
				if (which == Nest::MENU_PARAM)
					screen = IO;
				else if (which == Nest::UP_PARAM)
					stepRole(-1);
				else if (which == Nest::DOWN_PARAM)
					stepRole(1);
				else if (which == Nest::OK_PARAM)
					pickRole();
				break;
			case PARAMS:
				if (which == Nest::MENU_PARAM)
					screen = MAIN;
				else if (!paramIdx.empty() && which == Nest::UP_PARAM)
					parSel = (parSel + (int) paramIdx.size() - 1) % (int) paramIdx.size();
				else if (!paramIdx.empty() && which == Nest::DOWN_PARAM)
					parSel = (parSel + 1) % (int) paramIdx.size();
				else if (which == Nest::OK_PARAM && parSel < (int) paramIdx.size()) {
					const int p = paramIdx[parSel];
					withUndo(module, "JX-NEST slot parameter", [&]() {
						nest::CvSlot& s = module->slots[slotSel];
						s = nest::CvSlot();
						s.param = p;
						s.paramName = module->params_[p].name;
						s.base = module->normalised(p, carla_get_current_parameter_value(module->host, 0, (uint32_t) p));
						module->post();
					});
					say(string::f("SLOT %d = ", slotSel + 1) + module->params_[p].name);
					screen = MAIN;
				}
				break;
			case RANGE:
				if (which == Nest::MENU_PARAM)
					screen = MAIN;
				else if (which == Nest::UP_PARAM)
					rangeValue = math::clamp(rangeValue + 0.05f, 0.f, 1.f);
				else if (which == Nest::DOWN_PARAM)
					rangeValue = math::clamp(rangeValue - 0.05f, 0.f, 1.f);
				else if (which == Nest::OK_PARAM) {
					withUndo(module, "JX-NEST slot range", [&]() {
						nest::CvSlot& s = module->slots[slotSel];
						(rangeMax ? s.rangeMax : s.rangeMin) = rangeValue;
						module->post();
					});
					screen = MAIN;
				}
				break;
		}
	}

	void run(const nest::MenuResult& r) {
		Nest* const m = module;
		Library& lib = Library::get();
		switch (r.action) {
			case nest::A_LOAD:
				lib.ensureLoaded();
				screen = BROWSE;
				brSel = brTop = 0;
				break;
			case nest::A_RESCAN:
				lib.rescan();
				screen = BROWSE;
				brSel = brTop = 0;
				break;
			case nest::A_ADD_FOLDER:
				async_dialog_text_input("Add a plugin folder (its path)", rack::homeDir().c_str(), [this](char* path) {
					if (!path)
						return;
					std::string p = path;
					std::free(path);
					if (!system::isDirectory(p)) {
						say("NOT A FOLDER");
						return;
					}
					Library::get().ensureLoaded();
					Library::get().addFolder(p);
					screen = BROWSE;
				});
				break;
			case nest::A_REMOVE:
				withUndo(m, "JX-NEST remove plugin", [&]() {
					m->dropPlugin();
					if (m->missingJ) {
						json_decref(m->missingJ);
						m->missingJ = nullptr;
					}
					m->missingName.clear();
					m->post();
				}, true);
				break;
			case nest::A_IO_TABLE:
				screen = IO;
				ioSel = ioTop = 0;
				break;
			case nest::A_MIDI_CHANNEL:
				withUndo(m, "JX-NEST MIDI channel", [&]() {
					m->midiChannel = (int) r.value;
					m->post();
				});
				say(m->midiChannel ? string::f("MIDI CHANNEL %d", m->midiChannel) : "MIDI: ALL CHANNELS");
				break;
			case nest::A_CLEAR_SLOTS:
				withUndo(m, "JX-NEST clear slots", [&]() {
					for (nest::CvSlot& s : m->slots)
						s = nest::CvSlot();
					m->post();
				});
				break;
			case nest::A_LEARN:
				m->learnSlot = slotSel;
				{
					std::lock_guard<std::mutex> lock(m->paramMutex);
					m->paramChanges.clear();
				}
				if (!m->uiShown && m->hasPlugin) {
					carla_show_custom_ui(m->host, 0, true);
					m->uiShown = true;
				}
				break;
			case nest::A_CHOOSE:
				paramIdx.clear();
				for (int k = 0; k < (int) m->params_.size(); ++k)
					if (!m->params_[k].name.empty())
						paramIdx.push_back(k);
				parSel = parTop = 0;
				for (int k = 0; k < (int) paramIdx.size(); ++k)
					if (paramIdx[k] == m->slots[slotSel].param)
						parSel = k;
				screen = PARAMS;
				break;
			case nest::A_RANGE_MIN:
			case nest::A_RANGE_MAX:
				if (m->slots[slotSel].param < 0)
					break;
				rangeMax = r.action == nest::A_RANGE_MAX;
				rangeValue = rangeMax ? m->slots[slotSel].rangeMax : m->slots[slotSel].rangeMin;
				screen = RANGE;
				break;
			case nest::A_SLOT_REMOVE:
				withUndo(m, "JX-NEST remove slot", [&]() {
					m->slots[slotSel] = nest::CvSlot();
					m->post();
				});
				break;
			default:
				break;
		}
	}

	nest::Role& ioRole(int row) { return row < nest::kJacks ? module->io.in[row] : module->io.out[row - nest::kJacks]; }

	void openRoles() {
		const bool isIn = ioSel < nest::kJacks;
		roleChoices = nest::choices(isIn, module->ports);
		roleSel = roleTop = 0;
		const nest::Role& cur = ioRole(ioSel);
		for (int k = 0; k < (int) roleChoices.size(); ++k)
			if (!roleChoices[k].head && roleChoices[k].role.kind == cur.kind && roleChoices[k].role.index == cur.index)
				roleSel = k;
		screen = ROLES;
	}
	void stepRole(int d) {
		const int n = (int) roleChoices.size();
		if (n == 0)
			return;
		int k = roleSel;
		do
			k = (k + d + n) % n;
		while (roleChoices[k].head);
		roleSel = k;
	}
	void pickRole() {
		if (roleSel >= (int) roleChoices.size())
			return;
		nest::Role r = roleChoices[roleSel].role;
		const bool isIn = ioSel < nest::kJacks;
		withUndo(module, "JX-NEST jack role", [&]() {
			nest::Role& cur = ioRole(ioSel);
			if (r.kind == nest::R_GATE_NOTE && cur.kind == nest::R_GATE_NOTE)
				r.note = cur.note;
			cur = r;
			module->post();
		});
		say(string::f("%s %d %s ", isIn ? "IN" : "OUT", (ioSel % nest::kJacks) + 1, isIn ? "\xC2\xBB" : "\xC2\xAB") + nest::roleText(r, isIn));
		screen = IO;
	}

	// ---- drawing ----

	NVGcolor lit() const { return jxmp::style::vfdTint(kColour); }
	NVGcolor dim(float a) const {
		const NVGcolor l = lit();
		return nvgRGBAf(l.r, l.g, l.b, a);
	}
	float left() const { return mm2px(3.f); }
	float right() const { return box.size.x - mm2px(3.f); }
	float listY0() const { return mm2px(6.6f) + 3.f * mm2px(0.55f); }
	float listEnd() const { return box.size.y - jxmp::style::vfdBezel(box.size) - 3.f * mm2px(0.55f); }
	float rowH() const { return (listEnd() - listY0()) / kRows; }

	void header(NVGcontext* vg, const std::string& l, const std::string& r) {
		namespace st = jxmp::style;
		const float y = mm2px(4.f);
		st::vfdText(vg, math::Vec(left(), y), fitText(vg, l, 10.f, (right() - left()) * (r.empty() ? 1.f : 0.62f)), 10.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lit());
		if (!r.empty())
			st::vfdLegend(vg, math::Vec(right(), y), r, 6.5f, true, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.6f));
		nvgBeginPath(vg);
		nvgMoveTo(vg, left(), mm2px(6.6f));
		nvgLineTo(vg, right(), mm2px(6.6f));
		nvgStrokeWidth(vg, 0.7f);
		nvgStrokeColor(vg, dim(0.35f));
		nvgStroke(vg);
	}

	/** The highlight bar of row r (in the visible rows) */
	float rowY(int r) const { return listY0() + r * rowH() + rowH() / 2; }
	void highlight(NVGcontext* vg, float y) {
		nvgBeginPath(vg);
		nvgRect(vg, left(), y - rowH() / 2 + 0.8f, right() - mm2px(2.f) - left(), rowH() - 1.6f);
		nvgFillColor(vg, dim(0.95f));
		nvgFill(vg);
	}
	void rowText(NVGcontext* vg, float x, float y, const std::string& s, float size, bool on, int align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE) {
		namespace st = jxmp::style;
		if (on)
			st::text(vg, math::Vec(x, y), s, size, nvgRGB(0x0a, 0x14, 0x12), st::FONT_LABEL, align, 0.8f);
		else
			st::vfdText(vg, math::Vec(x, y), s, size, align, lit());
	}
	void scrollbar(NVGcontext* vg, int top, int count) {
		if (count <= kRows)
			return;
		const float y0 = listY0(), bh = listEnd() - y0, bx = right() - mm2px(0.6f);
		nvgBeginPath(vg);
		nvgRect(vg, bx, y0, 1.5f, bh);
		nvgFillColor(vg, dim(0.15f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, bx - 0.5f, y0 + bh * top / count, 2.5f, bh * kRows / count);
		nvgFillColor(vg, dim(0.8f));
		nvgFill(vg);
	}
	static void follow(int sel, int& top, int count) {
		if (sel < top)
			top = sel;
		if (sel >= top + kRows)
			top = sel - kRows + 1;
		top = math::clamp(top, 0, std::max(0, count - kRows));
	}

	void drawMenu(NVGcontext* vg, const nest::Menu& m, const std::string& title) {
		header(vg, m.depth ? m.path() : title, m.depth ? "MENU = BACK" : "MENU = CLOSE");
		if (m.confirming) {
			jxmp::style::vfdText(vg, math::Vec(box.size.x / 2, mm2px(15.f)), std::string(m.item().name) + "?", 10.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, lit());
			jxmp::style::vfdLegend(vg, math::Vec(box.size.x / 2, mm2px(21.f)), "OK = YES     MENU = NO", 7.5f, true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.75f));
			return;
		}
		follow(m.selected(), menuTop, m.count());
		for (int r = 0; r < kRows; ++r) {
			const int i = menuTop + r;
			if (i >= m.count())
				break;
			const nest::MenuItem& it = m.current()[i];
			const float y = rowY(r);
			const bool on = i == m.selected();
			if (on)
				highlight(vg, y);
			rowText(vg, left() + mm2px(2.f), y, it.name, 8.5f, on);
			const NVGcolor c = on ? nvgRGB(0x0a, 0x14, 0x12) : lit();
			const float ax = right() - mm2px(4.f);
			if (it.sub) {
				nvgBeginPath(vg);
				nvgMoveTo(vg, ax - 3.f, y - 3.5f);
				nvgLineTo(vg, ax + 2.f, y);
				nvgLineTo(vg, ax - 3.f, y + 3.5f);
				nvgClosePath(vg);
				nvgFillColor(vg, c);
				nvgFill(vg);
			}
			if (it.action == nest::A_MIDI_CHANNEL && (int) it.value == module->midiChannel) {
				nvgBeginPath(vg);
				nvgCircle(vg, ax, y, 2.f);
				nvgFillColor(vg, c);
				nvgFill(vg);
			}
		}
		scrollbar(vg, menuTop, m.count());
	}

	void bar(NVGcontext* vg, float x, float w, float y, float base, float now, bool on) {
		const float h = mm2px(1.6f);
		nvgBeginPath(vg);
		nvgRect(vg, x, y - h / 2, w, h);
		nvgFillColor(vg, on ? nvgRGBA(0x0a, 0x14, 0x12, 0x30) : dim(0.14f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, x, y - h / 2, w * now, h);
		nvgFillColor(vg, on ? nvgRGBA(0x0a, 0x14, 0x12, 0xa0) : dim(0.85f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, x + w * base - 0.8f, y - h, 1.6f, 2.f * h);
		nvgFillColor(vg, on ? nvgRGB(0x0a, 0x14, 0x12) : nvgRGB(0xff, 0xff, 0xff));
		nvgFill(vg);
	}

	bool slotPatched(int s) const {
		for (int k = 0; k < nest::kJacks; ++k)
			if (module->io.in[k].kind == nest::R_CV && module->io.in[k].index == s && module->inputs[Nest::IN_INPUT + k].isConnected())
				return true;
		return false;
	}

	/** BYPASS off: a line through a box (the plugin); on: the line arcs over the dimmed box */
	void bypassIcon(NVGcontext* vg, float cx, float cy, bool on) {
		const float hw = mm2px(2.6f), bw = mm2px(0.9f), bh = mm2px(0.65f), up = mm2px(2.6f);
		nvgBeginPath(vg);
		nvgRect(vg, cx - bw, cy - bh, 2.f * bw, 2.f * bh);
		nvgStrokeWidth(vg, 0.8f);
		nvgStrokeColor(vg, dim(on ? 0.3f : 0.9f));
		nvgStroke(vg);
		nvgBeginPath(vg);
		if (on) {
			nvgMoveTo(vg, cx - hw, cy);
			nvgLineTo(vg, cx - bw - mm2px(0.5f), cy);
			nvgBezierTo(vg, cx - bw, cy - up, cx + bw, cy - up, cx + bw + mm2px(0.5f), cy);
			nvgLineTo(vg, cx + hw, cy);
		}
		else {
			nvgMoveTo(vg, cx - hw, cy);
			nvgLineTo(vg, cx - bw, cy);
			nvgMoveTo(vg, cx + bw, cy);
			nvgLineTo(vg, cx + hw, cy);
		}
		nvgStrokeWidth(vg, 0.9f);
		nvgLineCap(vg, NVG_ROUND);
		nvgStrokeColor(vg, on ? lit() : dim(0.9f));
		nvgStroke(vg);
		nvgLineCap(vg, NVG_BUTT);
	}

	void drawMeters(NVGcontext* vg) {
		namespace st = jxmp::style;
		const float x = right() - mm2px(19.f), w = mm2px(14.f), h = mm2px(0.9f);
		const float v[2] = {module->meterIn.load(), module->meterOut.load()};
		for (int k = 0; k < 2; ++k) {
			const float y = mm2px(2.6f) + k * mm2px(2.1f);
			st::vfdLegend(vg, math::Vec(x - mm2px(1.f), y), k ? "OUT" : "IN", 5.f, true, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.6f));
			nvgBeginPath(vg);
			nvgRect(vg, x, y - h / 2, w, h);
			nvgFillColor(vg, dim(0.15f));
			nvgFill(vg);
			const float db = v[k] > 1e-5f ? 20.f * std::log10(v[k]) : -60.f;
			const float t = math::clamp((db + 48.f) / 48.f, 0.f, 1.f);
			nvgBeginPath(vg);
			nvgRect(vg, x, y - h / 2, w * t, h);
			nvgFillColor(vg, v[k] > 1.f ? nvgRGB(0xff, 0x60, 0x50) : dim(0.9f));
			nvgFill(vg);
		}
		const CardinalPluginContext* pc = module->pcontext;
		if (pc) {
			// Tempo, then the bypass symbol: the signal through the plugin (box), or round it while BYPASS is on
			const float gx = x - mm2px(9.5f), gy = mm2px(4.f);
			st::vfdLegend(vg, math::Vec(gx - mm2px(3.4f), gy), string::f("%d BPM", (int) std::round(pc->beatsPerMinute)), 6.f, true,
			              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, dim(0.75f));
			bypassIcon(vg, gx, gy, module->params[Nest::BYPASS_PARAM].getValue() > 0.5f);
		}
	}

	void drawMain(NVGcontext* vg) {
		namespace st = jxmp::style;
		Nest* const m = module;
		if (!m->missingName.empty()) {
			header(vg, m->missingName, "MISSING");
			st::vfdText(vg, math::Vec(box.size.x / 2, mm2px(13.f)), "PLUGIN NOT INSTALLED HERE", 9.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, lit());
			st::vfdLegend(vg, math::Vec(box.size.x / 2, mm2px(18.5f)), "ITS SETTINGS ARE KEPT IN THE PATCH", 6.5f, true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.6f));
			st::vfdLegend(vg, math::Vec(box.size.x / 2, mm2px(22.5f)), "INSTALL IT, THEN MENU > PLUGIN > RESCAN", 6.5f, true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.6f));
			return;
		}
		if (!m->hasPlugin) {
			header(vg, "NO PLUGIN", "");
			st::vfdText(vg, math::Vec(box.size.x / 2, mm2px(14.f)), "MENU > PLUGIN > LOAD", 9.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, lit());
			st::vfdLegend(vg, math::Vec(box.size.x / 2, mm2px(20.f)), "VST3  CLAP  LV2   (OR DROP A PLUGIN HERE)", 6.5f, true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.55f));
			return;
		}
		header(vg, m->plugin.name, " ");
		drawMeters(vg);
		follow(slotSel, slotTop, nest::kSlots);
		for (int r = 0; r < kRows; ++r) {
			const int i = slotTop + r;
			const nest::CvSlot& s = m->slots[i];
			const float y = rowY(r);
			const bool on = i == slotSel;
			if (on)
				highlight(vg, y);
			rowText(vg, left() + mm2px(1.5f), y, string::f("%d", i + 1), 8.f, on);
			const float x0 = left() + mm2px(6.f);
			if (m->learnSlot == i) {
				rowText(vg, x0, y, "LEARN: MOVE A KNOB IN THE PLUGIN WINDOW", 7.f, on);
				continue;
			}
			if (s.param < 0 || s.param >= (int) m->params_.size()) {
				rowText(vg, x0, y, "\xE2\x80\x94   OK = LEARN / CHOOSE", 7.f, on);
				continue;
			}
			const float barX = left() + mm2px(44.f), barW = mm2px(36.f);
			rowText(vg, x0, y, fitText(vg, s.paramName, 8.f, barX - x0 - mm2px(6.f)), 8.f, on);
			const bool cv = slotPatched(i);
			if (cv)
				rowText(vg, barX - mm2px(2.f), y, "CV", 5.5f, on, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			const float now = m->normalised(s.param, carla_get_current_parameter_value(m->host, 0, (uint32_t) s.param));
			bar(vg, barX, barW, y, s.base, now, on);
			const char* txt = carla_get_parameter_text(m->host, 0, (uint32_t) s.param);
			rowText(vg, right() - mm2px(3.f), y, fitText(vg, txt ? std::string(txt) : "", 7.f, right() - mm2px(3.f) - barX - barW - mm2px(2.f)), 7.f, on,
			        NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		}
		scrollbar(vg, slotTop, nest::kSlots);
	}

	void drawBrowse(NVGcontext* vg) {
		namespace st = jxmp::style;
		Library& lib = Library::get();
		const std::vector<int> l = browseList();
		if (brSel >= (int) l.size())
			brSel = std::max(0, (int) l.size() - 1);
		header(vg, "LOAD PLUGIN", lib.scanning ? string::f("SCANNING... %d FOUND", (int) lib.found.size()) : string::f("%d/%d", l.empty() ? 0 : brSel + 1, (int) l.size()));
		// Tabs (▲ on the first row switches them)
		const char* tabs[3] = {"ALL", "INSTRUMENTS", "EFFECTS"};
		float x = left();
		const float ty = mm2px(8.4f);
		for (int k = 0; k < 3; ++k) {
			st::vfdLegend(vg, math::Vec(x, ty), tabs[k], 6.f, k == tab, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, k == tab ? lit() : dim(0.35f));
			x += mm2px(4.f) + std::strlen(tabs[k]) * mm2px(1.55f);
		}
		if (!lib.error.empty()) {
			st::vfdText(vg, math::Vec(box.size.x / 2, mm2px(17.f)), lib.error, 9.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, lit());
			return;
		}
		if (l.empty()) {
			st::vfdLegend(vg, math::Vec(box.size.x / 2, mm2px(17.f)), lib.scanning ? "LOOKING FOR PLUGINS..." : "NO PLUGINS FOUND  (MENU > PLUGIN > ADD FOLDER)", 7.f, true,
			              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.7f));
			return;
		}
		const float y0 = mm2px(10.4f), rh = (listEnd() - y0) / kRows;
		follow(brSel, brTop, (int) l.size());
		for (int r = 0; r < kRows; ++r) {
			const int i = brTop + r;
			if (i >= (int) l.size())
				break;
			const nest::PluginEntry& e = lib.entries[l[i]];
			const float y = y0 + r * rh + rh / 2;
			const bool on = i == brSel;
			if (on) {
				nvgBeginPath(vg);
				nvgRect(vg, left(), y - rh / 2 + 0.6f, right() - mm2px(2.f) - left(), rh - 1.2f);
				nvgFillColor(vg, dim(0.95f));
				nvgFill(vg);
			}
			const std::string kind = std::string(e.instrument ? "INST" : "FX") + "  " + nest::formatName(e.format);
			rowText(vg, left() + mm2px(2.f), y, fitText(vg, e.name, 7.5f, mm2px(70.f)), 7.5f, on);
			rowText(vg, right() - mm2px(3.f), y, kind, 5.5f, on, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		}
		scrollbar(vg, brTop, (int) l.size());
	}

	void drawIO(NVGcontext* vg) {
		namespace st = jxmp::style;
		Nest* const m = module;
		header(vg, "I/O  " + (m->hasPlugin ? m->plugin.name : std::string("NO PLUGIN")), "MENU = BACK");
		follow(ioSel, ioTop, 2 * nest::kJacks);
		for (int r = 0; r < kRows; ++r) {
			const int i = ioTop + r;
			const bool isIn = i < nest::kJacks;
			const int jack = i % nest::kJacks;
			const nest::Role& role = isIn ? m->io.in[jack] : m->io.out[jack];
			const float y = rowY(r);
			const bool on = i == ioSel;
			if (on)
				highlight(vg, y);
			rowText(vg, left() + mm2px(2.f), y, string::f("%s %d", isIn ? "IN" : "OUT", jack + 1), 8.f, on);
			const bool cable = isIn ? m->inputs[Nest::IN_INPUT + jack].isConnected() : m->outputs[Nest::OUT_OUTPUT + jack].isConnected();
			nvgBeginPath(vg);
			nvgCircle(vg, left() + mm2px(18.f), y, mm2px(0.7f));
			nvgFillColor(vg, cable ? (on ? nvgRGB(0x0a, 0x14, 0x12) : nvgRGB(0xe0, 0x56, 0x7a)) : (on ? nvgRGBA(0x0a, 0x14, 0x12, 0x50) : dim(0.2f)));
			nvgFill(vg);
			rowText(vg, left() + mm2px(22.f), y, isIn ? "\xC2\xBB" : "\xC2\xAB", 8.f, on, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			const bool valid = !m->hasPlugin || nest::roleValid(role, isIn, m->ports);
			std::string t = role.kind == nest::R_AUDIO ? jxmp::nest::upper(role.name) : nest::roleText(role, isIn);
			if (!valid)
				t = "\xC3\x97 " + t;
			rowText(vg, left() + mm2px(26.f), y, fitText(vg, t, 8.f, mm2px(56.f)), 8.f, on);
			(void) st::vfdBezel;
		}
		scrollbar(vg, ioTop, 2 * nest::kJacks);
	}

	void drawRoles(NVGcontext* vg) {
		namespace st = jxmp::style;
		const bool isIn = ioSel < nest::kJacks;
		header(vg, string::f("%s %d %s PLUGIN", isIn ? "IN" : "OUT", ioSel % nest::kJacks + 1, isIn ? "\xC2\xBB" : "\xC2\xAB"), "MENU = BACK");
		follow(roleSel, roleTop, (int) roleChoices.size());
		const nest::Role& cur = ioRole(ioSel);
		for (int r = 0; r < kRows; ++r) {
			const int i = roleTop + r;
			if (i >= (int) roleChoices.size())
				break;
			const nest::Choice& c = roleChoices[i];
			const float y = rowY(r);
			if (c.head) {
				st::vfdLegend(vg, math::Vec(left() + mm2px(1.f), y), c.text, 5.5f, true, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dim(0.5f));
				continue;
			}
			const bool on = i == roleSel;
			if (on)
				highlight(vg, y);
			rowText(vg, left() + mm2px(4.f), y, c.text, 8.f, on);
			if (c.role.kind == cur.kind && c.role.index == cur.index) {
				nvgBeginPath(vg);
				nvgCircle(vg, right() - mm2px(4.f), y, 2.f);
				nvgFillColor(vg, on ? nvgRGB(0x0a, 0x14, 0x12) : lit());
				nvgFill(vg);
			}
		}
		scrollbar(vg, roleTop, (int) roleChoices.size());
	}

	void drawParams(NVGcontext* vg) {
		header(vg, string::f("SLOT %d: PARAMETER", slotSel + 1), paramIdx.empty() ? "MENU = BACK" : string::f("%d/%d", parSel + 1, (int) paramIdx.size()));
		follow(parSel, parTop, (int) paramIdx.size());
		for (int r = 0; r < kRows; ++r) {
			const int i = parTop + r;
			if (i >= (int) paramIdx.size())
				break;
			const float y = rowY(r);
			const bool on = i == parSel;
			if (on)
				highlight(vg, y);
			rowText(vg, left() + mm2px(2.f), y, fitText(vg, module->params_[paramIdx[i]].name, 8.f, mm2px(90.f)), 8.f, on);
		}
		scrollbar(vg, parTop, (int) paramIdx.size());
	}

	void drawRange(NVGcontext* vg) {
		namespace st = jxmp::style;
		const nest::CvSlot& s = module->slots[slotSel];
		header(vg, string::f("SLOT %d: RANGE %s", slotSel + 1, rangeMax ? "MAX" : "MIN"), "OK = SET  MENU = BACK");
		st::vfdText(vg, math::Vec(box.size.x / 2, mm2px(12.5f)), string::f("%d %%", (int) std::round(rangeValue * 100.f)), 12.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, lit());
		const float lo = rangeMax ? s.rangeMin : rangeValue, hi = rangeMax ? rangeValue : s.rangeMax;
		const float x = left() + mm2px(8.f), w = right() - left() - mm2px(16.f), y = mm2px(20.f);
		nvgBeginPath(vg);
		nvgRect(vg, x, y - 2.f, w, 4.f);
		nvgFillColor(vg, dim(0.15f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, x + w * std::min(lo, hi), y - 2.f, w * std::fabs(hi - lo), 4.f);
		nvgFillColor(vg, dim(0.85f));
		nvgFill(vg);
		st::vfdLegend(vg, math::Vec(box.size.x / 2, mm2px(24.5f)), "UP / DOWN: 5 %", 6.f, true, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim(0.55f));
	}

	void draw(const DrawArgs& args) override {
		namespace st = jxmp::style;
		NVGcontext* const vg = args.vg;
		st::vfdWindow(vg, box.size, lit());
		if (!module) {
			header(vg, "JX-NEST", "");
			st::vfdText(vg, math::Vec(box.size.x / 2, mm2px(15.f)), "PLUGIN HOST", 9.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, lit());
			return;
		}
		if (module->menu.open)
			drawMenu(vg, module->menu, "MENU");
		else if (module->slotMenu.open)
			drawMenu(vg, module->slotMenu, string::f("SLOT %d", slotSel + 1));
		else if (screen == BROWSE)
			drawBrowse(vg);
		else if (screen == IO)
			drawIO(vg);
		else if (screen == ROLES)
			drawRoles(vg);
		else if (screen == PARAMS)
			drawParams(vg);
		else if (screen == RANGE)
			drawRange(vg);
		else
			drawMain(vg);
		if (!msg.empty() && system::getTime() < msgUntil) {
			const float h = mm2px(4.f), y = listEnd() - h;
			nvgBeginPath(vg);
			nvgRect(vg, left(), y, right() - left(), h);
			nvgFillColor(vg, nvgRGB(0x0b, 0x16, 0x15));
			nvgFill(vg);
			st::text(vg, math::Vec(box.size.x / 2, y + h / 2), fitText(vg, msg, 7.f, right() - left() - mm2px(2.f)), 7.f, nvgRGB(0xff, 0xff, 0xff), st::FONT_LABEL,
			         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
		}
		st::vfdGlare(vg, box.size);
	}
};

// ---------------------------------------------------------------------------------------------------------------
// The panel

/** A latching button lit while on (BYPASS) */
struct BypassButton : VCVLightLatch<MediumSimpleLight<WhiteLight>> {};

const float kW = 121.92f;
// Back: two rows of six jacks for IN, two for OUT
inline float jackX(int k) { return 15.f + (k % 6) * 18.4f; }
const float kInY[2] = {37.f, 53.f}, kOutY[2] = {78.f, 94.f};

struct NestWidget : JXMPModuleWidget {
	NestDisplay* display = nullptr;
	bool openHeld = false;
	std::string dropFile;  // a dropped plugin waiting for its scan
	nest::Format dropFormat = nest::F_VST3;
	int dropAfter = 0, scansSeen = -1;

	NestWidget(Nest* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-NEST", kColour, 24);
		styled();

		section(3.f, 19.f, kW - 6.f, 34.f, "PLUGIN");
		display = new NestDisplay;
		display->module = module;
		display->box = math::Rect(mm2px(math::Vec(5.f, 22.2f)), mm2px(math::Vec(kW - 10.f, 29.f)));
		addChild(display);

		// Controls: the menu buttons, OPEN UI, BYPASS
		const float mid = 63.5f, menuX = 22.f, dx = 4.4f, dy = 4.1f;
		const IconButton::Icon icons[4] = {IconButton::UP, IconButton::DOWN, IconButton::MENU, IconButton::OK};
		const int ids[4] = {Nest::UP_PARAM, Nest::DOWN_PARAM, Nest::MENU_PARAM, Nest::OK_PARAM};
		for (int k = 0; k < 4; ++k) {
			IconButton* const b = createParamCentered<IconButton>(mm2px(math::Vec(menuX + (k < 2 ? -dx : dx), mid + (k % 2 ? dy : -dy))), module, ids[k]);
			b->icon = icons[k];
			const int which = ids[k];
			b->action = [this, which]() {
				if (display)
					display->press(which);
			};
			addParam(b);
		}
		{
			const float pad = 1.5f, rb = 3.05f;
			const math::Vec a(menuX - dx - rb - pad, mid - dy - rb - pad - 0.6f);
			const math::Vec b(menuX + dx + rb + pad, mid + dy + rb + pad);
			MenuFrame* const fr = new MenuFrame;
			// Lit in the menu and on every screen reached from it (browser, I/O table, roles, parameters, range)
			fr->isOpen = [this, module]() {
				return module && (module->menu.open || module->slotMenu.open || (display && display->screen != NestDisplay::MAIN));
			};
			fr->box.pos = mm2px(a.minus(math::Vec(1.f, 2.f)));
			fr->box.size = mm2px(b.minus(a).plus(math::Vec(2.f, 3.f)));
			fr->frame = math::Rect(mm2px(math::Vec(1.f, 2.f)), mm2px(b.minus(a)));
			addChild(fr);
		}
		label(math::Vec(61.f, mid - 6.f), "OPEN UI", 6.f);
		addParam(createParamCentered<VCVButton>(mm2px(math::Vec(61.f, mid + 1.f)), module, Nest::OPEN_UI_PARAM));
		label(math::Vec(96.f, mid - 6.f), "BYPASS", 6.f);
		addParam(createLightParamCentered<BypassButton>(mm2px(math::Vec(96.f, mid + 1.f)), module, Nest::BYPASS_PARAM, Nest::BYPASS_LIGHT));

		// CV DEPTH 1-8
		section(3.f, 76.f, kW - 6.f, 41.f, "CV DEPTH");
		for (int k = 0; k < nest::kSlots; ++k) {
			const math::Vec c(21.f + (k % 4) * 26.6f, k < 4 ? 89.f : 107.f);
			label(c.plus(math::Vec(0.f, 7.6f)), string::f("%d", k + 1).c_str(), 6.f);
			knob(c, Nest::DEPTH_PARAM + k, 7.5f, st::CAP_WHITE, -1.f, 1.f, st::bipolar());
		}

		// Back: IN 1-12, OUT 1-12
		for (int k = 0; k < nest::kJacks; ++k) {
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(jackX(k), kInY[k / 6])), module, Nest::IN_INPUT + k));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(jackX(k), kOutY[k / 6])), module, Nest::OUT_OUTPUT + k));
		}
	}

	void drawBackLegends(NVGcontext* const vg) override {
		namespace st = jxmp::style;
		const NVGcolor colour = decor->stripe;
		Nest* const m = dynamic_cast<Nest*>(module);
		auto heading = [&](float y, const char* text) {
			st::text(vg, mm2px(math::Vec(4.f, y)), text, 7.f, colour, st::FONT_LABEL, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.8f);
			nvgBeginPath(vg);
			nvgMoveTo(vg, mm2px(4.f + std::strlen(text) * 1.45f + 1.5f), mm2px(y));
			nvgLineTo(vg, mm2px(kW - 4.f), mm2px(y));
			nvgStrokeWidth(vg, 0.8f);
			nvgStrokeColor(vg, st::line());
			nvgStroke(vg);
		};
		heading(27.f, "IN");
		heading(68.f, "OUT");
		for (int k = 0; k < nest::kJacks; ++k) {
			for (int side = 0; side < 2; ++side) {
				const float x = jackX(k), y = side ? kOutY[k / 6] : kInY[k / 6];
				if (side) {
					const math::Rect field(mm2px(math::Vec(x - 4.6f, y - 9.2f)), mm2px(math::Vec(9.2f, 16.f)));
					nvgBeginPath(vg);
					nvgRoundedRect(vg, RECT_ARGS(field), 2.f);
					nvgFillColor(vg, nvgRGB(0xc9, 0xcb, 0xcf));
					nvgFill(vg);
				}
				const NVGcolor ink = side ? nvgRGB(0x26, 0x28, 0x2c) : st::legend();
				st::text(vg, mm2px(math::Vec(x, y - 6.f)), string::f("%d", k + 1), 7.f, side ? ink : colour, st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.3f);
				if (m) {
					const nest::Role& r = side ? m->io.out[k] : m->io.in[k];
					std::string l = nest::jackLabel(r);
					if (!l.empty() && m->hasPlugin && !nest::roleValid(r, !side, m->ports))
						l = "\xC3\x97" + l;
					st::text(vg, mm2px(math::Vec(x, y + 5.6f)), l, 6.f, ink, st::FONT_LABEL, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.2f);
				}
			}
		}
	}

	void step() override {
		JXMPModuleWidget::step();
		Nest* const m = dynamic_cast<Nest*>(module);
		if (!m)
			return;
		Library& lib = Library::get();
		lib.idle();
		m->uiIdle();
		// A dropped plugin, once the scan that searched its folder is done
		if (!dropFile.empty() && lib.scans >= dropAfter) {
			const std::string want = nest::folderKey(dropFile, kWindows);
			int at = -1;
			for (size_t k = 0; k < lib.entries.size() && at < 0; ++k)
				if (lib.entries[k].format == dropFormat && nest::folderKey(lib.entries[k].file, kWindows) == want)
					at = (int) k;
			if (at >= 0) {
				const nest::PluginEntry e = lib.entries[at];
				bool ok = false;
				withUndo(m, "JX-NEST load plugin", [&]() { ok = m->loadPlugin(e); }, true);
				if (display)
					display->say(ok ? "LOADED" : "DID NOT LOAD");
			}
			else {
				WARN("JX-NEST: %s was not found by the scanner", dropFile.c_str());
				if (display)
					display->say("NOT A PLUGIN JX-NEST CAN HOST");
			}
			dropFile.clear();
		}
		// A missing plugin: after every scan, look whether it is here now (installed + RESCAN)
		if (m->missingJ && lib.scans != scansSeen && !lib.scanning) {
			scansSeen = lib.scans;
			if (m->missingFound() >= 0) {
				json_t* j = m->dataToJson();
				m->dataFromJson(j);
				json_decref(j);
				if (display)
					display->say(m->missingJ ? "DID NOT LOAD" : "PLUGIN FOUND, LOADED");
			}
		}
		const bool held = m->params[Nest::OPEN_UI_PARAM].getValue() > 0.5f;
		if (held && !openHeld && m->hasPlugin && m->host) {
			const bool show = !m->uiShown;
			carla_show_custom_ui(m->host, 0, show);
			m->uiShown = show;
		}
		openHeld = held;
	}

	void onPathDrop(const PathDropEvent& e) override {
		Nest* const m = dynamic_cast<Nest*>(module);
		if (!m || e.paths.empty())
			return JXMPModuleWidget::onPathDrop(e);
		const std::string path = e.paths[0];
		nest::Format f;
		Library& lib = Library::get();
		lib.ensureLoaded();
		if (nest::isPluginPath(path, &f)) {
			const int after = lib.rescan(system::getDirectory(path));
			if (after < 0) {
				if (display)
					display->say(lib.error);
			}
			else {
				dropFile = path;
				dropFormat = f;
				dropAfter = after;
				if (display)
					display->say("SCANNING THE DROPPED PLUGIN...");
			}
			e.consume(this);
			return;
		}
		if (system::isDirectory(path)) {
			lib.addFolder(path);
			if (display) {
				display->say("FOLDER ADDED, SCANNING");
				display->screen = NestDisplay::BROWSE;
			}
			e.consume(this);
			return;
		}
		JXMPModuleWidget::onPathDrop(e);
	}
};

} // namespace

Model* modelJXMPNest = createModel<Nest, NestWidget>("Nest");

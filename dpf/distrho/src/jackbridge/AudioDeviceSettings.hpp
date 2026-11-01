/*
 * PatchCore: audio device settings for the standalone — data, config file, fallback decisions.
 * No DPF or Rack dependency, tested in PatchCore src/patchcore/tests/test_audio_settings.cpp.
 * SPDX-License-Identifier: ISC (as DPF)
 */
#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace patchcore_audio {

enum class Driver { Jack, Alsa, Wasapi, Asio };

inline const char* driverKey(Driver d) {
	switch (d) {
		case Driver::Jack: return "jack";
		case Driver::Alsa: return "alsa";
		case Driver::Wasapi: return "wasapi";
		case Driver::Asio: return "asio";
	}
	return "jack";
}

inline const char* driverName(Driver d) {
	switch (d) {
		case Driver::Jack: return "PipeWire / JACK";
		case Driver::Alsa: return "ALSA";
		case Driver::Wasapi: return "WASAPI";
		case Driver::Asio: return "ASIO";
	}
	return "";
}

inline std::vector<Driver> platformDrivers() {
#ifdef _WIN32
	return {Driver::Wasapi, Driver::Asio};
#else
	return {Driver::Jack, Driver::Alsa};
#endif
}

inline bool driverFromKey(const std::string& key, Driver* out) {
	for (Driver d : platformDrivers()) {
		if (key == driverKey(d)) {
			*out = d;
			return true;
		}
	}
	return false;
}

/** What to open. `output` empty = the system default output; `input` empty = no audio input. */
struct AudioSetup {
	Driver driver;
	std::string output;
	std::string input;
	unsigned sampleRate;
	unsigned bufferSize;
};

inline AudioSetup defaultSetup() {
	return AudioSetup{platformDrivers()[0], "", "", 48000, 256};
}

struct DeviceInfo {
	std::string name;
	unsigned outputs;
	unsigned inputs;
	std::vector<unsigned> sampleRates;
};

/** The setup to open, and a message for the user when it differs from the saved one. */
struct Choice {
	AudioSetup setup;
	std::string notice;
};

static const unsigned kBufferSizes[6] = {64, 128, 256, 512, 1024, 2048};

inline unsigned nearest(const std::vector<unsigned>& options, unsigned wanted) {
	if (options.empty())
		return wanted;
	unsigned best = options[0];
	for (unsigned o : options) {
		const unsigned dBest = best > wanted ? best - wanted : wanted - best;
		const unsigned dO = o > wanted ? o - wanted : wanted - o;
		if (dO < dBest || (dO == dBest && o > best))
			best = o;
	}
	return best;
}

inline const DeviceInfo* findDevice(const std::vector<DeviceInfo>& devices, const std::string& name) {
	for (const DeviceInfo& d : devices)
		if (d.name == name)
			return &d;
	return nullptr;
}

inline Choice chooseSetup(const AudioSetup& saved, const std::vector<DeviceInfo>& devices) {
	Choice c{saved, ""};
	c.setup.bufferSize = nearest(std::vector<unsigned>(kBufferSizes, kBufferSizes + 6), saved.bufferSize);
	// PipeWire/JACK: the server owns the device and may keep its own rate
	if (saved.driver == Driver::Jack)
		return c;

	const DeviceInfo* out = saved.output.empty() ? nullptr : findDevice(devices, saved.output);
	if (!saved.output.empty() && out == nullptr) {
		c.notice = "Sound card " + saved.output + " not found, using the default output";
		c.setup.output.clear();
	}
	if (!saved.input.empty() && findDevice(devices, saved.input) == nullptr) {
		if (c.notice.empty())
			c.notice = "Input " + saved.input + " not found, audio input is off";
		c.setup.input.clear();
	}
	if (out != nullptr)
		c.setup.sampleRate = nearest(out->sampleRates, saved.sampleRate);
	return c;
}

inline std::string serialize(const AudioSetup& s) {
	std::ostringstream o;
	o << "driver=" << driverKey(s.driver) << "\n"
	  << "output=" << s.output << "\n"
	  << "input=" << s.input << "\n"
	  << "sampleRate=" << s.sampleRate << "\n"
	  << "bufferSize=" << s.bufferSize << "\n";
	return o.str();
}

inline bool parseUnsigned(const std::string& v, unsigned lo, unsigned hi, unsigned* out) {
	if (v.empty() || v.size() > 7)
		return false;
	unsigned n = 0;
	for (char ch : v) {
		if (ch < '0' || ch > '9')
			return false;
		n = n * 10 + (unsigned) (ch - '0');
	}
	if (n < lo || n > hi)
		return false;
	*out = n;
	return true;
}

/** Unknown keys and bad values are ignored (the default stays). */
inline AudioSetup parse(const std::string& text) {
	AudioSetup s = defaultSetup();
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
		if (k == "driver")
			driverFromKey(v, &s.driver);
		else if (k == "output")
			s.output = v;
		else if (k == "input")
			s.input = v;
		else if (k == "sampleRate")
			parseUnsigned(v, 8000, 384000, &s.sampleRate);
		else if (k == "bufferSize")
			parseUnsigned(v, 16, 8192, &s.bufferSize);
	}
	return s;
}

/** Next to Cardinal's own settings (the folder rack::asset::config uses), overridable for tests. */
inline std::string configPath() {
	if (const char* const p = std::getenv("PATCHCORE_AUDIO_CONFIG"))
		return p;
#ifdef _WIN32
	// DPF puts the config folder in Documents on Windows
	const char* const profile = std::getenv("USERPROFILE");
	return std::string(profile ? profile : ".") + "\\Documents\\Cardinal\\PatchCoreAudio.conf";
#else
	if (const char* const xdg = std::getenv("XDG_CONFIG_HOME"))
		return std::string(xdg) + "/Cardinal/PatchCoreAudio.conf";
	const char* const home = std::getenv("HOME");
	return std::string(home ? home : ".") + "/.config/Cardinal/PatchCoreAudio.conf";
#endif
}

inline AudioSetup loadSetup() {
	std::ifstream f(configPath().c_str());
	if (!f)
		return defaultSetup();
	std::stringstream ss;
	ss << f.rdbuf();
	return parse(ss.str());
}

inline bool saveSetup(const AudioSetup& s) {
	std::ofstream f(configPath().c_str(), std::ios::trunc);
	if (!f)
		return false;
	f << serialize(s);
	return (bool) f;
}

inline double latencyMs(const AudioSetup& s) {
	return s.sampleRate > 0 ? 1000.0 * s.bufferSize / s.sampleRate : 0.0;
}

#ifdef PATCHCORE_AUDIO_GLOBALS
/** The setup the standalone opened (or tries to), and a message for the user ("" if none); JackBridge.cpp. */
extern AudioSetup gPatchCoreSetup;
extern std::string gPatchCoreNotice;
#endif

} // namespace patchcore_audio

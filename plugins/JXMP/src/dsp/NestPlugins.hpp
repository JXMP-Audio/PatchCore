/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-NEST plugin library, no Rack or Carla dependency: the standard plugin folders of Linux and Windows (the only
 * places searched, plus folders the user adds), the cache line format, the ALL / INSTRUMENTS / EFFECTS lists.
 */

#pragma once

#include "SampleBrowser.hpp"  // joinPath, isSep

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace jxmp {
namespace nest {

enum Format { F_VST2, F_VST3, F_CLAP, F_LV2 };

inline const char* formatName(Format f) {
	switch (f) {
		case F_VST2: return "VST2";
		case F_VST3: return "VST3";
		case F_CLAP: return "CLAP";
		default: return "LV2";
	}
}

struct Folder {
	Format format;
	std::string path;
	Folder(Format f, const std::string& p) : format(f), path(p) {}
};

/** windows: VST3 / CLAP under Common Files and the per-user Programs\Common; Linux: the home and system folders.
 * An empty home / commonFiles / localAppData leaves its folders out. */
inline std::vector<Folder> standardFolders(bool windows, const std::string& home, const std::string& commonFiles,
                                           const std::string& localAppData) {
	using sampler::joinPath;
	std::vector<Folder> f;
	if (windows) {
		const std::string local = localAppData.empty() ? "" : joinPath(joinPath(localAppData, "Programs"), "Common");
		if (!commonFiles.empty())
			f.push_back(Folder(F_VST3, joinPath(commonFiles, "VST3")));
		if (!local.empty())
			f.push_back(Folder(F_VST3, joinPath(local, "VST3")));
		if (!commonFiles.empty())
			f.push_back(Folder(F_CLAP, joinPath(commonFiles, "CLAP")));
		if (!local.empty())
			f.push_back(Folder(F_CLAP, joinPath(local, "CLAP")));
		return f;
	}
	if (!home.empty())
		f.push_back(Folder(F_VST3, joinPath(home, ".vst3")));
	f.push_back(Folder(F_VST3, "/usr/lib/vst3"));
	f.push_back(Folder(F_VST3, "/usr/local/lib/vst3"));
	if (!home.empty())
		f.push_back(Folder(F_CLAP, joinPath(home, ".clap")));
	f.push_back(Folder(F_CLAP, "/usr/lib/clap"));
	if (!home.empty())
		f.push_back(Folder(F_LV2, joinPath(home, ".lv2")));
	f.push_back(Folder(F_LV2, "/usr/lib/lv2"));
	f.push_back(Folder(F_LV2, "/usr/local/lib/lv2"));
	if (!home.empty())
		f.push_back(Folder(F_VST2, joinPath(home, ".vst")));
	f.push_back(Folder(F_VST2, "/usr/lib/vst"));
	return f;
}

inline std::string folderKey(std::string p, bool windows) {
	while (p.size() > 1 && sampler::isSep(p.back()) && !sampler::isRootPath(p))
		p.pop_back();
	if (windows)
		for (char& c : p)
			c = c == '/' ? '\\' : (char) std::tolower((unsigned char) c);
	return p;
}

/** Every user folder is searched for every format; the same folder (trailing separator, case on Windows) once */
inline std::vector<Folder> withUserFolders(std::vector<Folder> f, const std::vector<std::string>& user, bool windows) {
	for (const std::string& u : user) {
		if (u.empty())
			continue;
		for (Format fm : {F_VST2, F_VST3, F_CLAP, F_LV2}) {
			const std::string key = folderKey(u, windows);
			bool dup = false;
			for (const Folder& x : f)
				dup |= x.format == fm && folderKey(x.path, windows) == key;
			if (!dup)
				f.push_back(Folder(fm, folderKey(u, false)));
		}
	}
	return f;
}

struct PluginEntry {
	Format format = F_VST3;
	std::string id, name, maker, file, label;
	bool instrument = false;
	int ins = 0, outs = 0;
	bool midiIn = false;
};

inline std::string noTabs(std::string s) {
	for (char& c : s)
		if (c == '\t' || c == '\n' || c == '\r')
			c = ' ';
	return s;
}

/** "1 <tab> format <tab> id <tab> name <tab> maker <tab> file <tab> label <tab> instrument <tab> ins <tab> outs <tab> midiIn" */
inline std::string cacheLine(const PluginEntry& e) {
	const std::string t = "\t";
	return "1" + t + formatName(e.format) + t + noTabs(e.id) + t + noTabs(e.name) + t + noTabs(e.maker) + t +
	       noTabs(e.file) + t + noTabs(e.label) + t + (e.instrument ? "1" : "0") + t + std::to_string(e.ins) + t +
	       std::to_string(e.outs) + t + (e.midiIn ? "1" : "0");
}

inline bool parseCacheLine(const std::string& line, PluginEntry& e) {
	std::vector<std::string> f;
	size_t a = 0;
	for (;;) {
		const size_t b = line.find('\t', a);
		f.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
		if (b == std::string::npos)
			break;
		a = b + 1;
	}
	if (f.size() != 11 || f[0] != "1" || f[2].empty() || f[5].empty())
		return false;
	PluginEntry n;
	if (f[1] == "VST2")
		n.format = F_VST2;
	else if (f[1] == "VST3")
		n.format = F_VST3;
	else if (f[1] == "CLAP")
		n.format = F_CLAP;
	else if (f[1] == "LV2")
		n.format = F_LV2;
	else
		return false;
	n.id = f[2];
	n.name = f[3];
	n.maker = f[4];
	n.file = f[5];
	n.label = f[6];
	n.instrument = f[7] == "1";
	n.ins = std::atoi(f[8].c_str());
	n.outs = std::atoi(f[9].c_str());
	n.midiIn = f[10] == "1";
	e = n;
	return true;
}

enum ListTab { T_ALL, T_INSTRUMENTS, T_EFFECTS };

/** Indices into v for one tab, A-Z by name ignoring case, then by format */
inline std::vector<int> listFor(const std::vector<PluginEntry>& v, ListTab tab) {
	std::vector<int> r;
	for (int k = 0; k < (int) v.size(); ++k)
		if (tab == T_ALL || (tab == T_INSTRUMENTS) == v[k].instrument)
			r.push_back(k);
	auto lower = [](std::string s) {
		for (char& c : s)
			c = (char) std::tolower((unsigned char) c);
		return s;
	};
	std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
		const std::string la = lower(v[a].name), lb = lower(v[b].name);
		return la != lb ? la < lb : v[a].format < v[b].format;
	});
	return r;
}

inline int findById(const std::vector<PluginEntry>& v, Format f, const std::string& id) {
	for (int k = 0; k < (int) v.size(); ++k)
		if (v[k].format == f && v[k].id == id)
			return k;
	return -1;
}

/** LV2 discovery names only the bundle ("x.lv2"): the full path is the first of `paths` (joined with ':', ';' on
 * Windows) that has it; a name that is already a path, or is found nowhere, stays as it is */
inline std::string resolveBundle(const std::string& name, const std::string& paths, bool windows,
                                 const std::function<bool(const std::string&)>& exists) {
	if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
		return name;
	const char sep = windows ? ';' : ':';
	size_t a = 0;
	while (a <= paths.size()) {
		size_t b = paths.find(sep, a);
		if (b == std::string::npos)
			b = paths.size();
		if (b > a) {
			const std::string full = sampler::joinPath(paths.substr(a, b - a), name);
			if (exists(full))
				return full;
		}
		a = b + 1;
	}
	return name;
}

/** One entry per plugin (format + id): the first found wins (standard folders are searched first) */
inline std::vector<PluginEntry> uniqueEntries(const std::vector<PluginEntry>& v) {
	std::vector<PluginEntry> out;
	for (const PluginEntry& e : v)
		if (findById(out, e.format, e.id) < 0)
			out.push_back(e);
	return out;
}

/** PatchCore's own bundles (PatchCore*, Cardinal*): never offered, PatchCore inside PatchCore shares its state */
inline bool isOwnBundle(std::string path) {
	while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
		path.pop_back();
	const size_t slash = path.find_last_of("/\\");
	const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
	return base.compare(0, 9, "PatchCore") == 0 || base.compare(0, 8, "Cardinal") == 0;
}

/** A plugin file or bundle by its extension (".vst3", ".clap", ".lv2"; ".so" / ".dll" = VST2) */
inline bool isPluginPath(std::string p, Format* out) {
	while (p.size() > 1 && sampler::isSep(p.back()))
		p.pop_back();
	const size_t dot = p.find_last_of('.'), sep = p.find_last_of("/\\");
	if (dot == std::string::npos || (sep != std::string::npos && dot < sep))
		return false;
	std::string ext = p.substr(dot + 1);
	for (char& c : ext)
		c = (char) std::tolower((unsigned char) c);
	Format f;
	if (ext == "vst3")
		f = F_VST3;
	else if (ext == "clap")
		f = F_CLAP;
	else if (ext == "lv2")
		f = F_LV2;
	else if (ext == "so" || ext == "dll")
		f = F_VST2;
	else
		return false;
	if (out)
		*out = f;
	return true;
}

} // namespace nest
} // namespace jxmp

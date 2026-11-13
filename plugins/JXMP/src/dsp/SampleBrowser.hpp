/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL browser (SAMPLE > BROWSE), no Rack dependency: the list of one folder (".." first unless at the root, then
 * the folders, then the sound files, each A-Z ignoring case; hidden entries and other files left out), the selection
 * and the first row shown, and the path helpers for "/" and Windows "C:\" paths.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace jxmp {
namespace sampler {

/** A name the browser lists as a sound: .wav .flac .mp3 .ogg .oga, any case */
inline bool isAudioName(const std::string& name) {
	const size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || dot == 0)
		return false;
	std::string ext = name.substr(dot + 1);
	for (char& c : ext)
		c = (char) std::tolower((unsigned char) c);
	return ext == "wav" || ext == "flac" || ext == "mp3" || ext == "ogg" || ext == "oga";
}

inline std::string stripExtension(const std::string& name) {
	const size_t dot = name.find_last_of('.');
	return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

inline bool isSep(char c) { return c == '/' || c == '\\'; }

/** "/" or "C:\" (also "C:/") */
inline bool isRootPath(const std::string& p) {
	return p == "/" || p == "\\" || (p.size() == 3 && p[1] == ':' && isSep(p[2])) || (p.size() == 2 && p[1] == ':');
}

inline std::string parentDir(std::string p) {
	while (p.size() > 1 && isSep(p.back()) && !isRootPath(p))
		p.pop_back();
	if (isRootPath(p))
		return p;
	const size_t s = p.find_last_of("/\\");
	if (s == std::string::npos)
		return p;
	std::string up = p.substr(0, s);
	if (up.empty())
		return std::string(1, p[s]);  // "/home" -> "/"
	if (up.size() == 2 && up[1] == ':')
		return up + p[s];  // "C:\Users" -> "C:\"
	return up;
}

inline std::string joinPath(const std::string& dir, const std::string& name) {
	if (dir.empty())
		return name;
	if (isSep(dir.back()))
		return dir + name;
	return dir + (dir.find('\\') != std::string::npos && dir.find('/') == std::string::npos ? "\\" : "/") + name;
}

/** The last folder of a path ("Samples" of "/home/me/Samples"); the path itself at the root */
inline std::string lastPart(std::string p) {
	while (p.size() > 1 && isSep(p.back()) && !isRootPath(p))
		p.pop_back();
	if (isRootPath(p))
		return p;
	const size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? p : p.substr(s + 1);
}

/** At most `max` characters (UTF-8, never half of one): the start and the end with "…" between (the end often holds
 * the number or note) */
inline std::string shortenMiddle(const std::string& s, size_t max) {
	// Where every character starts
	std::vector<size_t> at;
	for (size_t k = 0; k < s.size(); ++k)
		if (((unsigned char) s[k] & 0xC0) != 0x80)
			at.push_back(k);
	if (at.size() <= max || max < 5)
		return s;
	const size_t head = (max - 1) * 55 / 100, tail = max - 1 - head;
	return s.substr(0, at[head]) + "\xE2\x80\xA6" + s.substr(at[at.size() - tail]);
}

struct BrowserEntry {
	std::string name;
	bool dir = false;
	BrowserEntry() {}
	BrowserEntry(const std::string& n, bool d) : name(n), dir(d) {}
};

struct Browser {
	std::string path;
	std::vector<BrowserEntry> items;
	int sel = 0, top = 0;

	/** Lists `path` from its raw entries; the selection goes to `select` (a name) when it is there */
	void open(const std::string& p, std::vector<BrowserEntry> raw, const std::string& select = std::string()) {
		path = p;
		items.clear();
		raw.erase(std::remove_if(raw.begin(), raw.end(),
		                         [](const BrowserEntry& e) {
			                         return e.name.empty() || e.name[0] == '.' || (!e.dir && !isAudioName(e.name));
		                         }),
		          raw.end());
		auto lower = [](std::string s) {
			for (char& c : s)
				c = (char) std::tolower((unsigned char) c);
			return s;
		};
		std::sort(raw.begin(), raw.end(), [&](const BrowserEntry& a, const BrowserEntry& b) {
			if (a.dir != b.dir)
				return a.dir;
			const std::string la = lower(a.name), lb = lower(b.name);
			return la != lb ? la < lb : a.name < b.name;
		});
		if (!isRootPath(p))
			items.push_back(BrowserEntry("..", true));
		items.insert(items.end(), raw.begin(), raw.end());
		sel = top = 0;
		for (size_t k = 0; k < items.size(); ++k)
			if (!select.empty() && items[k].name == select)
				sel = (int) k;
	}

	const BrowserEntry* selected() const { return sel >= 0 && sel < (int) items.size() ? &items[sel] : nullptr; }

	/** Keeps the selection inside the `rows` shown */
	void follow(int rows) {
		if (sel < top)
			top = sel;
		if (sel >= top + rows)
			top = sel - rows + 1;
		top = std::max(0, std::min(top, std::max(0, (int) items.size() - rows)));
	}
	/** ▲ ▼: one row up or down, round the end */
	void move(int d, int rows) {
		const int n = (int) items.size();
		if (n == 0)
			return;
		sel = ((sel + d) % n + n) % n;
		follow(rows);
	}
	/** A click on row i */
	void select(int i, int rows) {
		if (i < 0 || i >= (int) items.size())
			return;
		sel = i;
		follow(rows);
	}
};

} // namespace sampler
} // namespace jxmp

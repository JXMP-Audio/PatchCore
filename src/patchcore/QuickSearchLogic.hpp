/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Quick search logic: query syntax (text @brand #purpose), filters and the order of the empty-query list.
 * No Rack dependency, so it can be tested alone.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace rack {
namespace patchcore {
namespace qs {

/** Lower case, letters and digits only: "JX-MP Audio" -> "jxmpaudio". */
inline std::string normalize(const std::string& s) {
	std::string out;
	for (const char ch : s) {
		const unsigned char c = (unsigned char) ch;
		if (std::isalnum(c))
			out += (char) std::tolower(c);
	}
	return out;
}

inline std::string trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char) s[a]))
		++a;
	while (b > a && std::isspace((unsigned char) s[b - 1]))
		--b;
	return s.substr(a, b - a);
}

struct Query {
	std::string text;   // for the fuzzy search, as typed (trimmed)
	std::string brand;  // normalised, empty = any
	std::string tag;    // normalised, empty = any
};

/** "VCO@JXMP", "filter #effect @acme": everything after '@' up to the next marker is the brand,
after '#' the purpose; the rest is the text. */
inline Query parseQuery(const std::string& s) {
	Query q;
	std::string text, brand, tag;
	std::string* target = &text;
	for (const char c : s) {
		if (c == '@')
			target = &brand;
		else if (c == '#')
			target = &tag;
		else
			*target += c;
	}
	q.text = trim(text);
	q.brand = normalize(brand);
	q.tag = normalize(tag);
	return q;
}

struct Entry {
	std::string brand;
	std::vector<std::string> tags;  // tag names and aliases
	bool favorite;
	double lastAdded;  // 0 = never added
	bool ours;         // a JXMP module
};

/** Both filters are optional and match a part of the normalised brand / of any tag. */
inline bool passes(const Entry& e, const std::string& tagNeedle, const std::string& brandNeedle) {
	if (!brandNeedle.empty() && normalize(e.brand).find(brandNeedle) == std::string::npos)
		return false;
	if (!tagNeedle.empty()) {
		bool found = false;
		for (const std::string& t : e.tags)
			found = found || normalize(t).find(tagNeedle) != std::string::npos;
		if (!found)
			return false;
	}
	return true;
}

enum Section { SECTION_RECENT, SECTION_FAVORITES, SECTION_OURS, SECTION_ALL };

struct Item {
	int index;
	Section section;
};

/** List for an empty text: recent (newest first, at most `maxRecent`), favorites, our modules. With a filter
set, every other matching module follows (SECTION_ALL). Nothing appears twice. */
inline std::vector<Item> emptyQueryOrder(const std::vector<Entry>& entries, const std::string& tagNeedle,
                                         const std::string& brandNeedle, size_t maxRecent) {
	std::vector<Item> out;
	std::vector<bool> used(entries.size(), false);

	std::vector<int> recent;
	for (int i = 0; i < (int) entries.size(); ++i)
		if (entries[i].lastAdded > 0.0 && passes(entries[i], tagNeedle, brandNeedle))
			recent.push_back(i);
	std::sort(recent.begin(), recent.end(), [&](int a, int b) { return entries[a].lastAdded > entries[b].lastAdded; });
	for (size_t k = 0; k < recent.size() && k < maxRecent; ++k) {
		out.push_back({recent[k], SECTION_RECENT});
		used[recent[k]] = true;
	}

	for (int i = 0; i < (int) entries.size(); ++i)
		if (!used[i] && entries[i].favorite && passes(entries[i], tagNeedle, brandNeedle)) {
			out.push_back({i, SECTION_FAVORITES});
			used[i] = true;
		}
	for (int i = 0; i < (int) entries.size(); ++i)
		if (!used[i] && entries[i].ours && passes(entries[i], tagNeedle, brandNeedle)) {
			out.push_back({i, SECTION_OURS});
			used[i] = true;
		}

	if (!tagNeedle.empty() || !brandNeedle.empty()) {
		for (int i = 0; i < (int) entries.size(); ++i)
			if (!used[i] && passes(entries[i], tagNeedle, brandNeedle))
				out.push_back({i, SECTION_ALL});
	}
	return out;
}

} // namespace qs
} // namespace patchcore
} // namespace rack

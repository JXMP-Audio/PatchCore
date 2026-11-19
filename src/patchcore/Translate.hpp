/*
 * PatchCore: translations of the PatchCore UI.
 *
 * A language is a file <code>.json in the JXMP plugin's res/lang: {"name": "Čeština", "strings": {"English text":
 * "translation", ...}}. Texts are looked up by their English original (a missing one stays English); "%s" marks
 * where a value goes. English is built in and has no file. Pure logic, no Rack: unit-tested without the app.
 */

#pragma once

#include <jansson.h>

#include <map>
#include <string>
#include <vector>

namespace patchcore_lang {

/** The strings of the chosen language (empty: English) */
inline std::map<std::string, std::string>& current() {
	static std::map<std::string, std::string> strings;
	return strings;
}

inline std::string tr(const std::string& en) {
	const std::map<std::string, std::string>& m = current();
	const std::map<std::string, std::string>::const_iterator it = m.find(en);
	return it == m.end() || it->second.empty() ? en : it->second;
}

/** tr(), then every "%s" takes the next value */
inline std::string trf(const std::string& en, const std::vector<std::string>& values) {
	const std::string t = tr(en);
	std::string out;
	size_t next = 0;
	for (size_t i = 0; i < t.size(); ++i) {
		if (t[i] == '%' && i + 1 < t.size() && t[i + 1] == 's') {
			out += next < values.size() ? values[next] : "";
			++next;
			++i;
		}
		else
			out += t[i];
	}
	return out;
}

/** Upper case for ASCII and the Latin letters of U+00E0..U+017F (Czech and most European languages); other
characters are kept */
inline std::string upper(const std::string& in) {
	std::string out;
	for (size_t i = 0; i < in.size(); ++i) {
		const unsigned char c = (unsigned char) in[i];
		if (c < 0x80) {
			out += (char) (c >= 'a' && c <= 'z' ? c - 32 : c);
			continue;
		}
		if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) {
			unsigned cp = ((c & 0x1Fu) << 6) | ((unsigned char) in[i + 1] & 0x3Fu);
			if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
				cp -= 0x20;
			else if (cp >= 0x100 && cp <= 0x17F) {
				// Pairs: upper case even, lower odd; but odd upper / even lower in 0x139..0x148 and 0x179..0x17E
				const bool shifted = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
				if (shifted ? cp % 2 == 0 : cp % 2 == 1)
					cp -= 1;
			}
			out += (char) (0xC0 | (cp >> 6));
			out += (char) (0x80 | (cp & 0x3F));
			++i;
			continue;
		}
		out += (char) c;
	}
	return out;
}

inline bool parseLanguage(const std::string& text, std::string* name, std::map<std::string, std::string>* out,
                          std::string* error) {
	json_error_t jerr;
	json_t* const root = json_loads(text.c_str(), 0, &jerr);
	if (!root) {
		*error = std::string("not valid JSON: ") + jerr.text;
		return false;
	}
	json_t* const n = json_object_get(root, "name");
	json_t* const strings = json_object_get(root, "strings");
	bool ok = json_is_string(n) && std::string(json_string_value(n)).size() > 0 && json_is_object(strings);
	if (!ok)
		*error = "a language needs \"name\" and \"strings\"";
	else {
		*name = json_string_value(n);
		out->clear();
		const char* key;
		json_t* value;
		json_object_foreach(strings, key, value) {
			if (json_is_string(value))
				(*out)[key] = json_string_value(value);
		}
	}
	json_decref(root);
	return ok;
}

} // namespace patchcore_lang

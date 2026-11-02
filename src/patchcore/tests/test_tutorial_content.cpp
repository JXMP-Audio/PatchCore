// Checks every lesson in plugins/JXMP/res/tutorials against the JXMP module sources: module slugs, port and
// param names, and that every module a step needs is in start.vcv or added by an earlier step.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../TutorialLogic.hpp"

using namespace patchcore_tutorial;

static const std::string kRoot = PATCHCORE_ROOT;
static int failures = 0;

static std::string readFile(const std::string& path) {
	std::ifstream f(path.c_str());
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static void failf(const std::string& msg) {
	std::printf("FAIL: %s\n", msg.c_str());
	++failures;
}

/** Names a module source configures, by kind. "%d" in a name matches any number. */
struct Names {
	std::vector<std::string> inputs, outputs, params;
};

static const char* const kSources[] = {"Att", "Chr", "Clk", "Console", "Dly", "Drm", "Envelope", "Euc", "Fld", "Flt",
	"Lfo", "Lgc", "Mix", "Nse", "Osc", "Patchbay", "Ply", "Qnt", "Rev", "Rng", "Rnd", "Scp", "Seq", "Slw", "Sw", "Vca"};

/** The source whose createModel<...>("slug") line registers the model */
static std::string sourceFor(const std::string& slug) {
	const std::string call = ">(\"" + slug + "\");";
	for (const char* s : kSources) {
		const std::string text = readFile(kRoot + "/plugins/JXMP/src/" + s + ".cpp");
		std::istringstream lines(text);
		std::string line;
		while (std::getline(lines, line))
			if (line.find("createModel<") != std::string::npos && line.find(call) != std::string::npos)
				return text;
	}
	return "";
}

static Names namesIn(const std::string& src) {
	Names n;
	// `const char* names[...] = {"Kick", ...}` arrays: names built with "%s" (JX-DRM) or names[k] (JX-CLK)
	std::vector<std::string> arr;
	const std::regex arrRe("const char\\*\\s*names\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}");
	const std::regex lit("\"([^\"]*)\"");
	for (std::sregex_iterator a(src.begin(), src.end(), arrRe), end; a != end; ++a) {
		const std::string body = (*a)[1];
		for (std::sregex_iterator it(body.begin(), body.end(), lit), e2; it != e2; ++it)
			arr.push_back((*it)[1]);
	}
	// configParam(ID, min, max, default, "Name"...): min/max/default may be names (minFreq), so any three
	// arguments; configParam<DecayQuantity>(...) as well
	const std::regex call("config(Input|Output|Param|Switch|Button)(<[A-Za-z0-9_:]+>)?\\(\\s*[A-Za-z0-9_]+(\\s*\\+\\s*[A-Za-z0-9_]+)?\\s*,"
	                      "((?:[^,\"()]*,){3})?\\s*(string::f\\()?\"([^\"]*)\"");
	for (std::sregex_iterator it(src.begin(), src.end(), call), end; it != end; ++it) {
		const std::string kind = (*it)[1];
		const std::string name = (*it)[6];
		std::vector<std::string>& list = kind == "Input" ? n.inputs : kind == "Output" ? n.outputs : n.params;
		const size_t s = name.find("%s");
		if (s == std::string::npos)
			list.push_back(name);
		else
			for (const std::string& a : arr)
				list.push_back(name.substr(0, s) + a + name.substr(s + 2));
	}
	if (src.find("configOutput(DIV_OUTPUT + k, names[k])") != std::string::npos)
		n.outputs.insert(n.outputs.end(), arr.begin(), arr.end());
	return n;
}

static bool nameKnown(const std::vector<std::string>& names, const std::string& want) {
	if (want.size() > 1 && want[0] == '#')
		return true;
	for (const std::string& n : names) {
		std::string pattern;
		for (size_t i = 0; i < n.size(); ++i) {
			if (n.compare(i, 2, "%d") == 0) {
				pattern += "[0-9]+";
				++i;
			}
			else if (std::string("\\^$.|?*+()[]{}").find(n[i]) != std::string::npos)
				pattern += std::string("\\") + n[i];
			else
				pattern += n[i];
		}
		if (std::regex_match(want, std::regex(pattern)))
			return true;
	}
	return false;
}

static void checkRef(const Ref& r, const std::string& where) {
	if (r.module == "HostAudio2") {
		if (r.kind == Ref::PARAM ? r.name != "Level" : !(r.name.size() > 1 && r.name[0] == '#'))
			failf(where + ": HostAudio2 has only \"Level\" and numbered ports");
		return;
	}
	const std::string src = sourceFor(r.module);
	if (src.empty()) {
		failf(where + ": no JXMP module \"" + r.module + "\"");
		return;
	}
	if (r.kind == Ref::MODULE)
		return;
	const Names n = namesIn(src);
	const std::vector<std::string>& list = r.kind == Ref::INPUT ? n.inputs : r.kind == Ref::OUTPUT ? n.outputs : n.params;
	const char* const kind = r.kind == Ref::INPUT ? "input" : r.kind == Ref::OUTPUT ? "output" : "param";
	if (!nameKnown(list, r.name))
		failf(where + ": " + r.module + " has no " + kind + " '" + r.name + "'");
}

static void checkCondition(const Condition& c, const std::string& where) {
	if (c.type == Cond::Module || c.type == Cond::Param || c.type == Cond::Signal || (c.type == Cond::Cable && c.hasFrom))
		checkRef(c.ref, where);
	if (c.type == Cond::Cable && c.hasTo)
		checkRef(c.to, where);
	for (const Condition& p : c.parts)
		checkCondition(p, where);
}

/** Size of `enum InputIds {...}` (or OutputIds / ParamIds) of a module source; -1 when it cannot be read.
`templateN` stands for N in template modules (Patchbay4 → 4). */
static int enumCount(const std::string& src, const std::string& enumName, int templateN) {
	std::smatch m;
	if (!std::regex_search(src, m, std::regex("enum " + enumName + "\\s*\\{([^}]*)\\}")))
		return -1;
	const std::string body = m[1];
	int count = 0;
	const std::regex item("ENUMS\\(\\s*[A-Za-z0-9_]+\\s*,\\s*([A-Za-z0-9_:]+)\\s*\\)|([A-Za-z0-9_]+)");
	for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it) {
		const std::string plain = (*it)[2];
		if (!plain.empty()) {
			if (plain.compare(0, 4, "NUM_") == 0)
				break;
			++count;
			continue;
		}
		std::string size = (*it)[1];
		if (size.find("::") != std::string::npos)
			size = size.substr(size.rfind("::") + 2);
		if (std::regex_match(size, std::regex("[0-9]+"))) {
			count += std::atoi(size.c_str());
			continue;
		}
		if (size == "N" && templateN > 0) {
			count += templateN;
			continue;
		}
		std::smatch v;
		if (std::regex_search(src, v, std::regex("\\b" + size + "\\s*=\\s*([0-9]+)\\s*;"))) {
			count += std::atoi(std::string(v[1]).c_str());
			continue;
		}
		// NUM_VOICES at the end of `enum Voice { KICK, SNARE, HAT, CLAP, NUM_VOICES }`
		bool found = false;
		const std::regex anyEnum("enum\\s+[A-Za-z0-9_]+\\s*\\{([^}]*)\\}");
		for (std::sregex_iterator e(src.begin(), src.end(), anyEnum), eend; e != eend && !found; ++e) {
			const std::string eb = (*e)[1];
			// only plain lists (enum Voice {...}); a port enum with ENUMS(...) is not a list of names
			if (eb.find("ENUMS(") != std::string::npos)
				continue;
			int index = 0;
			const std::regex word("[A-Za-z0-9_]+");
			for (std::sregex_iterator w(eb.begin(), eb.end(), word), wend; w != wend; ++w, ++index) {
				if ((*w)[0] == size) {
					count += index;
					found = true;
					break;
				}
			}
		}
		if (!found)
			return -1;
	}
	return count;
}

/** Inputs, outputs and params of a model; -1 for a count that cannot be read */
struct Sizes {
	int inputs, outputs, params;
};

static Sizes sizesOf(const std::string& slug) {
	if (slug == "HostAudio2")
		return Sizes{2, 2, 1};
	const std::string src = sourceFor(slug);
	if (src.empty())
		return Sizes{-1, -1, -1};
	int n = 0;
	for (char ch : slug)
		if (ch >= '0' && ch <= '9')
			n = n * 10 + (ch - '0');
	return Sizes{enumCount(src, "InputIds", n), enumCount(src, "OutputIds", n), enumCount(src, "ParamIds", n)};
}

static void addedModules(const Condition& c, std::set<std::string>* out) {
	if (c.type == Cond::Module)
		out->insert(c.ref.module);
	for (const Condition& p : c.parts)
		addedModules(p, out);
}

int main() {
	const std::string dir = kRoot + "/plugins/JXMP/res/tutorials";
	std::vector<IndexEntry> index;
	std::string err;
	if (!parseIndex(readFile(dir + "/index.json"), &index, &err)) {
		failf("index.json: " + err);
		return 1;
	}
	if (index.size() != 6)
		failf("index.json: expected 6 lessons");
	int n = 0;
	for (const IndexEntry& e : index) {
		++n;
		const std::string lessonName = "lesson " + std::to_string(n);
		Lesson l;
		if (!parseLesson(readFile(dir + "/" + e.id + "/lesson.json"), &l, &err)) {
			failf(lessonName + " (" + e.id + "): " + err);
			continue;
		}
		// Modules in start.vcv
		json_error_t jerr;
		json_t* const patch = json_load_file((dir + "/" + e.id + "/start.vcv").c_str(), 0, &jerr);
		if (!patch) {
			failf(lessonName + ": start.vcv: " + jerr.text);
			continue;
		}
		std::set<std::string> available;
		std::map<long long, std::string> models;
		json_t* const mods = json_object_get(patch, "modules");
		for (size_t i = 0; i < json_array_size(mods); ++i) {
			json_t* const m = json_array_get(mods, i);
			const std::string model = json_string_value(json_object_get(m, "model"));
			available.insert(model);
			models[json_integer_value(json_object_get(m, "id"))] = model;
			// Param ids within the module's params
			const Sizes sz = sizesOf(model);
			json_t* const params = json_object_get(m, "params");
			for (size_t k = 0; k < json_array_size(params); ++k) {
				const long long pid = json_integer_value(json_object_get(json_array_get(params, k), "id"));
				if (sz.params >= 0 && (pid < 0 || pid >= sz.params))
					failf(lessonName + ": start.vcv: " + model + " has no param id " + std::to_string(pid));
			}
		}
		json_t* const cables = json_object_get(patch, "cables");
		for (size_t i = 0; i < json_array_size(cables); ++i) {
			json_t* const c = json_array_get(cables, i);
			const long long om = json_integer_value(json_object_get(c, "outputModuleId"));
			const long long im = json_integer_value(json_object_get(c, "inputModuleId"));
			if (!models.count(om) || !models.count(im)) {
				failf(lessonName + ": start.vcv: a cable points at a missing module");
				continue;
			}
			// Port ids within the modules' ports
			const long long oid = json_integer_value(json_object_get(c, "outputId"));
			const long long iid = json_integer_value(json_object_get(c, "inputId"));
			const int outs = sizesOf(models[om]).outputs, ins = sizesOf(models[im]).inputs;
			if (outs >= 0 && (oid < 0 || oid >= outs))
				failf(lessonName + ": start.vcv: " + models[om] + " has no output id " + std::to_string(oid));
			if (ins >= 0 && (iid < 0 || iid >= ins))
				failf(lessonName + ": start.vcv: " + models[im] + " has no input id " + std::to_string(iid));
		}
		json_decref(patch);
		if (l.steps.back().done.type != Cond::Next)
			failf(lessonName + ": the last step must be a summary (next)");
		for (size_t i = 0; i < l.steps.size(); ++i) {
			const Step& s = l.steps[i];
			const std::string where = lessonName + " step " + std::to_string(i + 1);
			for (const Ref& r : s.highlight)
				checkRef(r, where);
			checkCondition(s.done, where);
			addedModules(s.done, &available);
			std::vector<std::string> needed;
			for (const Ref& r : s.highlight)
				needed.push_back(r.module);
			collectModules(s.done, &needed);
			for (const std::string& m : needed)
				if (!available.count(m))
					failf(where + ": " + m + " is not in start.vcv and no step adds it");
		}
	}
	if (failures) {
		std::printf("tutorial content: %d problem(s)\n", failures);
		return 1;
	}
	std::printf("tutorial content: all lessons check out\n");
	return 0;
}

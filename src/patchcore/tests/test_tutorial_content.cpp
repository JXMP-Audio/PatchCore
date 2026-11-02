// Checks every lesson in plugins/JXMP/res/tutorials against the JXMP module sources: module slugs, port and
// param names, and that every module a step needs is in start.vcv or added by an earlier step.
#include <cassert>
#include <cstdio>
#include <fstream>
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
	// configParam(ID, min, max, default, "Name"...): min/max/default may be names (minFreq), so any three arguments
	const std::regex call("config(Input|Output|Param|Switch|Button)\\(\\s*[A-Za-z0-9_]+(\\s*\\+\\s*[A-Za-z0-9_]+)?\\s*,"
	                      "((?:[^,\"()]*,){3})?\\s*(string::f\\()?\"([^\"]*)\"");
	for (std::sregex_iterator it(src.begin(), src.end(), call), end; it != end; ++it) {
		const std::string kind = (*it)[1];
		const std::string name = (*it)[5];
		(kind == "Input" ? n.inputs : kind == "Output" ? n.outputs : n.params).push_back(name);
	}
	// configOutput(X + k, names[k]) with `const char* names[...] = {"x4", ...}` (JX-CLK)
	const std::regex arr("const char\\*\\s*names\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}");
	std::smatch m;
	if (src.find("configOutput(DIV_OUTPUT + k, names[k])") != std::string::npos && std::regex_search(src, m, arr)) {
		const std::string body = m[1];
		const std::regex lit("\"([^\"]*)\"");
		for (std::sregex_iterator it(body.begin(), body.end(), lit), end; it != end; ++it)
			n.outputs.push_back((*it)[1]);
	}
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
	if (index.size() != 3)
		failf("index.json: expected 3 lessons");
	int n = 0;
	for (const IndexEntry& e : index) {
		++n;
		const std::string lessonName = "lesson " + std::to_string(n) + " (" + e.id + ")";
		Lesson l;
		if (!parseLesson(readFile(dir + "/" + e.id + "/lesson.json"), &l, &err)) {
			failf(lessonName + ": " + err);
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
		std::set<long long> ids;
		json_t* const mods = json_object_get(patch, "modules");
		for (size_t i = 0; i < json_array_size(mods); ++i) {
			json_t* const m = json_array_get(mods, i);
			available.insert(json_string_value(json_object_get(m, "model")));
			ids.insert(json_integer_value(json_object_get(m, "id")));
		}
		json_t* const cables = json_object_get(patch, "cables");
		for (size_t i = 0; i < json_array_size(cables); ++i) {
			json_t* const c = json_array_get(cables, i);
			if (!ids.count(json_integer_value(json_object_get(c, "outputModuleId"))) || !ids.count(json_integer_value(json_object_get(c, "inputModuleId"))))
				failf(lessonName + ": start.vcv: a cable points at a missing module");
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

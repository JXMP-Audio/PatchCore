/*
 * PatchCore: interactive tutorials — lesson data, parsing, rack snapshot, conditions, step runner.
 * No Rack dependency (jansson only); tested in src/patchcore/tests/test_tutorial.cpp.
 * Design: research_docs/15-tutorials-design.md
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <jansson.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace patchcore_tutorial {

enum Side { SIDE_ANY = -1, SIDE_FRONT = 0, SIDE_BACK = 1 };

/** A module, or one of its ports or params. `name` is the configured name, or "#n" (1-based number). */
struct Ref {
	enum Kind { MODULE, INPUT, OUTPUT, PARAM };
	std::string module;
	Kind kind = MODULE;
	std::string name;
	int side = SIDE_ANY;
};

enum class Cond { Next, Module, Cable, Param, Side, Signal, Wait, All, Any };

struct Condition {
	Cond type = Cond::Next;
	/** module / param / signal port / cable "from" */
	Ref ref;
	/** cable "to" */
	Ref to;
	bool hasFrom = false, hasTo = false;
	bool hasMin = false, hasMax = false;
	double min = 0.0, max = 0.0;
	/** param: > 0 means "moved by this fraction of the range since the step began" */
	double moved = 0.0;
	/** side condition */
	int side = SIDE_ANY;
	/** signal: volts; wait: seconds */
	double value = 0.0;
	std::vector<Condition> parts;
};

struct Step {
	std::string text;
	std::vector<Ref> highlight;
	int side = SIDE_ANY;
	Condition done;
	bool showMe = true;
};

struct Lesson {
	std::string title;
	int startSide = SIDE_FRONT;
	std::vector<std::string> learned;
	std::vector<Step> steps;
};

struct IndexEntry {
	std::string id, title, description;
};


namespace detail {

inline bool fail(std::string* error, const std::string& message) {
	if (error)
		*error = message;
	return false;
}

inline std::string str(json_t* o, const char* key) {
	json_t* const j = json_object_get(o, key);
	return json_is_string(j) ? json_string_value(j) : "";
}

/** Missing is fine (SIDE_ANY); otherwise "front" or "back" */
inline bool parseSide(json_t* j, int* out) {
	if (!j)
		return true;
	const std::string s = json_is_string(j) ? json_string_value(j) : "";
	if (s == "front")
		*out = SIDE_FRONT;
	else if (s == "back")
		*out = SIDE_BACK;
	else
		return false;
	return true;
}

enum Need { NEED_MODULE, NEED_PORT, NEED_PARAM, NEED_ANY };

/** A reference object, or a plain string as a module slug */
inline bool parseRef(json_t* j, Need need, Ref* out, std::string* error) {
	if (json_is_string(j)) {
		out->module = json_string_value(j);
		out->kind = Ref::MODULE;
	}
	else if (json_is_object(j)) {
		out->module = str(j, "module");
		const char* const keys[3] = {"input", "output", "param"};
		const Ref::Kind kinds[3] = {Ref::INPUT, Ref::OUTPUT, Ref::PARAM};
		int count = 0;
		for (int i = 0; i < 3; ++i) {
			json_t* const v = json_object_get(j, keys[i]);
			if (!v)
				continue;
			if (!json_is_string(v) || !*json_string_value(v))
				return fail(error, std::string("\"") + keys[i] + "\" must be a non-empty string");
			out->kind = kinds[i];
			out->name = json_string_value(v);
			++count;
		}
		if (count > 1)
			return fail(error, "a reference names only one of input / output / param");
		if (!parseSide(json_object_get(j, "side"), &out->side))
			return fail(error, "\"side\" must be \"front\" or \"back\"");
	}
	else
		return fail(error, "a reference must be an object or a module name");
	if (out->module.empty())
		return fail(error, "a reference needs \"module\"");
	if (need == NEED_MODULE && out->kind != Ref::MODULE)
		return fail(error, "this reference names a module only");
	if (need == NEED_PORT && out->kind != Ref::INPUT && out->kind != Ref::OUTPUT)
		return fail(error, "this reference must name an input or an output");
	if (need == NEED_PARAM && out->kind != Ref::PARAM)
		return fail(error, "this reference must name a param");
	return true;
}

inline bool parseNumber(json_t* j, double* out) {
	if (!json_is_number(j))
		return false;
	*out = json_number_value(j);
	return true;
}

inline bool parseCondition(json_t* j, Condition* c, std::string* error) {
	if (!json_is_object(j))
		return fail(error, "a condition must be an object");
	json_t* v;
	if ((v = json_object_get(j, "next"))) {
		c->type = Cond::Next;
		return json_is_true(v) ? true : fail(error, "\"next\" must be true");
	}
	if ((v = json_object_get(j, "module"))) {
		c->type = Cond::Module;
		return parseRef(v, NEED_MODULE, &c->ref, error);
	}
	if ((v = json_object_get(j, "cable"))) {
		c->type = Cond::Cable;
		json_t* const from = json_is_object(v) ? json_object_get(v, "from") : nullptr;
		json_t* const to = json_is_object(v) ? json_object_get(v, "to") : nullptr;
		if (!from && !to)
			return fail(error, "\"cable\" needs \"from\" or \"to\"");
		if (from) {
			c->hasFrom = true;
			if (!parseRef(from, NEED_PORT, &c->ref, error))
				return false;
			if (c->ref.kind != Ref::OUTPUT)
				return fail(error, "cable \"from\" must be an output");
		}
		if (to) {
			c->hasTo = true;
			if (!parseRef(to, NEED_PORT, &c->to, error))
				return false;
			if (c->to.kind != Ref::INPUT)
				return fail(error, "cable \"to\" must be an input");
		}
		return true;
	}
	if ((v = json_object_get(j, "param"))) {
		c->type = Cond::Param;
		if (!parseRef(v, NEED_PARAM, &c->ref, error))
			return false;
		json_t* const mn = json_object_get(j, "min");
		json_t* const mx = json_object_get(j, "max");
		json_t* const mv = json_object_get(j, "moved");
		if (!mn && !mx && !mv)
			return fail(error, "\"param\" needs \"min\", \"max\" or \"moved\"");
		if (mv && (mn || mx))
			return fail(error, "\"param\" takes either a range or \"moved\"");
		if (mv && (!parseNumber(mv, &c->moved) || c->moved <= 0.0 || c->moved > 1.0))
			return fail(error, "\"moved\" must be a number in (0, 1]");
		if (mn && !(c->hasMin = parseNumber(mn, &c->min)))
			return fail(error, "\"min\" must be a number");
		if (mx && !(c->hasMax = parseNumber(mx, &c->max)))
			return fail(error, "\"max\" must be a number");
		if (c->hasMin && c->hasMax && c->min > c->max)
			return fail(error, "\"min\" is above \"max\"");
		return true;
	}
	if ((v = json_object_get(j, "side"))) {
		c->type = Cond::Side;
		if (!parseSide(v, &c->side))
			return fail(error, "\"side\" must be \"front\" or \"back\"");
		return true;
	}
	if ((v = json_object_get(j, "signal"))) {
		c->type = Cond::Signal;
		if (!parseRef(v, NEED_PORT, &c->ref, error))
			return false;
		if (!parseNumber(json_object_get(j, "above"), &c->value) || c->value < 0.0)
			return fail(error, "\"signal\" needs \"above\" (volts, 0 or more)");
		return true;
	}
	if ((v = json_object_get(j, "wait"))) {
		c->type = Cond::Wait;
		if (!parseNumber(v, &c->value) || c->value <= 0.0)
			return fail(error, "\"wait\" must be a number of seconds above 0");
		return true;
	}
	const bool all = json_object_get(j, "all") != nullptr;
	if ((v = json_object_get(j, all ? "all" : "any"))) {
		c->type = all ? Cond::All : Cond::Any;
		if (!json_is_array(v) || json_array_size(v) == 0)
			return fail(error, std::string("\"") + (all ? "all" : "any") + "\" needs a non-empty list");
		for (size_t i = 0; i < json_array_size(v); ++i) {
			Condition part;
			if (!parseCondition(json_array_get(v, i), &part, error))
				return false;
			c->parts.push_back(part);
		}
		return true;
	}
	return fail(error, "unknown condition");
}

inline bool parseStep(json_t* j, Step* s, std::string* error) {
	if (!json_is_object(j))
		return fail(error, "a step must be an object");
	s->text = str(j, "text");
	if (s->text.empty())
		return fail(error, "\"text\" is missing");
	if (json_t* const h = json_object_get(j, "highlight")) {
		if (!json_is_array(h))
			return fail(error, "\"highlight\" must be a list");
		for (size_t i = 0; i < json_array_size(h); ++i) {
			Ref r;
			if (!parseRef(json_array_get(h, i), NEED_ANY, &r, error))
				return false;
			s->highlight.push_back(r);
		}
	}
	if (!parseSide(json_object_get(j, "side"), &s->side))
		return fail(error, "\"side\" must be \"front\" or \"back\"");
	json_t* const done = json_object_get(j, "done");
	if (!done)
		return fail(error, "\"done\" is missing");
	if (!parseCondition(done, &s->done, error))
		return false;
	if (json_t* const sm = json_object_get(j, "showMe")) {
		const std::string v = json_is_string(sm) ? json_string_value(sm) : "";
		if (v != "auto" && v != "none")
			return fail(error, "\"showMe\" must be \"auto\" or \"none\"");
		s->showMe = v == "auto";
	}
	return true;
}

} // namespace detail


inline bool parseLesson(const std::string& text, Lesson* out, std::string* error) {
	json_error_t jerr;
	json_t* const root = json_loads(text.c_str(), 0, &jerr);
	if (!root)
		return detail::fail(error, std::string("not valid JSON: ") + jerr.text);
	Lesson l;
	bool ok = true;
	std::string e;
	l.title = detail::str(root, "title");
	json_t* const steps = json_object_get(root, "steps");
	if (l.title.empty())
		ok = detail::fail(&e, "\"title\" is missing");
	else if (!detail::parseSide(json_object_get(root, "startSide"), &l.startSide))
		ok = detail::fail(&e, "\"startSide\" must be \"front\" or \"back\"");
	else if (!json_is_array(steps) || json_array_size(steps) == 0)
		ok = detail::fail(&e, "\"steps\" must be a non-empty list");
	if (ok) {
		if (l.startSide == SIDE_ANY)
			l.startSide = SIDE_FRONT;
		json_t* const learned = json_object_get(root, "learned");
		for (size_t i = 0; json_is_array(learned) && i < json_array_size(learned); ++i)
			if (json_is_string(json_array_get(learned, i)))
				l.learned.push_back(json_string_value(json_array_get(learned, i)));
		for (size_t i = 0; ok && i < json_array_size(steps); ++i) {
			Step s;
			std::string se;
			if (!detail::parseStep(json_array_get(steps, i), &s, &se))
				ok = detail::fail(&e, "step " + std::to_string(i + 1) + ": " + se);
			else
				l.steps.push_back(s);
		}
	}
	json_decref(root);
	if (!ok)
		return detail::fail(error, e);
	*out = l;
	return true;
}

/** Lesson ids are folder names: lower-case letters, digits and '-' only */
inline bool validLessonId(const std::string& id) {
	if (id.empty())
		return false;
	for (char ch : id)
		if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'))
			return false;
	return true;
}

inline bool parseIndex(const std::string& text, std::vector<IndexEntry>* out, std::string* error) {
	json_error_t jerr;
	json_t* const root = json_loads(text.c_str(), 0, &jerr);
	if (!root)
		return detail::fail(error, std::string("not valid JSON: ") + jerr.text);
	std::vector<IndexEntry> list;
	bool ok = true;
	std::string e;
	json_t* const lessons = json_object_get(root, "lessons");
	if (!json_is_array(lessons))
		ok = detail::fail(&e, "\"lessons\" must be a list");
	for (size_t i = 0; ok && i < json_array_size(lessons); ++i) {
		json_t* const j = json_array_get(lessons, i);
		IndexEntry entry;
		entry.id = detail::str(j, "id");
		entry.title = detail::str(j, "title");
		entry.description = detail::str(j, "description");
		if (!validLessonId(entry.id))
			ok = detail::fail(&e, "lesson " + std::to_string(i + 1) + ": bad \"id\"");
		else if (entry.title.empty())
			ok = detail::fail(&e, "lesson " + std::to_string(i + 1) + ": \"title\" is missing");
		else
			list.push_back(entry);
	}
	json_decref(root);
	if (!ok)
		return detail::fail(error, e);
	*out = list;
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// What the rack looks like right now (built every frame by Tutorial.cpp)

struct ModuleState {
	int64_t id;
	std::string slug;
	/** Brand (plugin slug): "JXMP", "Cardinal", "Fundamental"… */
	std::string plugin;
};

struct PortState {
	int64_t moduleId;
	bool output;
	int index;
	std::string name;
	int side;
	/** Peak |voltage| over the last 0.3 s, all channels */
	float peak;
};

struct ParamState {
	int64_t moduleId;
	int index;
	std::string name;
	/** In the unit the knob shows (Hz, %, dB…) */
	float display;
	/** 0..1 across the knob's range */
	float scaled;
};

struct CableState {
	int64_t outModule;
	int outIndex;
	int64_t inModule;
	int inIndex;
};

struct RackState {
	std::vector<ModuleState> modules;
	std::vector<PortState> ports;
	std::vector<ParamState> params;
	std::vector<CableState> cables;
	int side = SIDE_FRONT;
};

/** Lessons teach JXMP modules; Host Audio is Cardinal's. Same-named modules of other brands (Fundamental VCO…)
do not count. */
inline std::string pluginFor(const std::string& slug) {
	return slug == "HostAudio2" ? "Cardinal" : "JXMP";
}

/** The first module of that model (lowest id) */
inline const ModuleState* findModule(const RackState& s, const std::string& slug) {
	const std::string plugin = pluginFor(slug);
	const ModuleState* best = nullptr;
	for (const ModuleState& m : s.modules)
		if (m.slug == slug && m.plugin == plugin && (best == nullptr || m.id < best->id))
			best = &m;
	return best;
}

/** "#n" matches the n-th (1-based) port or param; anything else matches the configured name */
inline bool nameMatches(const std::string& want, const std::string& name, int index) {
	if (want.size() > 1 && want[0] == '#')
		return std::atoi(want.c_str() + 1) == index + 1;
	return want == name;
}

inline const PortState* findPort(const RackState& s, const Ref& r) {
	const ModuleState* const m = findModule(s, r.module);
	if (m == nullptr || (r.kind != Ref::INPUT && r.kind != Ref::OUTPUT))
		return nullptr;
	for (const PortState& p : s.ports) {
		if (p.moduleId != m->id || p.output != (r.kind == Ref::OUTPUT))
			continue;
		if (r.side != SIDE_ANY && p.side != r.side)
			continue;
		if (nameMatches(r.name, p.name, p.index))
			return &p;
	}
	return nullptr;
}

inline const ParamState* findParam(const RackState& s, const Ref& r) {
	const ModuleState* const m = findModule(s, r.module);
	if (m == nullptr || r.kind != Ref::PARAM)
		return nullptr;
	for (const ParamState& p : s.params)
		if (p.moduleId == m->id && nameMatches(r.name, p.name, p.index))
			return &p;
	return nullptr;
}

/** Per-step memory: when the step began, Next clicks, knob positions at their first look */
struct StepContext {
	double stepStart = 0.0;
	bool nextPressed = false;
	std::map<std::string, float> baseline;
};

inline std::string paramKey(const ParamState& p) {
	return std::to_string(p.moduleId) + "/" + std::to_string(p.index);
}

inline bool holds(const Condition& c, const RackState& s, StepContext& ctx, double t) {
	switch (c.type) {
		case Cond::Next:
			return ctx.nextPressed;
		case Cond::Wait:
			return t - ctx.stepStart >= c.value;
		case Cond::Module:
			return findModule(s, c.ref.module) != nullptr;
		case Cond::Side:
			return s.side == c.side;
		case Cond::Signal: {
			const PortState* const p = findPort(s, c.ref);
			return p != nullptr && p->peak > c.value;
		}
		case Cond::Param: {
			const ParamState* const p = findParam(s, c.ref);
			if (p == nullptr)
				return false;
			if (c.moved > 0.0) {
				const std::string key = paramKey(*p);
				std::map<std::string, float>::const_iterator it = ctx.baseline.find(key);
				if (it == ctx.baseline.end()) {
					ctx.baseline[key] = p->scaled;
					return false;
				}
				return std::fabs(p->scaled - it->second) >= c.moved - 1e-6;
			}
			return (!c.hasMin || p->display >= c.min) && (!c.hasMax || p->display <= c.max);
		}
		case Cond::Cable: {
			const PortState* const from = c.hasFrom ? findPort(s, c.ref) : nullptr;
			const PortState* const to = c.hasTo ? findPort(s, c.to) : nullptr;
			if ((c.hasFrom && from == nullptr) || (c.hasTo && to == nullptr))
				return false;
			for (const CableState& k : s.cables) {
				if (from != nullptr && (k.outModule != from->moduleId || k.outIndex != from->index))
					continue;
				if (to != nullptr && (k.inModule != to->moduleId || k.inIndex != to->index))
					continue;
				return true;
			}
			return false;
		}
		case Cond::All: {
			// every part is evaluated, so "moved" parts record their baselines from the start
			bool ok = true;
			for (const Condition& part : c.parts)
				ok = holds(part, s, ctx, t) && ok;
			return ok;
		}
		case Cond::Any: {
			bool ok = false;
			for (const Condition& part : c.parts)
				ok = holds(part, s, ctx, t) || ok;
			return ok;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------
// Step runner

struct Runner {
	enum Phase { ACTIVE, DONE, FINISHED };
	static constexpr double kHold = 0.3;
	static constexpr double kAdvance = 0.8;

	const Lesson* lesson = nullptr;
	int step = 0;
	Phase phase = ACTIVE;
	/** Entered with Back: the step is only shown again, Next moves on */
	bool reviewing = false;
	double holdSince = -1.0;
	double doneAt = 0.0;
	StepContext ctx;
	/** Module slugs that were in the rack at some point of the lesson */
	std::set<std::string> seen;
	/** Counts the times the lesson reached its end (the app records the lesson as done on a change) */
	int finishCount = 0;
	/** The furthest step reached: steps before it are only shown again (reviewing) when Next passes them */
	int furthest = 0;

	void start(const Lesson& l, double t) {
		lesson = &l;
		step = 0;
		phase = ACTIVE;
		reviewing = false;
		seen.clear();
		finishCount = 0;
		furthest = 0;
		enter(t);
	}

	const Step& current() const {
		return lesson->steps[step];
	}

	bool isTask() const {
		return current().done.type != Cond::Next;
	}

	bool nextEnabled() const {
		return lesson != nullptr && phase == ACTIVE && (!isTask() || reviewing);
	}

	/** The step wants the other side of the rack first */
	bool needsTurn(const RackState& s) const {
		return lesson != nullptr && phase != FINISHED && current().side != SIDE_ANY && current().side != s.side;
	}

	void update(const RackState& s, double t) {
		for (const ModuleState& m : s.modules)
			if (m.plugin == pluginFor(m.slug))
				seen.insert(m.slug);
		if (lesson == nullptr || phase == FINISHED)
			return;
		if (phase == DONE) {
			if (t - doneAt >= kAdvance - 1e-9)
				advance(s, t);
			return;
		}
		if (reviewing || !isTask())
			return;
		const Condition& c = current().done;
		if (!holds(c, s, ctx, t)) {
			holdSince = -1.0;
			return;
		}
		if (holdSince < 0.0)
			holdSince = t;
		if (c.type == Cond::Wait || t - holdSince >= kHold - 1e-9) {
			phase = DONE;
			doneAt = t;
		}
	}

	void pressNext(const RackState& s, double t) {
		if (nextEnabled())
			advance(s, t);
	}

	void back(double t) {
		if (lesson == nullptr)
			return;
		if (phase == FINISHED) {
			phase = ACTIVE;
			reviewing = true;
			enter(t);
			return;
		}
		if (step == 0)
			return;
		--step;
		phase = ACTIVE;
		reviewing = true;
		enter(t);
	}

private:
	void enter(double t) {
		ctx = StepContext();
		ctx.stepStart = t;
		holdSince = -1.0;
	}

	/** To the next step; a task that already holds is skipped */
	void advance(const RackState& s, double t) {
		reviewing = false;
		phase = ACTIVE;
		for (;;) {
			if (step + 1 >= (int) lesson->steps.size()) {
				phase = FINISHED;
				++finishCount;
				return;
			}
			++step;
			enter(t);
			if (step < furthest) {
				reviewing = true;
				return;
			}
			furthest = step;
			const Condition& c = current().done;
			if (c.type != Cond::Next && c.type != Cond::Wait && holds(c, s, ctx, t))
				continue;
			return;
		}
	}
};


inline void collectModules(const Condition& c, std::vector<std::string>* out) {
	if (c.type == Cond::Module || c.type == Cond::Param || c.type == Cond::Signal || (c.type == Cond::Cable && c.hasFrom))
		out->push_back(c.ref.module);
	if (c.type == Cond::Cable && c.hasTo)
		out->push_back(c.to.module);
	for (const Condition& part : c.parts)
		collectModules(part, out);
}

/** Modules the step needs that were in the rack earlier in the lesson and are gone now */
inline std::vector<std::string> goneModules(const Step& step, const RackState& s, const std::set<std::string>& seen) {
	std::vector<std::string> names;
	for (const Ref& r : step.highlight)
		names.push_back(r.module);
	collectModules(step.done, &names);
	std::vector<std::string> gone;
	for (const std::string& n : names) {
		if (seen.count(n) == 0 || findModule(s, n) != nullptr)
			continue;
		bool listed = false;
		for (const std::string& g : gone)
			listed = listed || g == n;
		if (!listed)
			gone.push_back(n);
	}
	return gone;
}

/** None of the lesson's start modules is left: the user opened another patch. Matched by id and model, since
patch files reuse small ids (the start patches use large ones). */
inline bool patchReplaced(const std::vector<ModuleState>& start, const RackState& s) {
	if (start.empty())
		return false;
	for (const ModuleState& a : start)
		for (const ModuleState& m : s.modules)
			if (m.id == a.id && m.slug == a.slug && m.plugin == a.plugin)
				return false;
	return true;
}


// ---------------------------------------------------------------------------------------------------------
// Show me

struct Action {
	enum Type { ADD_MODULE, ADD_CABLE, SET_PARAM, MOVE_PARAM, SET_SIDE };
	Type type = ADD_MODULE;
	/** ADD_MODULE: model slug */
	std::string module;
	/** ADD_CABLE */
	Ref from, to;
	/** SET_PARAM / MOVE_PARAM */
	Ref param;
	/** SET_PARAM: displayed value; MOVE_PARAM: fraction of the range */
	double value = 0.0;
	/** SET_SIDE */
	int side = SIDE_ANY;
	/** SET_PARAM: the range to land in (displayed units), so the app can nudge a value rounded just outside */
	bool hasMin = false, hasMax = false;
	double min = 0.0, max = 0.0;
};

inline bool canDo(const Condition& c) {
	switch (c.type) {
		case Cond::Module:
		case Cond::Param:
		case Cond::Side:
			return true;
		case Cond::Cable:
			return c.hasFrom && c.hasTo;
		case Cond::All:
			for (const Condition& part : c.parts)
				if (canDo(part))
					return true;
			return false;
		case Cond::Any:
			return canDo(c.parts[0]);
		default:
			return false;
	}
}

inline bool canShowMe(const Step& step) {
	return step.showMe && canDo(step.done);
}

inline void addModuleAction(const std::string& slug, const RackState& s, std::vector<Action>* out) {
	if (findModule(s, slug) != nullptr)
		return;
	for (const Action& a : *out)
		if (a.type == Action::ADD_MODULE && a.module == slug)
			return;
	Action a;
	a.type = Action::ADD_MODULE;
	a.module = slug;
	out->push_back(a);
}

inline void conditionActions(const Condition& c, const RackState& s, std::vector<Action>* out) {
	Action a;
	switch (c.type) {
		case Cond::Module:
			addModuleAction(c.ref.module, s, out);
			break;
		case Cond::Side:
			a.type = Action::SET_SIDE;
			a.side = c.side;
			out->push_back(a);
			break;
		case Cond::Cable:
			if (!c.hasFrom || !c.hasTo)
				break;
			addModuleAction(c.ref.module, s, out);
			addModuleAction(c.to.module, s, out);
			a.type = Action::ADD_CABLE;
			a.from = c.ref;
			a.to = c.to;
			out->push_back(a);
			break;
		case Cond::Param:
			addModuleAction(c.ref.module, s, out);
			a.param = c.ref;
			if (c.moved > 0.0) {
				a.type = Action::MOVE_PARAM;
				a.value = c.moved + 0.05;
			}
			else {
				a.type = Action::SET_PARAM;
				a.value = c.hasMin && c.hasMax ? (c.min + c.max) / 2.0 : c.hasMin ? c.min : c.max;
				a.hasMin = c.hasMin;
				a.hasMax = c.hasMax;
				a.min = c.min;
				a.max = c.max;
			}
			out->push_back(a);
			break;
		case Cond::All:
			for (const Condition& part : c.parts)
				if (canDo(part))
					conditionActions(part, s, out);
			break;
		case Cond::Any:
			conditionActions(c.parts[0], s, out);
			break;
		default:
			break;
	}
}

/** What Show me does for the step, in order: turn the rack, add modules, then cables and knobs */
inline std::vector<Action> showMeActions(const Step& step, const RackState& s) {
	std::vector<Action> out;
	if (!canShowMe(step))
		return out;
	if (step.side != SIDE_ANY && step.side != s.side) {
		Action a;
		a.type = Action::SET_SIDE;
		a.side = step.side;
		out.push_back(a);
	}
	conditionActions(step.done, s, &out);
	return out;
}

// ---------------------------------------------------------------------------------------------------------
// "Waiting for: …" in the panel

inline std::string numberText(double v) {
	std::ostringstream o;
	o << v;
	return o.str();
}

inline std::string refText(const Ref& r) {
	std::string t = r.module;
	if (r.kind != Ref::MODULE)
		t += " " + r.name;
	if (r.side == SIDE_BACK)
		t += " (back)";
	else if (r.side == SIDE_FRONT)
		t += " (front)";
	return t;
}

inline std::string describe(const Condition& c) {
	switch (c.type) {
		case Cond::Next:
			return "Next";
		case Cond::Wait:
			return "a few seconds";
		case Cond::Module: {
			const char first = c.ref.module.empty() ? 'x' : c.ref.module[0];
			const bool vowel = std::string("AEFHILMNORSX").find(first) != std::string::npos;
			return std::string(vowel ? "an " : "a ") + c.ref.module + " in the rack";
		}
		case Cond::Side:
			return c.side == SIDE_BACK ? "the back of the rack" : "the front of the rack";
		case Cond::Signal:
			return c.ref.module == "HostAudio2" ? "sound from the speakers" : "a signal at " + refText(c.ref);
		case Cond::Param: {
			const std::string p = c.ref.module + " " + c.ref.name;
			if (c.moved > 0.0)
				return "a turn of " + p;
			if (c.hasMin && c.hasMax)
				return p + " between " + numberText(c.min) + " and " + numberText(c.max);
			return p + (c.hasMin ? " at " + numberText(c.min) + " or more" : " at " + numberText(c.max) + " or less");
		}
		case Cond::Cable:
			if (c.hasFrom && c.hasTo)
				return "a cable " + refText(c.ref) + " \xE2\x86\x92 " + refText(c.to);
			return c.hasFrom ? "a cable from " + refText(c.ref) : "a cable into " + refText(c.to);
		case Cond::All:
		case Cond::Any:
			return c.parts.empty() ? "" : describe(c.parts[0]);
	}
	return "";
}

/** What the step still waits for: the first part of an "all" that does not hold yet, else the condition */
inline std::string waitingFor(const Condition& c, const RackState& s, StepContext& ctx, double t) {
	if (c.type == Cond::All) {
		for (const Condition& part : c.parts) {
			StepContext probe = ctx;  // no baselines recorded by just asking
			if (!holds(part, s, probe, t))
				return waitingFor(part, s, ctx, t);
		}
	}
	return describe(c);
}

} // namespace patchcore_tutorial

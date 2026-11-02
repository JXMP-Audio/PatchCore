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

} // namespace patchcore_tutorial

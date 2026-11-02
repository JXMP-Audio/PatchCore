# Interactive Tutorials Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Help → Tutorials opens step-by-step lessons that watch what the user patches and move on by themselves,
teaching a beginner the basics with JXMP modules (lessons 1–3).

**Architecture:** Lessons are data (`plugins/JXMP/res/tutorials/<id>/lesson.json` + `start.vcv`). A pure-logic header
`src/patchcore/TutorialLogic.hpp` parses lessons, evaluates step conditions against a plain `RackState` snapshot and
runs the step sequence; it is tested without Rack. `src/patchcore/Tutorial.cpp` takes the snapshot from Rack every
frame, draws the step panel and the highlight, and performs Show me actions.

**Tech Stack:** C++11 (gnu++11 like DPF), Rack 2 API (Cardinal fork), jansson, NanoVG; tests with plain `g++` +
`assert` (`src/patchcore/tests/run.sh`); Python 3 for the start-patch generator.

**Spec:** [15-tutorials-design.md](15-tutorials-design.md)

## Global Constraints

- **No builds, no tests, no GitHub Actions until the user says "build"** (user rule). Steps that say "Run" are
  done only after that; before it, write the code and record "not run" in the commit message.
- Work on branch `tutorials` (on top of `jxmp-set3`). Push after every task. Do not merge.
- English only for all lesson text and UI strings.
- Lesson text: at most 4 sentences per step.
- References use the configured names of `configInput` / `configOutput` / `configParam` / `configSwitch` /
  `configButton`; unnamed ports use `"#n"` (1-based); Patchbay ports add `"side": "front"|"back"`.
- A condition other than `next` and `wait` must hold for **0.3 s** without a break; a done task step advances after
  **0.8 s**.
- Every Show me action goes through Rack history (Ctrl+Z undoes it).
- Lessons live in `plugins/JXMP/res/tutorials`; nothing goes into `src/Rack` (submodule).
- C++11 only: no `std::optional`, no `std::make_unique`, no structured bindings.
- Style: tabs, `namespace rack { namespace patchcore {`, comments like the surrounding code (short, plain English).
- No third-party trademarks in text (no Roland, TR-808 etc.).

## Review Focus

1. **Back on a task step**: going Back to a done task must not bounce forward again at once — the step is shown in
   "reviewing" mode with Next enabled and no auto-advance. Test in Task 3.
2. **The user loads another patch (File → Open / New) while a lesson runs**: the lesson must close, not point at
   modules that are gone. `patchReplaced()` test in Task 3.
3. **A knob swept through the target range**: a `param` range condition must not complete on a pass-through
   shorter than 0.3 s. Test in Task 3 (hold timer).
4. **Show me on an input that already has a cable**: Rack inputs take one cable; the old cable is removed (with
   history) before the new one is added. Action list test in Task 3 (`ADD_CABLE` after `ADD_MODULE`), the removal
   itself in Task 5.
5. **A lesson reference that does not exist after a module rename**: the content test fails with
   "lesson N step M: …". Test in Task 4.

---

## File map

| File | Responsibility |
|---|---|
| `src/patchcore/TutorialLogic.hpp` (new) | Lesson data types, JSON parsing, `RackState`, reference resolution, conditions, `Runner`, Show me action lists |
| `src/patchcore/tests/test_tutorial.cpp` (new) | Unit tests of the logic |
| `src/patchcore/tests/test_tutorial_content.cpp` (new) | Content check of every lesson against the module sources |
| `src/patchcore/tests/run.sh` | Link `-ljansson`, pass `PATCHCORE_ROOT` |
| `plugins/JXMP/res/tutorials/index.json`, `*/lesson.json`, `*/start.vcv` (new) | The three lessons |
| `plugins/JXMP/tests/make_tutorial_patches.py` (new) | Generates the three `start.vcv` |
| `plugins/Makefile` | Bundle `JXMP/res/tutorials` |
| `src/patchcore/Tutorial.cpp` (new) | Snapshot, panel, highlight, Show me, open lesson, first-start offer |
| `src/patchcore/PatchCore.hpp` / `PatchCore.cpp` | Settings `tutorialsDone`, `tutorialOffered`; tutorial API declarations |
| `src/CardinalCommon.hpp` / `.cpp` | `loadPathDialog` gets an `afterLoad` callback |
| `src/override/Scene.cpp` | Adds the tutorial layer |
| `src/override/MenuBar.cpp` | Help → Tutorials submenu |
| `src/Makefile` | Compile `patchcore/Tutorial.cpp` |
| `plugins/JXMP/README.md`, `research_docs/STATUS.md` | Docs |

---

### Task 1: Lesson model and parser

**Files:**
- Create: `src/patchcore/TutorialLogic.hpp`
- Create: `src/patchcore/tests/test_tutorial.cpp`
- Modify: `src/patchcore/tests/run.sh`

**Interfaces:**
- Produces (namespace `patchcore_tutorial`): `enum Side { SIDE_ANY = -1, SIDE_FRONT = 0, SIDE_BACK = 1 }`;
  `struct Ref { enum Kind { MODULE, INPUT, OUTPUT, PARAM }; std::string module; Kind kind; std::string name; int side; }`;
  `enum class Cond { Next, Module, Cable, Param, Side, Signal, Wait, All, Any }`;
  `struct Condition { Cond type; Ref ref; Ref to; bool hasFrom, hasTo, hasMin, hasMax; double min, max, moved; int side; double value; std::vector<Condition> parts; }`;
  `struct Step { std::string text; std::vector<Ref> highlight; int side; Condition done; bool showMe; }`;
  `struct Lesson { std::string title; int startSide; std::vector<std::string> learned; std::vector<Step> steps; }`;
  `struct IndexEntry { std::string id, title, description; }`;
  `bool parseLesson(const std::string& json, Lesson* out, std::string* error)`;
  `bool parseIndex(const std::string& json, std::vector<IndexEntry>* out, std::string* error)`.

- [ ] **Step 1: Link jansson in the test runner**

Replace the loop body in `src/patchcore/tests/run.sh`:

```bash
for t in "$here"/test_*.cpp; do
	name="$(basename "$t" .cpp)"
	g++ -std=c++11 -Wall -Wextra -Werror -I"$here/.." -DPATCHCORE_ROOT="\"$here/../../..\"" "$t" -o "$out/$name" -ljansson
	"$out/$name"
done
```

- [ ] **Step 2: Write the failing parser test**

Create `src/patchcore/tests/test_tutorial.cpp`:

```cpp
#include <cassert>
#include <cstdio>
#include <string>
#include "../TutorialLogic.hpp"

using namespace patchcore_tutorial;

static void testParse() {
	const char* good = R"({
		"title": "First sound",
		"startSide": "back",
		"learned": ["Cables"],
		"steps": [
			{"text": "Hello.", "done": {"next": true}},
			{"text": "Add a VCO.", "done": {"module": "VCO"}, "showMe": "none"},
			{"text": "Patch.", "side": "back",
			 "highlight": [{"module": "VCO", "output": "Saw"}, {"module": "Patchbay4", "input": "Ch 1", "side": "back"}],
			 "done": {"all": [
				{"cable": {"from": {"module": "VCO", "output": "Saw"}, "to": {"module": "VCA", "input": "In 1"}}},
				{"signal": {"module": "HostAudio2", "input": "#1"}, "above": 0.05}]}},
			{"text": "Turn.", "done": {"param": {"module": "VCF", "param": "Resonance"}, "min": 50}},
			{"text": "Turn more.", "done": {"param": {"module": "VCA", "param": "Channel 1 level"}, "moved": 0.2}},
			{"text": "Flip.", "done": {"side": "front"}},
			{"text": "Wait.", "done": {"wait": 2}}
		]})";
	Lesson l;
	std::string err;
	assert(parseLesson(good, &l, &err));
	assert(l.title == "First sound" && l.startSide == SIDE_BACK && l.learned.size() == 1 && l.steps.size() == 7);
	assert(l.steps[0].done.type == Cond::Next);
	assert(l.steps[1].done.type == Cond::Module && l.steps[1].done.ref.module == "VCO" && !l.steps[1].showMe);
	const Step& s2 = l.steps[2];
	assert(s2.side == SIDE_BACK && s2.highlight.size() == 2);
	assert(s2.highlight[1].kind == Ref::INPUT && s2.highlight[1].side == SIDE_BACK && s2.highlight[1].name == "Ch 1");
	assert(s2.done.type == Cond::All && s2.done.parts.size() == 2);
	const Condition& cable = s2.done.parts[0];
	assert(cable.type == Cond::Cable && cable.hasFrom && cable.hasTo);
	assert(cable.ref.kind == Ref::OUTPUT && cable.ref.name == "Saw" && cable.to.kind == Ref::INPUT && cable.to.name == "In 1");
	assert(s2.done.parts[1].type == Cond::Signal && s2.done.parts[1].value == 0.05);
	assert(l.steps[3].done.type == Cond::Param && l.steps[3].done.hasMin && !l.steps[3].done.hasMax && l.steps[3].done.min == 50);
	assert(l.steps[4].done.moved == 0.2);
	assert(l.steps[5].done.type == Cond::Side && l.steps[5].done.side == SIDE_FRONT);
	assert(l.steps[6].done.type == Cond::Wait && l.steps[6].done.value == 2);

	// Rejections, each with a message naming the step
	const char* bad[] = {
		"not json",
		R"({"steps": [{"text": "x", "done": {"next": true}}]})",                                   // no title
		R"({"title": "t", "steps": []})",                                                         // no steps
		R"({"title": "t", "steps": [{"done": {"next": true}}]})",                                 // no text
		R"({"title": "t", "steps": [{"text": "x"}]})",                                            // no done
		R"({"title": "t", "steps": [{"text": "x", "done": {"dance": true}}]})",                   // unknown condition
		R"({"title": "t", "steps": [{"text": "x", "done": {"cable": {}}}]})",                     // cable without ends
		R"({"title": "t", "steps": [{"text": "x", "done": {"cable": {"from": {"module": "VCO", "input": "FM"}}}}]})",  // from must be an output
		R"({"title": "t", "steps": [{"text": "x", "done": {"param": {"module": "VCF", "param": "Resonance"}}}]})",      // no min/max/moved
		R"({"title": "t", "steps": [{"text": "x", "done": {"param": {"module": "VCF", "param": "Resonance"}, "min": 1, "moved": 0.2}}]})",
		R"({"title": "t", "steps": [{"text": "x", "done": {"side": "left"}}]})",
		R"({"title": "t", "steps": [{"text": "x", "done": {"signal": {"module": "VCO", "output": "Saw"}}}]})",          // no above
		R"({"title": "t", "steps": [{"text": "x", "done": {"wait": 0}}]})",
		R"({"title": "t", "steps": [{"text": "x", "done": {"all": []}}]})",
		R"({"title": "t", "steps": [{"text": "x", "done": {"module": {"output": "Saw"}}}]})",                           // ref without module
		R"({"title": "t", "steps": [{"text": "x", "done": {"next": true}, "showMe": "maybe"}]})",
	};
	for (const char* b : bad) {
		Lesson x;
		std::string e;
		assert(!parseLesson(b, &x, &e));
		assert(!e.empty());
	}
	Lesson x;
	assert(!parseLesson(R"({"title": "t", "steps": [{"text": "a", "done": {"next": true}}, {"text": "b", "done": {"wait": -1}}]})", &x, &err));
	assert(err.find("step 2") != std::string::npos);

	// Index
	std::vector<IndexEntry> idx;
	assert(parseIndex(R"({"lessons": [{"id": "01-first-sound", "title": "First sound", "description": "d"}]})", &idx, &err));
	assert(idx.size() == 1 && idx[0].id == "01-first-sound" && idx[0].description == "d");
	assert(!parseIndex(R"({"lessons": [{"id": "../evil", "title": "t", "description": "d"}]})", &idx, &err));
	assert(!parseIndex(R"({"lessons": [{"title": "t"}]})", &idx, &err));
}

int main() {
	testParse();
	std::printf("tutorial logic: all tests passed\n");
	return 0;
}
```

- [ ] **Step 3: Run it to see it fail** (only after the user said "build")

Run: `bash src/patchcore/tests/run.sh`
Expected: compile error, `../TutorialLogic.hpp: No such file or directory`.

- [ ] **Step 4: Write the model and parser**

Create `src/patchcore/TutorialLogic.hpp`:

```cpp
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
```

- [ ] **Step 5: Run the tests** (only after "build")

Run: `bash src/patchcore/tests/run.sh`
Expected: `tutorial logic: all tests passed` among the other test outputs.

- [ ] **Step 6: Commit**

```bash
git add src/patchcore/TutorialLogic.hpp src/patchcore/tests/test_tutorial.cpp src/patchcore/tests/run.sh
git commit -m "Tutorials: lesson model and parser (tests: not run, no build yet)"
git push
```

---

### Task 2: Rack snapshot model and conditions

**Files:**
- Modify: `src/patchcore/TutorialLogic.hpp` (append before the closing namespace)
- Modify: `src/patchcore/tests/test_tutorial.cpp`

**Interfaces:**
- Consumes: Task 1 types.
- Produces: `struct ModuleState { int64_t id; std::string slug; }`;
  `struct PortState { int64_t moduleId; bool output; int index; std::string name; int side; float peak; }`;
  `struct ParamState { int64_t moduleId; int index; std::string name; float display; float scaled; }`;
  `struct CableState { int64_t outModule; int outIndex; int64_t inModule; int inIndex; }`;
  `struct RackState { std::vector<ModuleState> modules; std::vector<PortState> ports; std::vector<ParamState> params; std::vector<CableState> cables; int side; }`;
  `const ModuleState* findModule(const RackState&, const std::string& slug)`;
  `const PortState* findPort(const RackState&, const Ref&)`; `const ParamState* findParam(const RackState&, const Ref&)`;
  `struct StepContext { double stepStart; bool nextPressed; std::map<std::string, float> baseline; }`;
  `bool holds(const Condition&, const RackState&, StepContext&, double t)`.

- [ ] **Step 1: Write the failing condition tests**

Add to `test_tutorial.cpp` above `main` and call `testConditions();` from `main`:

```cpp
static RackState demoRack() {
	RackState s;
	s.side = SIDE_BACK;
	s.modules = {{20, "VCO"}, {10, "VCO"}, {30, "VCA"}, {40, "Patchbay4"}, {50, "HostAudio2"}};
	// VCO 10 is the first VCO (lowest id)
	s.ports = {
		{10, true, 2, "Saw", SIDE_BACK, 5.f},
		{10, true, 3, "Sqr", SIDE_BACK, 5.f},
		{20, true, 2, "Saw", SIDE_BACK, 5.f},
		{30, false, 0, "In 1", SIDE_BACK, 0.f},
		{30, true, 0, "Out 1", SIDE_BACK, 0.f},
		{40, false, 0, "Ch 1", SIDE_FRONT, 0.f},
		{40, false, 4, "Ch 1", SIDE_BACK, 0.f},
		{50, false, 0, "", SIDE_FRONT, 0.3f},
	};
	s.params = {{30, 0, "Channel 1 level", 100.f, 1.f}, {10, 1, "Frequency", 261.6f, 0.5f}};
	s.cables = {{10, 2, 30, 0}};
	return s;
}

static void testConditions() {
	const RackState s = demoRack();
	assert(findModule(s, "VCO")->id == 10);
	assert(findModule(s, "LFO") == nullptr);

	Ref saw;
	saw.module = "VCO";
	saw.kind = Ref::OUTPUT;
	saw.name = "Saw";
	assert(findPort(s, saw)->moduleId == 10);

	Ref pbBack;
	pbBack.module = "Patchbay4";
	pbBack.kind = Ref::INPUT;
	pbBack.name = "Ch 1";
	pbBack.side = SIDE_BACK;
	assert(findPort(s, pbBack)->index == 4);
	pbBack.side = SIDE_FRONT;
	assert(findPort(s, pbBack)->index == 0);

	Ref host;
	host.module = "HostAudio2";
	host.kind = Ref::INPUT;
	host.name = "#1";
	assert(findPort(s, host) != nullptr && findPort(s, host)->index == 0);
	host.name = "#2";
	assert(findPort(s, host) == nullptr);

	Ref inParam;
	inParam.module = "VCA";
	inParam.kind = Ref::PARAM;
	inParam.name = "Channel 1 level";
	assert(findParam(s, inParam)->display == 100.f);

	StepContext ctx;
	ctx.stepStart = 0.0;

	Condition c;
	c.type = Cond::Module;
	c.ref.module = "VCA";
	assert(holds(c, s, ctx, 0.0));
	c.ref.module = "LFO";
	assert(!holds(c, s, ctx, 0.0));

	Condition side;
	side.type = Cond::Side;
	side.side = SIDE_BACK;
	assert(holds(side, s, ctx, 0.0));
	side.side = SIDE_FRONT;
	assert(!holds(side, s, ctx, 0.0));

	// Cable: both ends, only from, only to, wrong end
	Condition cable;
	cable.type = Cond::Cable;
	cable.hasFrom = cable.hasTo = true;
	cable.ref = saw;
	cable.to.module = "VCA";
	cable.to.kind = Ref::INPUT;
	cable.to.name = "In 1";
	assert(holds(cable, s, ctx, 0.0));
	cable.hasTo = false;
	assert(holds(cable, s, ctx, 0.0));
	cable.hasTo = true;
	cable.hasFrom = false;
	assert(holds(cable, s, ctx, 0.0));
	cable.hasFrom = true;
	cable.ref.name = "Sqr";
	assert(!holds(cable, s, ctx, 0.0));

	// Signal on an unnamed Host Audio input
	Condition sig;
	sig.type = Cond::Signal;
	sig.ref.module = "HostAudio2";
	sig.ref.kind = Ref::INPUT;
	sig.ref.name = "#1";
	sig.value = 0.05;
	assert(holds(sig, s, ctx, 0.0));
	sig.value = 0.5;
	assert(!holds(sig, s, ctx, 0.0));

	// Param range in displayed units
	Condition range;
	range.type = Cond::Param;
	range.ref = inParam;
	range.hasMin = true;
	range.min = 50.0;
	assert(holds(range, s, ctx, 0.0));
	range.hasMax = true;
	range.max = 80.0;
	assert(!holds(range, s, ctx, 0.0));

	// Param moved: the first look records the baseline, then it needs the distance
	Condition moved;
	moved.type = Cond::Param;
	moved.ref = inParam;
	moved.moved = 0.2;
	StepContext mctx;
	RackState t = s;
	assert(!holds(moved, t, mctx, 0.0));
	t.params[0].scaled = 0.9f;
	assert(!holds(moved, t, mctx, 0.1));
	t.params[0].scaled = 0.75f;
	assert(holds(moved, t, mctx, 0.2));
	t.params[0].scaled = 1.f;
	assert(!holds(moved, t, mctx, 0.3));

	// Next and wait
	Condition next;
	next.type = Cond::Next;
	assert(!holds(next, s, ctx, 0.0));
	ctx.nextPressed = true;
	assert(holds(next, s, ctx, 0.0));
	Condition wait;
	wait.type = Cond::Wait;
	wait.value = 2.0;
	assert(!holds(wait, s, ctx, 1.9));
	assert(holds(wait, s, ctx, 2.0));

	// All / any; all evaluates every part (baselines are recorded even when an earlier part fails)
	Condition all;
	all.type = Cond::All;
	all.parts = {side, moved};
	StepContext actx;
	RackState u = s;
	assert(!holds(all, u, actx, 0.0));
	assert(actx.baseline.size() == 1);
	Condition any;
	any.type = Cond::Any;
	any.parts = {side, c};
	assert(!holds(any, s, ctx, 0.0));
	any.parts.push_back(cable);
	cable.ref.name = "Saw";
	any.parts.back() = cable;
	assert(holds(any, s, ctx, 0.0));
}
```

- [ ] **Step 2: Run to see it fail** (only after "build")

Run: `bash src/patchcore/tests/run.sh`
Expected: compile errors: `RackState` / `findModule` / `holds` not declared.

- [ ] **Step 3: Implement the snapshot model and conditions**

Append to `TutorialLogic.hpp` before `} // namespace patchcore_tutorial`:

```cpp
// ---------------------------------------------------------------------------------------------------------
// What the rack looks like right now (built every frame by Tutorial.cpp)

struct ModuleState {
	int64_t id;
	std::string slug;
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

/** The first module of that model (lowest id) */
inline const ModuleState* findModule(const RackState& s, const std::string& slug) {
	const ModuleState* best = nullptr;
	for (const ModuleState& m : s.modules)
		if (m.slug == slug && (best == nullptr || m.id < best->id))
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
```

- [ ] **Step 4: Run the tests** (only after "build")

Run: `bash src/patchcore/tests/run.sh`
Expected: `tutorial logic: all tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/patchcore/TutorialLogic.hpp src/patchcore/tests/test_tutorial.cpp
git commit -m "Tutorials: rack snapshot model and step conditions (tests: not run, no build yet)"
git push
```

---

### Task 3: Step runner, missing modules, Show me actions

**Files:**
- Modify: `src/patchcore/TutorialLogic.hpp` (append)
- Modify: `src/patchcore/tests/test_tutorial.cpp`

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces: `struct Runner` with `start(const Lesson&, double t)`, `update(const RackState&, double t)`,
  `pressNext(const RackState&, double t)`, `back(double t)`, `const Step& current() const`, `bool isTask() const`,
  `bool nextEnabled() const`, `bool needsTurn(const RackState&) const`, fields `int step`, `Phase phase`
  (`ACTIVE`, `DONE`, `FINISHED`), `bool reviewing`, `std::set<std::string> seen`, constants `kHold = 0.3`,
  `kAdvance = 0.8`;
  `std::vector<std::string> goneModules(const Step&, const RackState&, const std::set<std::string>& seen)`;
  `bool patchReplaced(const std::vector<int64_t>& startIds, const RackState&)`;
  `struct Action { enum Type { ADD_MODULE, ADD_CABLE, SET_PARAM, MOVE_PARAM, SET_SIDE }; Type type; std::string module; Ref from, to, param; double value; int side; }`;
  `bool canShowMe(const Step&)`; `std::vector<Action> showMeActions(const Step&, const RackState&)`.

- [ ] **Step 1: Write the failing runner tests**

Add to `test_tutorial.cpp` and call `testRunner(); testShowMe();` from `main`:

```cpp
static Lesson runnerLesson() {
	Lesson l;
	std::string err;
	const bool ok = parseLesson(R"({"title": "t", "steps": [
		{"text": "intro", "done": {"next": true}},
		{"text": "add vca", "done": {"module": "VCA"}},
		{"text": "level", "side": "front", "done": {"param": {"module": "VCA", "param": "Channel 1 level"}, "min": 50, "max": 80}},
		{"text": "end", "done": {"next": true}}
	]})", &l, &err);
	assert(ok);
	return l;
}

static void testRunner() {
	const Lesson l = runnerLesson();
	RackState s = demoRack();
	s.modules.erase(s.modules.begin() + 2);  // no VCA yet
	Runner r;
	r.start(l, 0.0);
	assert(r.step == 0 && r.phase == Runner::ACTIVE && !r.isTask() && r.nextEnabled());

	// Next on an explanation step moves on at once
	r.pressNext(s, 1.0);
	assert(r.step == 1 && r.isTask() && !r.nextEnabled());

	// A task: must hold 0.3 s, then DONE, then the next step 0.8 s later
	RackState withVca = demoRack();
	withVca.params[0].display = 100.f;  // outside 50..80, so step 3 is not skipped
	r.update(withVca, 2.0);
	assert(r.phase == Runner::ACTIVE);
	r.update(s, 2.1);  // VCA gone again: the hold restarts
	r.update(withVca, 2.2);
	r.update(withVca, 2.45);
	assert(r.phase == Runner::ACTIVE);
	r.update(withVca, 2.5);
	assert(r.phase == Runner::DONE && r.step == 1);
	r.update(withVca, 3.0);
	assert(r.step == 1);
	r.update(withVca, 3.3);
	assert(r.step == 2 && r.phase == Runner::ACTIVE);

	// The step needs the front, the rack shows the back
	assert(r.needsTurn(withVca));
	withVca.side = SIDE_FRONT;
	assert(!r.needsTurn(withVca));

	// Sweeping the knob through 50..80 in less than 0.3 s does not count (Review Focus 3)
	withVca.params[0].display = 60.f;
	r.update(withVca, 4.0);
	r.update(withVca, 4.2);
	withVca.params[0].display = 90.f;
	r.update(withVca, 4.25);
	withVca.params[0].display = 60.f;
	r.update(withVca, 4.3);
	r.update(withVca, 4.5);
	assert(r.phase == Runner::ACTIVE);
	r.update(withVca, 4.6);
	assert(r.phase == Runner::DONE);
	r.update(withVca, 5.4);
	assert(r.step == 3);

	// Back to a done task: reviewing, Next enabled, no bounce forward (Review Focus 1)
	r.back(6.0);
	assert(r.step == 2 && r.reviewing && r.nextEnabled());
	r.update(withVca, 6.5);
	r.update(withVca, 8.0);
	assert(r.step == 2 && r.phase == Runner::ACTIVE);
	r.pressNext(withVca, 8.1);
	assert(r.step == 3 && !r.reviewing);

	// Next on the last step finishes the lesson; Back from the summary returns to the last step
	r.pressNext(withVca, 9.0);
	assert(r.phase == Runner::FINISHED);
	r.back(9.5);
	assert(r.phase == Runner::ACTIVE && r.step == 3 && r.reviewing);

	// Advancing skips a task that already holds
	Runner r2;
	r2.start(l, 0.0);
	RackState done = demoRack();
	done.params[0].display = 60.f;
	r2.pressNext(done, 0.1);
	assert(r2.step == 3);

	// Modules the user deleted after they were in the rack
	Runner r3;
	r3.start(l, 0.0);
	r3.update(demoRack(), 0.0);
	RackState noVca = demoRack();
	noVca.modules.erase(noVca.modules.begin() + 2);
	std::vector<std::string> gone = goneModules(l.steps[2], noVca, r3.seen);
	assert(gone.size() == 1 && gone[0] == "VCA");
	std::set<std::string> neverSeen;
	assert(goneModules(l.steps[1], noVca, neverSeen).empty());

	// Another patch loaded (Review Focus 2)
	assert(!patchReplaced({40, 50}, demoRack()));
	RackState other;
	other.modules = {{99, "VCO"}};
	assert(patchReplaced({40, 50}, other));
	assert(!patchReplaced({}, other));
}

static void testShowMe() {
	Lesson l;
	std::string err;
	const bool ok = parseLesson(R"({"title": "t", "steps": [
		{"text": "a", "done": {"next": true}},
		{"text": "b", "side": "back", "done": {"all": [
			{"cable": {"from": {"module": "LFO", "output": "Tri"}, "to": {"module": "VCA", "input": "In 1"}}},
			{"signal": {"module": "HostAudio2", "input": "#1"}, "above": 0.05}]}},
		{"text": "c", "done": {"param": {"module": "VCA", "param": "Channel 1 level"}, "min": 20, "max": 40}},
		{"text": "d", "done": {"param": {"module": "VCA", "param": "Channel 1 level"}, "moved": 0.2}},
		{"text": "e", "done": {"cable": {"from": {"module": "VCO", "output": "Saw"}}}},
		{"text": "f", "done": {"module": "VCO"}, "showMe": "none"}
	]})", &l, &err);
	assert(ok);
	RackState s = demoRack();
	s.side = SIDE_FRONT;

	assert(!canShowMe(l.steps[0]));
	assert(canShowMe(l.steps[1]));
	assert(canShowMe(l.steps[2]) && canShowMe(l.steps[3]));
	assert(!canShowMe(l.steps[4]));  // a cable without both ends cannot be made
	assert(!canShowMe(l.steps[5]));  // showMe: none

	// Turn the rack, add the missing LFO, then the cable (the signal part has nothing to do)
	std::vector<Action> a = showMeActions(l.steps[1], s);
	assert(a.size() == 3);
	assert(a[0].type == Action::SET_SIDE && a[0].side == SIDE_BACK);
	assert(a[1].type == Action::ADD_MODULE && a[1].module == "LFO");
	assert(a[2].type == Action::ADD_CABLE && a[2].from.name == "Tri" && a[2].to.name == "In 1");

	a = showMeActions(l.steps[2], s);
	assert(a.size() == 1 && a[0].type == Action::SET_PARAM && a[0].value == 30.0);
	a = showMeActions(l.steps[3], s);
	assert(a.size() == 1 && a[0].type == Action::MOVE_PARAM && a[0].value > 0.2);
}
```

- [ ] **Step 2: Run to see it fail** (only after "build")

Run: `bash src/patchcore/tests/run.sh`
Expected: compile errors: `Runner`, `goneModules`, `patchReplaced`, `canShowMe`, `showMeActions` not declared.

- [ ] **Step 3: Implement**

Append to `TutorialLogic.hpp` before the closing namespace:

```cpp
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

	void start(const Lesson& l, double t) {
		lesson = &l;
		step = 0;
		phase = ACTIVE;
		reviewing = false;
		seen.clear();
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
				return;
			}
			++step;
			enter(t);
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

/** None of the lesson's start modules is left: the user opened another patch */
inline bool patchReplaced(const std::vector<int64_t>& startIds, const RackState& s) {
	if (startIds.empty())
		return false;
	for (int64_t id : startIds)
		for (const ModuleState& m : s.modules)
			if (m.id == id)
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
```

- [ ] **Step 4: Run the tests** (only after "build")

Run: `bash src/patchcore/tests/run.sh`
Expected: `tutorial logic: all tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/patchcore/TutorialLogic.hpp src/patchcore/tests/test_tutorial.cpp
git commit -m "Tutorials: step runner, gone modules, Show me actions (tests: not run, no build yet)"
git push
```

---

### Task 4: The three lessons and the content check

**Files:**
- Create: `plugins/JXMP/res/tutorials/index.json`
- Create: `plugins/JXMP/res/tutorials/01-first-sound/lesson.json`
- Create: `plugins/JXMP/res/tutorials/02-filter-envelope/lesson.json`
- Create: `plugins/JXMP/res/tutorials/03-modulation/lesson.json`
- Create: `plugins/JXMP/tests/make_tutorial_patches.py`
- Create (generated): `plugins/JXMP/res/tutorials/0{1,2,3}-*/start.vcv`
- Create: `src/patchcore/tests/test_tutorial_content.cpp`
- Modify: `plugins/Makefile` (after `RESOURCE_FILES += Meander/res`)

**Interfaces:**
- Consumes: `parseIndex`, `parseLesson`, `Lesson`, `Step`, `Condition`, `Ref` (Task 1); `collectModules` (Task 3).
- Produces: lesson ids `01-first-sound`, `02-filter-envelope`, `03-modulation`, used by Task 6's menu.

- [ ] **Step 1: Write the failing content test**

Create `src/patchcore/tests/test_tutorial_content.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to see it fail** (only after "build")

Run: `bash src/patchcore/tests/run.sh`
Expected: `FAIL: index.json: not valid JSON` (the files do not exist yet), exit code 1.

- [ ] **Step 3: Write the index**

Create `plugins/JXMP/res/tutorials/index.json`:

```json
{
  "lessons": [
    {"id": "01-first-sound", "title": "1 · First sound",
     "description": "Add modules, patch cables, hear and see an oscillator."},
    {"id": "02-filter-envelope", "title": "2 · Filter and envelope",
     "description": "Shape a sound: filter, gates and the ADSR envelope."},
    {"id": "03-modulation", "title": "3 · Modulation",
     "description": "Let an LFO turn the knobs: CV, vibrato and trills."}
  ]
}
```

- [ ] **Step 4: Write lesson 1**

Create `plugins/JXMP/res/tutorials/01-first-sound/lesson.json`:

```json
{
  "title": "First sound",
  "startSide": "front",
  "learned": [
    "Adding modules with the quick search",
    "Turning the rack around with Tab",
    "Patching cables from outputs to inputs",
    "Oscillator: pitch and wave shape",
    "Amplifier: volume",
    "Seeing a wave on the scope"
  ],
  "steps": [
    {"text": "Welcome! A modular synth is built from modules. Each module makes or changes a voltage, and cables carry the voltages from module to module. At the end of this lesson you will hear a sound you patched yourself.",
     "done": {"next": true}},
    {"text": "First a sound source: an oscillator. Right-click an empty spot in the rack (or press Enter), type VCO and press Enter.",
     "done": {"module": "VCO"}},
    {"text": "Now an amplifier, the same way: right-click an empty spot, type VCA, press Enter. It will set how loud the sound is.",
     "done": {"module": "VCA"}},
    {"text": "The jacks of JXMP modules are on the back of the rack, like on real studio gear. Press Tab to turn the rack around.",
     "done": {"side": "back"}},
    {"text": "Drag a cable from SAW of the oscillator to IN 1 of the amplifier. The oscillator now sends its sawtooth wave into the amplifier.",
     "side": "back",
     "highlight": [{"module": "VCO", "output": "Saw"}, {"module": "VCA", "input": "In 1"}],
     "done": {"cable": {"from": {"module": "VCO", "output": "Saw"}, "to": {"module": "VCA", "input": "In 1"}}}},
    {"text": "Drag a cable from OUT 1 of the amplifier to jack 1 of the patchbay. The patchbay is the way out of the rack: its front is wired to your speakers. You should hear a buzzing tone now.",
     "side": "back",
     "highlight": [{"module": "VCA", "output": "Out 1"}, {"module": "Patchbay4", "input": "Ch 1", "side": "back"}],
     "done": {"all": [
       {"cable": {"from": {"module": "VCA", "output": "Out 1"}, "to": {"module": "Patchbay4", "input": "Ch 1", "side": "back"}}},
       {"signal": {"module": "HostAudio2", "input": "#1"}, "above": 0.05}]}},
    {"text": "Press Tab to turn the rack back to the front, then turn LEVEL 1 of the amplifier. The amplifier sets the volume: down is quiet, up is loud.",
     "side": "front",
     "highlight": [{"module": "VCA", "param": "Channel 1 level"}],
     "done": {"param": {"module": "VCA", "param": "Channel 1 level"}, "moved": 0.2}},
    {"text": "Turn the FREQUENCY knob of the oscillator. The frequency is the pitch: how high or low the tone is.",
     "side": "front",
     "highlight": [{"module": "VCO", "param": "Frequency"}],
     "done": {"param": {"module": "VCO", "param": "Frequency"}, "moved": 0.1}},
    {"text": "Press Tab and drag a cable from SQR of the oscillator to IN 1 of the amplifier. An input holds one cable, so the old one goes. A square wave sounds hollow, a sawtooth bright.",
     "side": "back",
     "highlight": [{"module": "VCO", "output": "Sqr"}, {"module": "VCA", "input": "In 1"}],
     "done": {"cable": {"from": {"module": "VCO", "output": "Sqr"}, "to": {"module": "VCA", "input": "In 1"}}}},
    {"text": "Now patch SIN to IN 1. The sine is the purest tone, with no overtones at all. The shape of the wave is what you hear as the timbre.",
     "side": "back",
     "highlight": [{"module": "VCO", "output": "Sin"}, {"module": "VCA", "input": "In 1"}],
     "done": {"cable": {"from": {"module": "VCO", "output": "Sin"}, "to": {"module": "VCA", "input": "In 1"}}}},
    {"text": "Let's see the wave. Add a scope (type Scope) and drag a second cable from SIN of the oscillator to IN 1 of the scope. One output can feed many inputs.",
     "side": "back",
     "highlight": [{"module": "VCO", "output": "Sin"}],
     "done": {"all": [
       {"module": "Scope"},
       {"cable": {"from": {"module": "VCO", "output": "Sin"}, "to": {"module": "Scope", "input": "In 1"}}}]}},
    {"text": "That's a patch! The oscillator makes the sound, the amplifier sets its volume, and cables carry voltages from outputs to inputs. Press Next to finish.",
     "done": {"next": true}}
  ]
}
```

- [ ] **Step 5: Write lesson 2**

Create `plugins/JXMP/res/tutorials/02-filter-envelope/lesson.json`:

```json
{
  "title": "Filter and envelope",
  "startSide": "back",
  "learned": [
    "Subtractive synthesis: oscillator, filter, amplifier",
    "Cutoff and resonance",
    "Gates and the ADSR envelope",
    "One output driving several inputs"
  ],
  "steps": [
    {"text": "Subtractive synthesis: start with a bright, buzzing wave and take away what you don't want with a filter. Most classic synths work like this. Everything for this lesson is already in the rack.",
     "done": {"next": true}},
    {"text": "Patch the voice: SAW of the oscillator to IN of the filter, OUT of the filter to IN 1 of the amplifier, and OUT 1 of the amplifier to jack 1 of the patchbay.",
     "side": "back",
     "highlight": [{"module": "VCO", "output": "Saw"}, {"module": "VCF", "input": "In"}, {"module": "VCF", "output": "Out"},
                   {"module": "VCA", "input": "In 1"}, {"module": "VCA", "output": "Out 1"}, {"module": "Patchbay4", "input": "Ch 1", "side": "back"}],
     "done": {"all": [
       {"cable": {"from": {"module": "VCO", "output": "Saw"}, "to": {"module": "VCF", "input": "In"}}},
       {"cable": {"from": {"module": "VCF", "output": "Out"}, "to": {"module": "VCA", "input": "In 1"}}},
       {"cable": {"from": {"module": "VCA", "output": "Out 1"}, "to": {"module": "Patchbay4", "input": "Ch 1", "side": "back"}}},
       {"signal": {"module": "HostAudio2", "input": "#1"}, "above": 0.05}]}},
    {"text": "Press Tab and sweep the CUTOFF knob of the filter down and up. Down, the filter takes away the highs and the sound gets dark; up, everything passes.",
     "side": "front",
     "highlight": [{"module": "VCF", "param": "Cutoff frequency"}],
     "done": {"param": {"module": "VCF", "param": "Cutoff frequency"}, "moved": 0.3}},
    {"text": "Turn RESONANCE past the middle. The filter now boosts the frequencies right at the cutoff and starts to sing. Sweep CUTOFF again to hear it.",
     "side": "front",
     "highlight": [{"module": "VCF", "param": "Resonance"}],
     "done": {"param": {"module": "VCF", "param": "Resonance"}, "min": 50}},
    {"text": "So far the sound never stops. Press Tab and patch BEAT of the clock to GATE of the envelope. A gate is like a key: high while the key is held down.",
     "side": "back",
     "highlight": [{"module": "Clock", "output": "Beat"}, {"module": "ADSR", "input": "Gate"}],
     "done": {"cable": {"from": {"module": "Clock", "output": "Beat"}, "to": {"module": "ADSR", "input": "Gate"}}}},
    {"text": "Patch ENV of the envelope to CV 1 of the amplifier. The envelope now opens the amplifier on every beat: the drone turns into notes.",
     "side": "back",
     "highlight": [{"module": "ADSR", "output": "Env"}, {"module": "VCA", "input": "CV 1"}],
     "done": {"cable": {"from": {"module": "ADSR", "output": "Env"}, "to": {"module": "VCA", "input": "CV 1"}}}},
    {"text": "Press Tab and turn ATTACK and RELEASE of the envelope. Attack is how fast a note fades in, release how slowly it fades out after the gate ends.",
     "side": "front",
     "highlight": [{"module": "ADSR", "param": "Attack"}, {"module": "ADSR", "param": "Release"}],
     "done": {"all": [
       {"param": {"module": "ADSR", "param": "Attack"}, "moved": 0.2},
       {"param": {"module": "ADSR", "param": "Release"}, "moved": 0.2}]}},
    {"text": "Press Tab and patch a second cable from ENV of the envelope to IN 1 of the scope, to see the envelope's shape.",
     "side": "back",
     "highlight": [{"module": "ADSR", "output": "Env"}, {"module": "Scope", "input": "In 1"}],
     "done": {"cable": {"from": {"module": "ADSR", "output": "Env"}, "to": {"module": "Scope", "input": "In 1"}}}},
    {"text": "Press Tab and turn DECAY and SUSTAIN while you watch the scope. After the attack the level falls (decay) to the sustain level and stays there while the gate is high.",
     "side": "front",
     "highlight": [{"module": "ADSR", "param": "Decay"}, {"module": "ADSR", "param": "Sustain"}],
     "done": {"all": [
       {"param": {"module": "ADSR", "param": "Decay"}, "moved": 0.2},
       {"param": {"module": "ADSR", "param": "Sustain"}, "moved": 0.2}]}},
    {"text": "One envelope can drive many things. Press Tab and patch another cable from ENV of the envelope to ENV of the filter.",
     "side": "back",
     "highlight": [{"module": "ADSR", "output": "Env"}, {"module": "VCF", "input": "Env"}],
     "done": {"cable": {"from": {"module": "ADSR", "output": "Env"}, "to": {"module": "VCF", "input": "Env"}}}},
    {"text": "Press Tab and turn up ENV AMOUNT of the filter. Every note now opens the filter and closes it again: the classic synth 'wow'.",
     "side": "front",
     "highlight": [{"module": "VCF", "param": "Envelope amount"}],
     "done": {"param": {"module": "VCF", "param": "Envelope amount"}, "min": 30}},
    {"text": "Oscillator, filter, amplifier, and an envelope that shapes both the volume and the colour: that is the classic synth voice. Press Next to finish.",
     "done": {"next": true}}
  ]
}
```

- [ ] **Step 6: Write lesson 3**

Create `plugins/JXMP/res/tutorials/03-modulation/lesson.json`:

```json
{
  "title": "Modulation",
  "startSide": "back",
  "learned": [
    "CV: voltages that turn knobs for you",
    "An LFO on the filter and on the pitch",
    "Attenuverter: scale and flip a voltage",
    "A square LFO for a trill"
  ],
  "steps": [
    {"text": "A control voltage (CV) is a voltage that turns a knob for you. Audio wiggles hundreds of times a second; CV usually moves slowly. In this lesson an LFO, a slow oscillator, moves other modules.",
     "done": {"next": true}},
    {"text": "Patch TRI of the LFO to FREQ CV of the filter.",
     "side": "back",
     "highlight": [{"module": "LFO", "output": "Tri"}, {"module": "VCF", "input": "Freq CV"}],
     "done": {"cable": {"from": {"module": "LFO", "output": "Tri"}, "to": {"module": "VCF", "input": "Freq CV"}}}},
    {"text": "Press Tab and turn up CUTOFF CV of the filter. That knob sets how far the LFO moves the cutoff: the filter now breathes.",
     "side": "front",
     "highlight": [{"module": "VCF", "param": "Cutoff CV"}],
     "done": {"param": {"module": "VCF", "param": "Cutoff CV"}, "min": 30}},
    {"text": "Turn RATE of the LFO. Slow, it is a sweep; fast, it becomes a wobble.",
     "side": "front",
     "highlight": [{"module": "LFO", "param": "Rate"}],
     "done": {"param": {"module": "LFO", "param": "Rate"}, "moved": 0.2}},
    {"text": "Now the pitch. Press Tab and patch SIN of the LFO to FM of the oscillator.",
     "side": "back",
     "highlight": [{"module": "LFO", "output": "Sin"}, {"module": "VCO", "input": "FM"}],
     "done": {"cable": {"from": {"module": "LFO", "output": "Sin"}, "to": {"module": "VCO", "input": "FM"}}}},
    {"text": "Press Tab and turn FM of the oscillator up just a little: vibrato. Too much and the pitch swings wildly.",
     "side": "front",
     "highlight": [{"module": "VCO", "param": "Frequency modulation"}],
     "done": {"param": {"module": "VCO", "param": "Frequency modulation"}, "min": 3, "max": 30}},
    {"text": "An attenuverter scales and flips a voltage. Press Tab, patch SIN of the LFO to IN 1 of the attenuverter, and OUT 1 of the attenuverter to FM of the oscillator (it replaces the old cable).",
     "side": "back",
     "highlight": [{"module": "LFO", "output": "Sin"}, {"module": "Attenuverter", "input": "In 1"},
                   {"module": "Attenuverter", "output": "Out 1"}, {"module": "VCO", "input": "FM"}],
     "done": {"all": [
       {"cable": {"from": {"module": "LFO", "output": "Sin"}, "to": {"module": "Attenuverter", "input": "In 1"}}},
       {"cable": {"from": {"module": "Attenuverter", "output": "Out 1"}, "to": {"module": "VCO", "input": "FM"}}}]}},
    {"text": "Press Tab and turn GAIN 1 of the attenuverter. Left of the middle the vibrato is upside down; at the middle it stops.",
     "side": "front",
     "highlight": [{"module": "Attenuverter", "param": "Row 1 gain"}],
     "done": {"param": {"module": "Attenuverter", "param": "Row 1 gain"}, "moved": 0.1}},
    {"text": "Press Tab and patch SQR of the LFO to IN 1 of the attenuverter; the old cable goes.",
     "side": "back",
     "highlight": [{"module": "LFO", "output": "Sqr"}, {"module": "Attenuverter", "input": "In 1"}],
     "done": {"cable": {"from": {"module": "LFO", "output": "Sqr"}, "to": {"module": "Attenuverter", "input": "In 1"}}}},
    {"text": "Press Tab and turn GAIN 1 up. A square wave jumps between two voltages, so the pitch jumps between two notes: a trill.",
     "side": "front",
     "highlight": [{"module": "Attenuverter", "param": "Row 1 gain"}],
     "done": {"param": {"module": "Attenuverter", "param": "Row 1 gain"}, "min": 1.5}},
    {"text": "Compare the two worlds on the scope: press Tab, patch SIN of the LFO to IN 1 and OUT 1 of the amplifier to IN 2.",
     "side": "back",
     "highlight": [{"module": "LFO", "output": "Sin"}, {"module": "Scope", "input": "In 1"},
                   {"module": "VCA", "output": "Out 1"}, {"module": "Scope", "input": "In 2"}],
     "done": {"all": [
       {"cable": {"from": {"module": "LFO", "output": "Sin"}, "to": {"module": "Scope", "input": "In 1"}}},
       {"cable": {"from": {"module": "VCA", "output": "Out 1"}, "to": {"module": "Scope", "input": "In 2"}}}]}},
    {"text": "Audio and CV are the same thing: voltages, only at different speeds. Any output can drive any input, so try your own connections. Press Next to finish.",
     "done": {"next": true}}
  ]
}
```

- [ ] **Step 7: Write the start-patch generator and generate**

Create `plugins/JXMP/tests/make_tutorial_patches.py`:

```python
#!/usr/bin/env python3
"""Writes the start.vcv of each tutorial lesson (plugins/JXMP/res/tutorials/*/start.vcv).
Port and param ids follow the enums in plugins/JXMP/src (comments name them). Positions are [HP, row].
Run from anywhere: python3 plugins/JXMP/tests/make_tutorial_patches.py"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "tutorials")
COLORS = ["#f0a030", "#5b6ee1", "#3cb46e", "#e04e4e", "#9a6bd6", "#ee7b1f", "#3a8dde", "#f2c230"]


def module(mid, plugin, model, x, row=0, params=None):
    return {"id": mid, "plugin": plugin, "model": model, "version": "2.0.0",
            "params": [{"id": k, "value": v} for k, v in (params or {}).items()], "pos": [x, row]}


def patch(modules, cables):
    out = []
    for i, (om, oid, im, iid) in enumerate(cables):
        out.append({"id": i + 1, "outputModuleId": om, "outputId": oid, "inputModuleId": im, "inputId": iid,
                    "color": COLORS[i % len(COLORS)]})
    return {"version": "2.6.0", "zoom": 1.0, "gridOffset": [-1, 0], "modules": modules, "cables": out}


# Ids
PB, HA, CLK, VCO, VCF, ADSR, VCA, SCP, LFO, ATT = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
# Patchbay4: FRONT_INPUT 0-3, BACK_INPUT 4-7; FRONT_OUTPUT 0-3, BACK_OUTPUT 4-7
PB_FRONT_OUT_1, PB_BACK_IN_1 = 0, 4
# Host Audio level param 0: 0.5 = about -12 dB (quiet start)
HOST = {0: 0.5}
OUT_CABLE = (PB, PB_FRONT_OUT_1, HA, 0)  # patchbay front Ch 1 -> Host Audio input 1 (mono to both speakers)

lesson1 = patch([module(PB, "JXMP", "Patchbay4", 0), module(HA, "Cardinal", "HostAudio2", 4, params=HOST)],
                [OUT_CABLE])

voice = [
    module(CLK, "JXMP", "Clock", 0, params={0: 100, 1: 1}),  # TEMPO 100 BPM, RUN on
    module(VCO, "JXMP", "VCO", 12),
    module(VCF, "JXMP", "VCF", 30),
    module(ADSR, "JXMP", "ADSR", 44),
    module(VCA, "JXMP", "VCA", 58),
    module(SCP, "JXMP", "Scope", 66),
    module(PB, "JXMP", "Patchbay4", 90),
    module(HA, "Cardinal", "HostAudio2", 94, params=HOST),
]
lesson2 = patch(voice, [OUT_CABLE])

# Lesson 3 starts from the finished lesson 2: resonance and envelope amount up
voice3 = [dict(m) for m in voice]
voice3[2] = module(VCF, "JXMP", "VCF", 30, params={1: 0.5, 9: 0.5})  # RES_PARAM 1, ENV_AMT_PARAM 9
voice3 += [module(LFO, "JXMP", "LFO", 0, row=1), module(ATT, "JXMP", "Attenuverter", 10, row=1)]
lesson3 = patch(voice3, [
    OUT_CABLE,
    (VCO, 2, VCF, 0),           # VCO SAW_OUTPUT 2 -> VCF IN_INPUT 0
    (VCF, 0, VCA, 0),           # VCF OUT_OUTPUT 0 -> VCA IN1_INPUT 0
    (VCA, 0, PB, PB_BACK_IN_1),  # VCA OUT1_OUTPUT 0 -> Patchbay back Ch 1
    (CLK, 2, ADSR, 0),          # Clock DIV_OUTPUT+2 "Beat" -> ADSR GATE_INPUT 0
    (ADSR, 0, VCA, 1),          # ADSR ENV_OUTPUT 0 -> VCA CV1_INPUT 1
    (ADSR, 0, SCP, 0),          # ADSR ENV -> Scope IN1_INPUT 0
    (ADSR, 0, VCF, 2),          # ADSR ENV -> VCF ENV_INPUT 2
])

for name, data in [("01-first-sound", lesson1), ("02-filter-envelope", lesson2), ("03-modulation", lesson3)]:
    path = os.path.join(ROOT, name, "start.vcv")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=1)
        f.write("\n")
    print("wrote", path)
```

Run (plain file generation, allowed before "build"): `python3 plugins/JXMP/tests/make_tutorial_patches.py`
Expected: three `wrote …/start.vcv` lines.

- [ ] **Step 8: Bundle the folder**

In `plugins/Makefile`, after the line `RESOURCE_FILES += Meander/res`, add:

```make
# PatchCore: tutorial lessons (research_docs/15)
RESOURCE_FILES += JXMP/res/tutorials
```

- [ ] **Step 9: Run the content check** (only after "build")

Run: `bash src/patchcore/tests/run.sh`
Expected: `tutorial content: all lessons check out`. If a name does not match, fix the lesson text/reference, not the
module (the module's name is the truth).

- [ ] **Step 10: Commit**

```bash
git add plugins/JXMP/res/tutorials plugins/JXMP/tests/make_tutorial_patches.py plugins/Makefile src/patchcore/tests/test_tutorial_content.cpp
git commit -m "Tutorials: lessons 1-3, start patches, content check (tests: not run, no build yet)"
git push
```

---

### Task 5: Tutorial engine in the app (snapshot, panel, highlight, Show me)

**Files:**
- Create: `src/patchcore/Tutorial.cpp`
- Modify: `src/patchcore/PatchCore.hpp` (declarations + settings fields)
- Modify: `src/patchcore/PatchCore.cpp` (settings load/save)
- Modify: `src/CardinalCommon.hpp:61`, `src/CardinalCommon.cpp:1011-1032` (`afterLoad` callback)
- Modify: `src/override/Scene.cpp:160` (add the layer)
- Modify: `src/Makefile:59` (compile the file)

**Interfaces:**
- Consumes: everything in `TutorialLogic.hpp`; `addModelAt(plugin::Model*, math::Vec)` (`src/custom/Browser.cpp`);
  `isBackView()`, `setViewSide(ViewSide)`, `portSide(app::PortWidget*)`, `theme()`, `uiFont()`, `labelFont()`,
  `draw::bevelBox`, `draw::text`, `mixerShown()`, `audioNotice()`.
- Produces (namespace `rack::patchcore`): `struct TutorialInfo { std::string id, title, description; bool available; bool done; }`;
  `std::vector<TutorialInfo> tutorialList()`; `void openTutorial(const std::string& id)`;
  `widget::Widget* createTutorialLayer()`; settings fields `std::vector<std::string> tutorialsDone`,
  `bool tutorialOffered`.

- [ ] **Step 1: Settings and declarations**

In `src/patchcore/PatchCore.hpp`, add `#include <vector>` at the top if missing, and inside `struct Settings` after
`int wheelSensitivity = 0;`:

```cpp
	/** Ids of finished tutorial lessons (research_docs/15) */
	std::vector<std::string> tutorialsDone;
	/** The "New to modular?" offer was shown once */
	bool tutorialOffered = false;
```

After `widget::Widget* createNoticeStrip();` add:

```cpp
/** Interactive tutorials (Tutorial.cpp, research_docs/15) */
struct TutorialInfo {
	std::string id, title, description;
	/** lesson.json parses */
	bool available;
	bool done;
};
std::vector<TutorialInfo> tutorialList();
/** Asks to save unsaved work, opens the lesson's start patch untitled and starts the lesson */
void openTutorial(const std::string& id);
/** Full-scene layer: step panel, highlight, first-start offer */
widget::Widget* createTutorialLayer();
```

(`audioNotice()` is already declared in `PatchCore.hpp`; `Tutorial.cpp` uses it for the offer's position.)

In `src/patchcore/PatchCore.cpp` `loadSettings()`, before `json_decref(rootJ);`:

```cpp
	if (json_t* const j = json_object_get(rootJ, "tutorialsDone")) {
		settings.tutorialsDone.clear();
		for (size_t i = 0; i < json_array_size(j); ++i)
			if (json_is_string(json_array_get(j, i)))
				settings.tutorialsDone.push_back(json_string_value(json_array_get(j, i)));
	}
	if (json_t* const j = json_object_get(rootJ, "tutorialOffered"))
		settings.tutorialOffered = json_boolean_value(j);
```

In `saveSettings()`, before `const std::string path = settingsPath();`:

```cpp
	json_t* const doneJ = json_array();
	for (const std::string& id : settings.tutorialsDone)
		json_array_append_new(doneJ, json_string(id.c_str()));
	json_object_set_new(rootJ, "tutorialsDone", doneJ);
	json_object_set_new(rootJ, "tutorialOffered", json_boolean(settings.tutorialOffered));
```

- [ ] **Step 2: `loadPathDialog` with a callback**

In `src/CardinalCommon.hpp` replace line 61 and make sure `<functional>` is included at the top:

```cpp
void loadPathDialog(const std::string& path, bool asTemplate = false, std::function<void()> afterLoad = nullptr);
```

In `src/CardinalCommon.cpp` replace the function (lines 1011–1032):

```cpp
void loadPathDialog(const std::string& path, const bool asTemplate, std::function<void()> afterLoad)
{
#ifndef HEADLESS_BEHAVIOUR
    promptClear("The current patch is unsaved. Clear it and open the new patch?", [path, asTemplate, afterLoad]() {
        APP->patch->loadAction(path);

        if (asTemplate)
        {
            APP->patch->path = "";
            APP->history->setSaved();
        }

       #ifdef DISTRHO_OS_WASM
        syncfs();
       #endif

        if (remoteUtils::RemoteDetails* const remoteDetails = remoteUtils::getRemote())
            if (remoteDetails->autoDeploy)
                remoteUtils::sendFullPatchToRemote(remoteDetails);

        // PatchCore: e.g. the tutorial starts its lesson once the start patch is in
        if (afterLoad)
            afterLoad();
    });
#endif
}
```

- [ ] **Step 3: Write `src/patchcore/Tutorial.cpp`**

```cpp
/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Interactive tutorials (research_docs/15): the step panel, the highlight around the target, Show me, and the
// one-time "New to modular?" offer. The lesson logic is in TutorialLogic.hpp; this file only reads the rack into
// a RackState every frame and acts on it.

#include <app/CableWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/ParamWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/RackScrollWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <engine/Engine.hpp>
#include <helpers.hpp>
#include <history.hpp>
#include <logger.hpp>
#include <patch.hpp>
#include <plugin.hpp>
#include <string.hpp>
#include <system.hpp>
#include <widget/OpaqueWidget.hpp>
#include <window/Window.hpp>

#include <fstream>
#include <map>
#include <sstream>

#include "../CardinalCommon.hpp"
#include "../patchcore/PatchCore.hpp"
#include <patchcore/Mixer.hpp>

#include "TutorialLogic.hpp"

namespace rack {
namespace patchcore {

namespace tu = ::patchcore_tutorial;


static std::string tutorialsDir() {
	plugin::Plugin* const p = plugin::getPlugin("JXMP");
	return p ? system::join(p->path, "res/tutorials") : "";
}

static std::string readFile(const std::string& path) {
	std::ifstream f(path.c_str());
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static bool loadLesson(const std::string& id, tu::Lesson* out, std::string* error) {
	return tu::parseLesson(readFile(system::join(tutorialsDir(), id, "lesson.json")), out, error);
}

static bool isDone(const std::string& id) {
	for (const std::string& d : settings.tutorialsDone)
		if (d == id)
			return true;
	return false;
}


std::vector<TutorialInfo> tutorialList() {
	std::vector<TutorialInfo> list;
	std::vector<tu::IndexEntry> index;
	std::string error;
	if (!tu::parseIndex(readFile(system::join(tutorialsDir(), "index.json")), &index, &error)) {
		WARN("PatchCore tutorials: index.json: %s", error.c_str());
		return list;
	}
	for (const tu::IndexEntry& e : index) {
		tu::Lesson l;
		TutorialInfo info;
		info.id = e.id;
		info.title = e.title;
		info.description = e.description;
		info.available = loadLesson(e.id, &l, &error);
		if (!info.available)
			WARN("PatchCore tutorials: %s: %s", e.id.c_str(), error.c_str());
		info.done = isDone(e.id);
		list.push_back(info);
	}
	return list;
}


// ---------------------------------------------------------------------------------------------------------
// Rack snapshot

struct PeakHold {
	float peak = 0.f;
	double at = 0.0;
};

static float portPeak(engine::Port& port, PeakHold& hold, double t) {
	float v = 0.f;
	for (int c = 0; c < port.getChannels(); ++c)
		v = std::max(v, std::fabs(port.getVoltage(c)));
	if (v >= hold.peak || t - hold.at > tu::Runner::kHold) {
		hold.peak = v;
		hold.at = t;
	}
	return hold.peak;
}

static int sideOf(app::ModuleWidget* mw, bool output, int index) {
	if (mw == nullptr)
		return tu::SIDE_FRONT;
	app::PortWidget* const pw = output ? mw->getOutput(index) : mw->getInput(index);
	return pw != nullptr && portSide(pw) == SIDE_BACK ? tu::SIDE_BACK : tu::SIDE_FRONT;
}

static tu::RackState snapshot(std::map<std::string, PeakHold>& peaks, double t) {
	tu::RackState s;
	s.side = isBackView() ? tu::SIDE_BACK : tu::SIDE_FRONT;
	for (int64_t id : APP->engine->getModuleIds()) {
		engine::Module* const m = APP->engine->getModule(id);
		if (m == nullptr || m->model == nullptr)
			continue;
		tu::ModuleState ms;
		ms.id = id;
		ms.slug = m->model->slug;
		s.modules.push_back(ms);
		app::ModuleWidget* const mw = APP->scene->rack->getModule(id);
		for (int out = 0; out < 2; ++out) {
			std::vector<engine::Port>& ports = out ? m->outputs : m->inputs;
			for (int i = 0; i < (int) ports.size(); ++i) {
				engine::PortInfo* const info = out ? m->outputInfos[i] : m->inputInfos[i];
				tu::PortState p;
				p.moduleId = id;
				p.output = out != 0;
				p.index = i;
				p.name = info ? info->name : "";
				p.side = sideOf(mw, p.output, i);
				p.peak = portPeak(ports[i], peaks[std::to_string(id) + (out ? "/o" : "/i") + std::to_string(i)], t);
				s.ports.push_back(p);
			}
		}
		for (int i = 0; i < (int) m->paramQuantities.size(); ++i) {
			engine::ParamQuantity* const pq = m->paramQuantities[i];
			if (pq == nullptr)
				continue;
			tu::ParamState p;
			p.moduleId = id;
			p.index = i;
			p.name = pq->name;
			p.display = pq->getDisplayValue();
			p.scaled = pq->getScaledValue();
			s.params.push_back(p);
		}
	}
	for (int64_t cid : APP->engine->getCableIds()) {
		engine::Cable* const c = APP->engine->getCable(cid);
		if (c == nullptr || c->inputModule == nullptr || c->outputModule == nullptr)
			continue;
		tu::CableState k;
		k.outModule = c->outputModule->id;
		k.outIndex = c->outputId;
		k.inModule = c->inputModule->id;
		k.inIndex = c->inputId;
		s.cables.push_back(k);
	}
	return s;
}


// ---------------------------------------------------------------------------------------------------------
// From references to widgets, and Show me

static widget::Widget* refWidget(const tu::Ref& r, const tu::RackState& s) {
	const tu::ModuleState* const m = tu::findModule(s, r.module);
	if (m == nullptr)
		return nullptr;
	app::ModuleWidget* const mw = APP->scene->rack->getModule(m->id);
	if (mw == nullptr)
		return nullptr;
	if (r.kind == tu::Ref::MODULE)
		return mw;
	if (r.kind == tu::Ref::PARAM) {
		const tu::ParamState* const p = tu::findParam(s, r);
		return p ? mw->getParam(p->index) : nullptr;
	}
	const tu::PortState* const p = tu::findPort(s, r);
	if (p == nullptr)
		return nullptr;
	return p->output ? (widget::Widget*) mw->getOutput(p->index) : (widget::Widget*) mw->getInput(p->index);
}

static plugin::Model* findModel(const std::string& slug) {
	if (plugin::Model* const m = plugin::getModel("JXMP", slug))
		return m;
	return plugin::getModel("Cardinal", slug);
}

/** Right of the rightmost module, in the same row */
static math::Vec freeSpot() {
	math::Vec pos(0.f, 0.f);
	for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
		if (mw->box.getRight() > pos.x)
			pos = math::Vec(mw->box.getRight() + RACK_GRID_WIDTH, mw->box.pos.y);
	}
	return pos;
}

static void perform(const tu::Action& a, std::map<std::string, PeakHold>& peaks, double t) {
	// Each action reads the rack again: a module added by the previous action must be found
	const tu::RackState s = snapshot(peaks, t);
	app::RackWidget* const rack = APP->scene->rack;
	switch (a.type) {
		case tu::Action::SET_SIDE:
			setViewSide(a.side == tu::SIDE_BACK ? VIEW_BACK : VIEW_FRONT);
			break;
		case tu::Action::ADD_MODULE:
			if (plugin::Model* const model = findModel(a.module))
				addModelAt(model, freeSpot());  // pushes its own history action
			break;
		case tu::Action::ADD_CABLE: {
			const tu::PortState* const from = tu::findPort(s, a.from);
			const tu::PortState* const to = tu::findPort(s, a.to);
			app::ModuleWidget* const outMw = from ? rack->getModule(from->moduleId) : nullptr;
			app::ModuleWidget* const inMw = to ? rack->getModule(to->moduleId) : nullptr;
			if (outMw == nullptr || inMw == nullptr)
				break;
			history::ComplexAction* const complex = new history::ComplexAction;
			complex->name = "show me: patch cable";
			// An input holds one cable: the old one goes first
			for (app::CableWidget* old : rack->getCompleteCablesOnPort(inMw->getInput(to->index))) {
				history::CableRemove* const h = new history::CableRemove;
				h->setCable(old);
				complex->push(h);
				rack->removeCable(old);
				delete old;
			}
			engine::Cable* const cable = new engine::Cable;
			cable->outputModule = outMw->module;
			cable->outputId = from->index;
			cable->inputModule = inMw->module;
			cable->inputId = to->index;
			APP->engine->addCable(cable);
			app::CableWidget* const cw = new app::CableWidget;
			cw->setCable(cable);
			cw->color = rack->getNextCableColor();
			rack->addCable(cw);
			history::CableAdd* const h = new history::CableAdd;
			h->setCable(cw);
			complex->push(h);
			APP->history->push(complex);
			break;
		}
		case tu::Action::SET_PARAM:
		case tu::Action::MOVE_PARAM: {
			const tu::ParamState* const p = tu::findParam(s, a.param);
			engine::Module* const m = p ? APP->engine->getModule(p->moduleId) : nullptr;
			engine::ParamQuantity* const pq = m ? m->paramQuantities[p->index] : nullptr;
			if (pq == nullptr)
				break;
			const float oldValue = pq->getValue();
			if (a.type == tu::Action::SET_PARAM)
				pq->setDisplayValue((float) a.value);
			else {
				const float cur = pq->getScaledValue();
				pq->setScaledValue(cur + a.value <= 1.f ? cur + (float) a.value : cur - (float) a.value);
			}
			history::ParamChange* const h = new history::ParamChange;
			h->name = "show me: turn knob";
			h->moduleId = p->moduleId;
			h->paramId = p->index;
			h->oldValue = oldValue;
			h->newValue = pq->getValue();
			APP->history->push(h);
			break;
		}
	}
}


// ---------------------------------------------------------------------------------------------------------
// The layer: panel + highlight + first-start offer

struct TutorialLayer;
static TutorialLayer* sLayer = nullptr;

struct TutorialPanel : widget::OpaqueWidget {
	TutorialLayer* layer;
	math::Rect backRect, nextRect, exitRect, showMeRect;
	bool dragging = false;
	bool placed = false;

	static constexpr float kW = 340.f, kH = 200.f;

	TutorialPanel(TutorialLayer* l) : layer(l) {
		box.size = math::Vec(kW, kH);
		const float by = kH - 40.f;
		backRect = math::Rect(16.f, by, 70.f, 28.f);
		showMeRect = math::Rect(96.f, by, 80.f, 28.f);
		exitRect = math::Rect(kW - 166.f, by, 70.f, 28.f);
		nextRect = math::Rect(kW - 86.f, by, 70.f, 28.f);
	}

	void onButton(const ButtonEvent& e) override;
	void onDragMove(const DragMoveEvent& e) override {
		if (dragging)
			box.pos = box.pos.plus(e.mouseDelta);
	}
	void onDragEnd(const DragEndEvent& e) override {
		dragging = false;
	}
	void draw(const DrawArgs& args) override;
};

struct TutorialLayer : widget::Widget {
	tu::Lesson lesson;
	tu::Runner runner;
	std::string lessonId;
	bool active = false;
	std::vector<int64_t> startIds;
	std::map<std::string, PeakHold> peaks;
	tu::RackState state;
	TutorialPanel* panel;
	int scrolledStep = -1;
	/** First-start offer strip and its two buttons */
	math::Rect offerStart, offerLater, offerBox;

	TutorialLayer() {
		panel = new TutorialPanel(this);
		panel->hide();
		addChild(panel);
		sLayer = this;
	}

	~TutorialLayer() {
		if (sLayer == this)
			sLayer = nullptr;
	}

	double now() const {
		return system::getTime();
	}

	void begin(const std::string& id, const tu::Lesson& l) {
		lessonId = id;
		lesson = l;
		active = true;
		peaks.clear();
		startIds = APP->engine->getModuleIds();
		setViewSide(lesson.startSide == tu::SIDE_BACK ? VIEW_BACK : VIEW_FRONT);
		runner.start(lesson, now());
		scrolledStep = -1;
		panel->placed = false;
		panel->show();
	}

	void end() {
		active = false;
		panel->hide();
	}

	void finishLesson() {
		if (!isDone(lessonId)) {
			settings.tutorialsDone.push_back(lessonId);
			saveSettings();
		}
	}

	/** The lesson after this one, "" if it is the last */
	std::string nextLessonId() const {
		const std::vector<TutorialInfo> list = tutorialList();
		for (size_t i = 0; i + 1 < list.size(); ++i)
			if (list[i].id == lessonId)
				return list[i + 1].id;
		return "";
	}

	void showMe() {
		if (runner.phase != tu::Runner::ACTIVE)
			return;
		const std::vector<tu::Action> actions = tu::showMeActions(runner.current(), state);
		for (const tu::Action& a : actions)
			perform(a, peaks, now());
	}

	bool offerVisible() const {
		return !settings.tutorialOffered && !active;
	}

	void step() override {
		if (parent != nullptr)
			box = math::Rect(math::Vec(), parent->box.size);
		if (!panel->placed && box.size.x > 0.f) {
			panel->box.pos = box.size.minus(panel->box.size).minus(math::Vec(24.f, 60.f));
			panel->placed = true;
		}
		if (active) {
			const double t = now();
			state = snapshot(peaks, t);
			if (tu::patchReplaced(startIds, state)) {
				end();
			}
			else {
				const tu::Runner::Phase before = runner.phase;
				runner.update(state, t);
				if (before != tu::Runner::FINISHED && runner.phase == tu::Runner::FINISHED)
					finishLesson();
				scrollToTarget();
			}
		}
		// The offer sits under the top bar (and under the audio notice when one shows)
		float y = APP->scene->menuBar != nullptr ? APP->scene->menuBar->box.size.y : 0.f;
		if (!audioNotice().empty())
			y += 26.f;
		offerBox = math::Rect(0.f, y, box.size.x, 26.f);
		offerStart = math::Rect(box.size.x - 190.f, y + 3.f, 80.f, 20.f);
		offerLater = math::Rect(box.size.x - 100.f, y + 3.f, 80.f, 20.f);
		Widget::step();
	}

	/** Once per step: bring the first target into view */
	void scrollToTarget() {
		if (runner.phase != tu::Runner::ACTIVE || scrolledStep == runner.step)
			return;
		scrolledStep = runner.step;
		const tu::Step& st = runner.current();
		if (st.highlight.empty())
			return;
		widget::Widget* const w = refWidget(st.highlight[0], state);
		app::ModuleWidget* mw = dynamic_cast<app::ModuleWidget*>(w);
		if (mw == nullptr && w != nullptr)
			mw = w->getAncestorOfType<app::ModuleWidget>();
		if (mw == nullptr)
			return;
		app::RackScrollWidget* const scroll = APP->scene->rackScroll;
		const math::Vec a = mw->getRelativeOffset(math::Vec(), this);
		const math::Vec b = mw->getRelativeOffset(mw->box.size, this);
		const math::Rect view = scroll->box;
		if (a.x >= view.pos.x && b.x <= view.getRight() && a.y >= view.pos.y && b.y <= view.getBottom())
			return;
		scroll->setGridOffset(mw->box.pos.div(RACK_GRID_SIZE).minus(math::Vec(4.f, 0.f)));
	}

	void onButton(const ButtonEvent& e) override {
		if (offerVisible() && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && offerBox.contains(e.pos)) {
			if (offerStart.contains(e.pos)) {
				settings.tutorialOffered = true;
				saveSettings();
				const std::vector<TutorialInfo> list = tutorialList();
				if (!list.empty())
					openTutorial(list[0].id);
			}
			else if (offerLater.contains(e.pos)) {
				settings.tutorialOffered = true;
				saveSettings();
			}
			e.consume(this);
			return;
		}
		Widget::onButton(e);
	}

	void drawHighlight(NVGcontext* vg) {
		if (!active || runner.phase == tu::Runner::FINISHED || mixerShown())
			return;
		const math::Rect clip = APP->scene->rackScroll->box;
		nvgSave(vg);
		nvgScissor(vg, RECT_ARGS(clip));
		const float pulse = 0.4f + 0.6f * (0.5f + 0.5f * std::sin((float) (now() * 2.0 * M_PI * 1.5)));
		for (const tu::Ref& r : runner.current().highlight) {
			widget::Widget* const w = refWidget(r, state);
			if (w == nullptr || !w->isVisible())
				continue;
			const math::Vec a = w->getRelativeOffset(math::Vec(), this);
			const math::Vec b = w->getRelativeOffset(w->box.size, this);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, a.x - 3.f, a.y - 3.f, b.x - a.x + 6.f, b.y - a.y + 6.f, 5.f);
			nvgStrokeWidth(vg, 2.f);
			nvgStrokeColor(vg, nvgRGBAf(0.94f, 0.6f, 0.24f, pulse));
			nvgStroke(vg);
		}
		nvgRestore(vg);
	}

	void drawOffer(NVGcontext* vg) {
		if (!offerVisible())
			return;
		nvgBeginPath(vg);
		nvgRect(vg, RECT_ARGS(offerBox));
		nvgFillColor(vg, nvgRGB(0x1f, 0x3a, 0x2a));
		nvgFill(vg);
		const NVGcolor fg = nvgRGB(0xbf, 0xf0, 0xcf);
		draw::text(vg, uiFont(), 12.f, fg, math::Vec(16.f, offerBox.getCenter().y), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
		           "New to modular? Start the first lesson and patch your first sound.");
		draw::bevelBox(vg, offerStart, 4.f, nvgRGB(0xf0, 0x9a, 0x3c), nvgRGB(0xc8, 0x76, 0x22), nvgRGB(0, 0, 0));
		draw::text(vg, uiFont(), 11.f, nvgRGB(0x16, 0x12, 0x0c), offerStart.getCenter(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "Start");
		draw::text(vg, uiFont(), 11.f, fg, offerLater.getCenter(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "Not now");
	}

	void draw(const DrawArgs& args) override {
		drawHighlight(args.vg);
		drawOffer(args.vg);
		Widget::draw(args);
	}
};


void TutorialPanel::onButton(const ButtonEvent& e) {
	OpaqueWidget::onButton(e);
	if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;
	e.consume(this);
	tu::Runner& r = layer->runner;
	const double t = layer->now();
	if (exitRect.contains(e.pos)) {
		layer->end();
		return;
	}
	if (backRect.contains(e.pos)) {
		r.back(t);
		return;
	}
	if (nextRect.contains(e.pos)) {
		if (r.phase == tu::Runner::FINISHED) {
			const std::string next = layer->nextLessonId();
			layer->end();
			if (!next.empty())
				openTutorial(next);
			return;
		}
		r.ctx.nextPressed = true;
		r.pressNext(layer->state, t);
		return;
	}
	if (showMeRect.contains(e.pos) && r.phase == tu::Runner::ACTIVE && tu::canShowMe(r.current())) {
		layer->showMe();
		return;
	}
	// Anywhere else on the panel: drag it
	dragging = true;
}


static void drawPanelButton(NVGcontext* vg, const math::Rect& r, const std::string& label, bool enabled, bool primary) {
	const Theme& t = theme();
	if (primary && enabled)
		draw::bevelBox(vg, r, 5.f, nvgRGB(0xf0, 0x9a, 0x3c), nvgRGB(0xc8, 0x76, 0x22), nvgRGB(0, 0, 0));
	else
		draw::bevelBox(vg, r, 5.f, nvgRGB(0x4a, 0x4d, 0x54), nvgRGB(0x2f, 0x32, 0x37), nvgRGB(0, 0, 0));
	draw::text(vg, uiFont(), 12.f, enabled ? (primary ? nvgRGB(0x16, 0x12, 0x0c) : t.text) : t.textFaint, r.getCenter(),
	           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, label);
}


void TutorialPanel::draw(const DrawArgs& args) {
	NVGcontext* const vg = args.vg;
	const Theme& t = theme();
	const tu::Runner& r = layer->runner;
	const tu::Lesson& l = layer->lesson;

	draw::bevelBox(vg, box.zeroPos(), 8.f, t.chrome2, t.chrome1, nvgRGB(0, 0, 0));
	draw::text(vg, labelFont(), 11.f, t.textDim, math::Vec(16.f, 22.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, string::uppercase(l.title), 1.4f);
	const bool finished = r.phase == tu::Runner::FINISHED;
	if (!finished)
		draw::text(vg, uiFont(), 11.f, t.textFaint, math::Vec(kW - 16.f, 22.f), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
		           string::f("Step %d / %d", r.step + 1, (int) l.steps.size()));

	// Body text: the step, the "gone" notice, or the summary
	std::string body;
	if (finished) {
		body = "You learned:";
		for (const std::string& s : l.learned)
			body += "\n\xE2\x80\xA2 " + s;
	}
	else {
		body = r.current().text;
		const std::vector<std::string> gone = tu::goneModules(r.current(), layer->state, r.seen);
		if (!gone.empty())
			body = "The " + gone[0] + " is gone \xE2\x80\x93 press Show me to put it back.";
	}
	nvgSave(vg);
	nvgFontFaceId(vg, uiFont()->handle);
	nvgFontSize(vg, 12.5f);
	nvgTextLineHeight(vg, 1.3f);
	nvgFillColor(vg, t.text);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgTextBox(vg, 16.f, 40.f, kW - 32.f, body.c_str(), nullptr);
	nvgRestore(vg);

	// Status line above the buttons
	std::string status;
	NVGcolor statusColor = t.textFaint;
	if (!finished && r.needsTurn(layer->state))
		status = "Press Tab to turn the rack around";
	else if (r.phase == tu::Runner::DONE) {
		status = "\xE2\x9C\x93 Done";
		statusColor = t.play;
	}
	else if (!finished && r.isTask() && !r.reviewing)
		status = "Waiting for you\xE2\x80\xA6";
	draw::text(vg, uiFont(), 11.f, statusColor, math::Vec(16.f, kH - 54.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, status);

	drawPanelButton(vg, backRect, "Back", r.step > 0 || finished, false);
	if (!finished && tu::canShowMe(r.current()))
		drawPanelButton(vg, showMeRect, "Show me", r.phase == tu::Runner::ACTIVE, false);
	drawPanelButton(vg, exitRect, "Exit", true, false);
	if (finished)
		drawPanelButton(vg, nextRect, layer->nextLessonId().empty() ? "Close" : "Next lesson", true, true);
	else
		drawPanelButton(vg, nextRect, "Next", r.nextEnabled(), true);

	OpaqueWidget::draw(args);
}


void openTutorial(const std::string& id) {
	tu::Lesson lesson;
	std::string error;
	if (!loadLesson(id, &lesson, &error)) {
		WARN("PatchCore tutorials: %s: %s", id.c_str(), error.c_str());
		return;
	}
	if (sLayer != nullptr)
		sLayer->end();
	patchUtils::loadPathDialog(system::join(tutorialsDir(), id, "start.vcv"), true, [id, lesson]() {
		if (sLayer != nullptr)
			sLayer->begin(id, lesson);
	});
}


widget::Widget* createTutorialLayer() {
	return new TutorialLayer;
}

} // namespace patchcore
} // namespace rack
```

Notes for the implementer:
- `getAncestorOfType`, `getRelativeOffset`, `isVisible`, `RECT_ARGS`, `RACK_GRID_SIZE`, `RACK_GRID_WIDTH`,
  `string::uppercase`, `string::f` are Rack 2 API (check `src/Rack/include/widget/Widget.hpp`, `common.hpp`,
  `string.hpp`). Include `<string.hpp>` if `string::uppercase` is not found.
- `draw::text(vg, font, size, color, pos, align, text, letterSpacing)` and `draw::bevelBox` are the PatchCore helpers
  used in `AudioSettings.cpp`; use the same signatures.
- `audioNotice()` is defined in `AudioSettings.cpp`, which every variant builds (symlink per variant folder), so
  `Tutorial.cpp` (built once in `RACK_FILES`) links in every variant.

- [ ] **Step 4: Hook the layer and compile the file**

In `src/override/Scene.cpp`, after `addChild(patchcore::createNoticeStrip());` add:

```cpp
		// Tutorials: step panel, highlight, first-start offer (above the rack, below the quick search)
		addChild(patchcore::createTutorialLayer());
```

In `src/Makefile`, after `RACK_FILES += patchcore/TopBar.cpp` add:

```make
RACK_FILES += patchcore/Tutorial.cpp
```

- [ ] **Step 5: Build** (only after "build")

Run: `make -j12 jack && make -j12 vst3`
Expected: builds without errors. Fix compile errors in `Tutorial.cpp` against the real Rack headers.

- [ ] **Step 6: Commit**

```bash
git add src/patchcore/Tutorial.cpp src/patchcore/PatchCore.hpp src/patchcore/PatchCore.cpp src/CardinalCommon.hpp src/CardinalCommon.cpp src/override/Scene.cpp src/Makefile
git commit -m "Tutorials: step panel, highlight, Show me, lesson opening (not built yet)"
git push
```

---

### Task 6: Help menu, docs, handoff

**Files:**
- Modify: `src/override/MenuBar.cpp:796-818` (`HelpButton`)
- Modify: `plugins/JXMP/README.md`
- Modify: `research_docs/STATUS.md`

**Interfaces:**
- Consumes: `patchcore::tutorialList()`, `patchcore::openTutorial()` (Task 5).

- [ ] **Step 1: Help → Tutorials**

In `HelpButton::onAction`, before the `"PatchCore on GitHub"` item, add:

```cpp
		// PatchCore: interactive lessons (research_docs/15)
		menu->addChild(createSubmenuItem("Tutorials", "", [](ui::Menu* menu) {
			const std::vector<patchcore::TutorialInfo> list = patchcore::tutorialList();
			if (list.empty())
				menu->addChild(createMenuLabel("No lessons found"));
			for (const patchcore::TutorialInfo& t : list) {
				const std::string id = t.id;
				ui::MenuItem* const item = createMenuItem(t.title, t.done ? CHECKMARK_STRING : "", [id]() {
					patchcore::openTutorial(id);
				});
				item->disabled = !t.available;
				menu->addChild(item);
				menu->addChild(createMenuLabel("    " + t.description));
			}
		}));

		menu->addChild(new ui::MenuSeparator);
```

- [ ] **Step 2: Docs**

In `plugins/JXMP/README.md`, add a section at the end:

```markdown
## Tutorials

`res/tutorials` holds the interactive lessons of PatchCore (Help → Tutorials): `index.json` lists them, each
folder has `lesson.json` (steps and their conditions, format in `research_docs/15-tutorials-design.md`) and
`start.vcv` (generated by `tests/make_tutorial_patches.py`). Lessons reference ports and knobs by their
configured names: renaming a port or knob breaks a lesson, and `src/patchcore/tests/test_tutorial_content.cpp`
says which.
```

In `research_docs/STATUS.md`, replace the "Tutorials: spec 15 written" section with the current state (which tasks
are done, built or not, what the user still has to check: each lesson done normally and only through Show me,
standalone and VST3).

- [ ] **Step 3: Build and check with the user** (only after "build")

Run: `make -j12 jack && bash src/patchcore/tests/run.sh`
Then with the user watching workspace 3 (`./run-patchcore.sh`): Help → Tutorials lists 3 lessons; lesson 1 done by
hand from start to the summary; lesson 2 done only with Show me; lesson 3 with Back on a done task; File → New in
the middle of a lesson closes the panel; the first-start offer appears with a fresh `PatchCore.json` (move it
aside, restore it after). Then `make -j12 vst3` and lesson 1 in a VST3 host.

- [ ] **Step 4: Commit and bump**

```bash
git add src/override/MenuBar.cpp plugins/JXMP/README.md research_docs/STATUS.md
git commit -m "Tutorials: Help menu entry, docs, status"
git push
```

The collection pointer follows `main`; bump it only after the branch is merged.

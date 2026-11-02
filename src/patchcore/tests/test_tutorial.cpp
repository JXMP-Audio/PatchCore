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

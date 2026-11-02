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

int main() {
	testParse();
	testConditions();
	std::printf("tutorial logic: all tests passed\n");
	return 0;
}

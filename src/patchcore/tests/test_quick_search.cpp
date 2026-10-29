#include "QuickSearchLogic.hpp"
#include <cassert>
#include <cstdio>

using namespace rack::patchcore::qs;

int main() {
	// Normalising: lower case, letters and digits only
	assert(normalize("JX-MP Audio") == "jxmpaudio");
	assert(normalize("  Low-frequency oscillator ") == "lowfrequencyoscillator");

	// Query syntax: text, @brand, #purpose in any order, case and spaces ignored
	Query q = parseQuery("VCO@JXMP");
	assert(q.text == "VCO" && q.brand == "jxmp" && q.tag.empty());
	q = parseQuery("vco @ jx mp");
	assert(q.text == "vco" && q.brand == "jxmp");
	q = parseQuery("filter#Effect@acme");
	assert(q.text == "filter" && q.tag == "effect" && q.brand == "acme");
	q = parseQuery("@acme");
	assert(q.text.empty() && q.brand == "acme");
	q = parseQuery("#lfo slow");
	assert(q.tag == "lfoslow" || q.tag == "lfo");

	std::vector<Entry> e = {
		{"Acme", {"Oscillator"}, false, 0.0, false},                        // 0
		{"JXMP Audio", {"VCA"}, false, 0.0, true},                          // 1
		{"Acme", {"Filter", "Effect"}, true, 0.0, false},                  // 2 favorite
		{"Other", {"LFO", "Low-frequency oscillator"}, false, 50.0, false}, // 3 recent (older)
		{"JXMP Audio", {"LFO"}, true, 90.0, true},                          // 4 recent (newest), favorite, ours
	};

	// Filters match a part of the normalised brand / any tag
	assert(passes(e[1], "", "jxmp"));
	assert(!passes(e[0], "", "jxmp"));
	assert(passes(e[2], "filter", "") && !passes(e[2], "lfo", ""));
	assert(passes(e[3], "lowfreq", ""));
	assert(passes(e[0], "", ""));

	// Empty text: recent (newest first), then favorites, then our modules; nothing twice
	std::vector<Item> order = emptyQueryOrder(e, "", "", 8);
	assert(order.size() == 4);
	assert(order[0].index == 4 && order[0].section == SECTION_RECENT);
	assert(order[1].index == 3 && order[1].section == SECTION_RECENT);
	assert(order[2].index == 2 && order[2].section == SECTION_FAVORITES);
	assert(order[3].index == 1 && order[3].section == SECTION_OURS);
	// With a filter, every matching module is listed (filtered sections first)
	order = emptyQueryOrder(e, "lfo", "", 8);
	assert(order.size() == 2 && order[0].index == 4 && order[1].index == 3);
	order = emptyQueryOrder(e, "oscillator", "", 8);
	assert(order.size() == 2);  // the oscillator (0) and the LFO tagged "Low-frequency oscillator" (3)
	order = emptyQueryOrder(e, "", "acme", 8);
	assert(order.size() == 2 && order[0].index == 2 && order[1].index == 0 && order[1].section == SECTION_ALL);
	// Recent is capped
	order = emptyQueryOrder(e, "", "", 1);
	assert(order[0].index == 4 && order[1].section != SECTION_RECENT);
	std::puts("quick search: all tests passed");
	return 0;
}

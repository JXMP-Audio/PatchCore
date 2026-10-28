#include "PatchbayLogic.hpp"
#include <cassert>
#include <cstdio>

using namespace jxmp;

int main() {
	// Direction comes from complete cables
	assert(direction({}) == Dir::Free);
	assert(direction({true, false, false, false}) == Dir::Forward);  // front in
	assert(direction({false, false, false, true}) == Dir::Forward);  // back out
	assert(direction({false, false, true, false}) == Dir::Reverse);  // back in
	assert(direction({false, true, false, false}) == Dir::Reverse);  // front out
	// Hand-edited patch with both inputs: forward wins in the UI
	assert(direction({true, false, true, false}) == Dir::Forward);

	// What a dragged cable still needs
	assert(need(false, false) == Need::Nothing);
	assert(need(true, false) == Need::Input);
	assert(need(false, true) == Need::Output);
	assert(need(true, true) == Need::Nothing);

	// Roles
	assert(roles(Dir::Forward, Need::Nothing).front == Role::In);
	assert(roles(Dir::Forward, Need::Output).back == Role::Out);
	assert(roles(Dir::Reverse, Need::Input).front == Role::Out);
	assert(roles(Dir::Reverse, Need::Input).back == Role::In);
	assert(roles(Dir::Free, Need::Input).front == Role::In);
	assert(roles(Dir::Free, Need::Input).back == Role::In);
	assert(roles(Dir::Free, Need::Output).front == Role::Out);
	assert(roles(Dir::Free, Need::Nothing).front == Role::Out);

	// A drag started on our free jack flips its end when hovering a foreign port of the same type
	assert(ownedDragRole(Role::Out, true, false) == Role::In);
	assert(ownedDragRole(Role::In, false, true) == Role::Out);
	assert(ownedDragRole(Role::Out, false, true) == Role::Out);
	assert(ownedDragRole(Role::In, true, false) == Role::In);
	assert(ownedDragRole(Role::Out, false, false) == Role::Out);

	// Only a drag that starts on a channel that was free before it owns the cable's end
	assert(takesOwnership(Dir::Free, true));
	assert(!takesOwnership(Dir::Forward, true));  // picked up its only cable by the far end
	assert(!takesOwnership(Dir::Reverse, true));
	assert(!takesOwnership(Dir::Free, false));

	std::puts("patchbay logic: all tests passed");
	return 0;
}

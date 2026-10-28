/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

/** Direction rules of a Patchbay channel. No Rack dependency, so it can be tested alone. */
namespace jxmp {

enum class Dir { Free, Forward, Reverse };
enum class Role { In, Out };
/** What the cable being dragged still needs at its free end. */
enum class Need { Nothing, Input, Output };

/** Which of a channel's four ports have a complete cable. */
struct ChannelCables {
	bool frontIn = false;
	bool frontOut = false;
	bool backIn = false;
	bool backOut = false;
};

struct Roles {
	Role front;
	Role back;
};

/** Forward = front to back. Any cable locks the direction. */
inline Dir direction(const ChannelCables& c) {
	if (c.frontIn || c.backOut)
		return Dir::Forward;
	if (c.backIn || c.frontOut)
		return Dir::Reverse;
	return Dir::Free;
}

inline Need need(bool hasOutputEnd, bool hasInputEnd) {
	if (hasOutputEnd && !hasInputEnd)
		return Need::Input;
	if (hasInputEnd && !hasOutputEnd)
		return Need::Output;
	return Need::Nothing;
}

/** Which port is active under each smart jack. A free channel offers what the dragged cable needs, else an output. */
inline Roles roles(Dir dir, Need dragging) {
	switch (dir) {
	case Dir::Forward:
		return {Role::In, Role::Out};
	case Dir::Reverse:
		return {Role::Out, Role::In};
	default: {
		const Role r = dragging == Need::Input ? Role::In : Role::Out;
		return {r, r};
	}
	}
}

/** Role of our end of a cable dragged out of a free jack: it turns into an input while a foreign output
is hovered, and back into an output while a foreign input is hovered. */
inline Role ownedDragRole(Role current, bool hoveringForeignOutput, bool hoveringForeignInput) {
	if (current == Role::Out && hoveringForeignOutput)
		return Role::In;
	if (current == Role::In && hoveringForeignInput)
		return Role::Out;
	return current;
}

/** A cable dragged out of this channel is owned (its end may flip role) only when the channel was free before the
drag started. A cable picked up by its far end keeps its Patchbay end, like any Rack jack. */
inline bool takesOwnership(Dir dirBeforeDrag, bool cableTouchesChannel) {
	return dirBeforeDrag == Dir::Free && cableTouchesChannel;
}

} // namespace jxmp

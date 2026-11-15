/*
 * JXMP Audio
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * "One per project" modules (JX-CONSOLE, JX-EDIT): the owner is kept per Rack context, so two PatchCore instances
 * in one DAW process each have their own console and editor.
 */

#pragma once

#include <atomic>
#include <map>
#include <mutex>

#include <context.hpp>

namespace jxmp {

/** The owner slot of type T for the current context (APP). Call it where APP is the module's context (the module's
constructor) and keep the pointer: the slot lives as long as the process. */
template <typename T>
std::atomic<T*>* projectOwnerSlot() {
	static std::mutex lock;
	static std::map<rack::Context*, std::atomic<T*>*> slots;
	std::lock_guard<std::mutex> guard(lock);
	std::atomic<T*>*& slot = slots[rack::contextGet()];
	if (slot == nullptr)
		slot = new std::atomic<T*>(nullptr);
	return slot;
}

} // namespace jxmp

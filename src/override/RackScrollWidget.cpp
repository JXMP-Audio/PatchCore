/*
 * DISTRHO Cardinal Plugin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * This file is an edited version of VCVRack's app/RackScrollWidget.cpp
 * Copyright (C) 2016-2023 VCV.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 */

#include <app/RackScrollWidget.hpp>
#include <app/Scene.hpp>
#include <app/RackWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/Knob.hpp>
#include <window/Window.hpp>
#include <context.hpp>
#include <settings.hpp>


namespace rack {
namespace app {


struct RackScrollWidget::Internal {
	/** For viewport expanding */
	float oldZoom = 0.f;
	math::Vec oldOffset;
};


RackScrollWidget::RackScrollWidget() {
	internal = new Internal;

	zoomWidget = new widget::ZoomWidget;
	container->addChild(zoomWidget);

	rackWidget = new RackWidget;
	rackWidget->box.size = RACK_OFFSET.mult(2);
	zoomWidget->addChild(rackWidget);

	reset();
}


RackScrollWidget::~RackScrollWidget() {
	delete internal;
}


void RackScrollWidget::reset() {
	offset = RACK_OFFSET * zoomWidget->getZoom() - math::Vec(30, 30);
}


math::Vec RackScrollWidget::getGridOffset() {
	return (offset / zoomWidget->getZoom() - RACK_OFFSET) / RACK_GRID_SIZE;
}


void RackScrollWidget::setGridOffset(math::Vec gridOffset) {
	offset = (gridOffset * RACK_GRID_SIZE + RACK_OFFSET) * zoomWidget->getZoom();
}


float RackScrollWidget::getZoom() {
	return zoomWidget->getZoom();
}


void RackScrollWidget::setZoom(float zoom) {
	setZoom(zoom, getSize().div(2));
}


void RackScrollWidget::setZoom(float zoom, math::Vec pivot) {
	zoom = math::clamp(zoom, std::pow(2.f, -2), std::pow(2.f, 2));

	offset = (offset + pivot) * (zoom / zoomWidget->getZoom()) - pivot;
	zoomWidget->setZoom(zoom);
}


void RackScrollWidget::step() {
	float zoom = getZoom();

	// Compute module bounding box
	math::Rect moduleBox = rackWidget->getModuleContainer()->getChildrenBoundingBox();
	if (!moduleBox.size.isFinite())
		moduleBox = math::Rect(RACK_OFFSET, math::Vec(0, 0));

	// Expand moduleBox by a screen size
	math::Rect scrollBox = moduleBox;
	scrollBox.pos = scrollBox.pos.mult(zoom);
	scrollBox.size = scrollBox.size.mult(zoom);
	scrollBox = scrollBox.grow(box.size.mult(0.9));

	// Expand to the current viewport box so that moving modules (and thus changing the module bounding box) doesn't clamp the scroll offset.
	if (zoom == internal->oldZoom) {
		math::Rect viewportBox;
		viewportBox.pos = internal->oldOffset;
		viewportBox.size = box.size;
		scrollBox = scrollBox.expand(viewportBox);
	}

	// Reposition widgets
	zoomWidget->box = scrollBox;
	rackWidget->box.pos = scrollBox.pos.div(zoom).neg();

	// Scroll rack if dragging certain widgets near the edge of the screen
	math::Vec pos = APP->scene->mousePos - box.pos;
	math::Rect viewport = getViewport(box.zeroPos());
	widget::Widget* dw = APP->event->getDraggedWidget();
	if (dw && APP->event->dragButton == GLFW_MOUSE_BUTTON_LEFT &&
		(dynamic_cast<RackWidget*>(dw) || dynamic_cast<ModuleWidget*>(dw) || dynamic_cast<PortWidget*>(dw))) {
		float margin = 1.0;
		float speed = 15.0;
		if (pos.x <= viewport.pos.x + margin)
			offset.x -= speed;
		if (pos.x >= viewport.pos.x + viewport.size.x - margin)
			offset.x += speed;
		if (pos.y <= viewport.pos.y + margin)
			offset.y -= speed;
		if (pos.y >= viewport.pos.y + viewport.size.y - margin)
			offset.y += speed;
	}

	// Hide scrollbars if fullscreen
	hideScrollbars = APP->window->isFullScreen();

	ScrollWidget::step();

	internal->oldOffset = offset;
	internal->oldZoom = zoom;
}


void RackScrollWidget::draw(const DrawArgs& args) {
	ScrollWidget::draw(args);
}


void RackScrollWidget::onHoverKey(const HoverKeyEvent& e) {
	ScrollWidget::onHoverKey(e);
}


/** The knob under the mouse, if any (the hovered widget or one of its parents) */
static Knob* hoveredKnob() {
	for (widget::Widget* w = APP->event->hoveredWidget; w; w = w->parent) {
		if (Knob* const k = dynamic_cast<Knob*>(w))
			return k;
	}
	return nullptr;
}


void RackScrollWidget::onHoverScroll(const HoverScrollEvent& e) {
	// PatchCore: Ctrl + wheel over a knob turns it finely (the knob's own Ctrl speed) instead of zooming the rack
	if ((APP->window->getMods() & RACK_MOD_MASK) == RACK_MOD_CTRL && !(settings::knobScroll && hoveredKnob())) {
		// Increase zoom
		float zoomDelta = e.scrollDelta.y / 50 / 4;
		if (settings::invertZoom)
			zoomDelta *= -1;
		float zoom = getZoom() * std::pow(2.f, zoomDelta);
		setZoom(zoom, e.pos);
		e.consume(this);
	}

	if (e.isConsumed())
		return;
	ScrollWidget::onHoverScroll(e);
}


void RackScrollWidget::onHover(const HoverEvent& e) {
	ScrollWidget::onHover(e);

	// Hide menu bar if fullscreen and moving mouse over the RackScrollWidget
	if (APP->window->isFullScreen()) {
		APP->scene->menuBar->hide();
	}
}


void RackScrollWidget::onButton(const ButtonEvent& e) {
	ScrollWidget::onButton(e);
	if (e.isConsumed())
		return;

	// Zoom in/out with extra mouse buttons
	if (e.action == GLFW_PRESS) {
		if (e.button == GLFW_MOUSE_BUTTON_4) {
			float zoom = getZoom() * std::pow(2.f, -0.5f);
			setZoom(zoom, e.pos);
			e.consume(this);
		}
		if (e.button == GLFW_MOUSE_BUTTON_5) {
			float zoom = getZoom() * std::pow(2.f, 0.5f);
			setZoom(zoom, e.pos);
			e.consume(this);
		}
	}
}


} // namespace app
} // namespace rack

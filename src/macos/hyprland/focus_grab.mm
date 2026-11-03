#include "focus_grab.hpp"

#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qquickwindow.h>
#include <qwindow.h>

#include "../../window/proxywindow.hpp"
#include "../../window/windowinterface.hpp"

#import <AppKit/AppKit.h>

namespace qs::hyprland {

namespace {

constexpr NSEventMask MOUSE_DOWN_EVENTS =
    NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown;

QWindow* backingWindowFor(QObject* object) {
	if (auto* windowInterface = qobject_cast<WindowInterface*>(object)) {
		auto* proxyWindow = windowInterface->proxyWindow();
		return proxyWindow ? proxyWindow->backingWindow() : nullptr;
	}
	if (auto* proxyWindow = qobject_cast<ProxyWindowBase*>(object)) return proxyWindow->backingWindow();
	return qobject_cast<QWindow*>(object);
}

qintptr nativeWindowNumber(QWindow* window) {
	if (!window || !window->handle()) return 0;
	auto* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
	return view.window.windowNumber;
}

}

HyprlandFocusGrab::~HyprlandFocusGrab() { this->stopMonitoring(); }

void HyprlandFocusGrab::setActive(bool active) {
	if (active == this->mActive) return;
	this->mActive = active;

	if (active) this->startMonitoring();
	else this->stopMonitoring();

	emit this->activeChanged();
}

void HyprlandFocusGrab::setWindows(const QList<QObject*>& windows) {
	if (windows == this->mWindows) return;
	this->mWindows = windows;
	emit this->windowsChanged();
}

QList<QWindow*> HyprlandFocusGrab::grabbedWindows() const {
	QList<QWindow*> windows;
	for (auto* object: this->mWindows) {
		if (auto* window = backingWindowFor(object)) windows.append(window);
	}
	return windows;
}

bool HyprlandFocusGrab::containsNativeWindow(qintptr windowNumber) const {
	if (windowNumber == 0) return false;

	for (auto* window: this->grabbedWindows()) {
		if (nativeWindowNumber(window) == windowNumber) return true;
	}
	return false;
}

void HyprlandFocusGrab::clearGrab() {
	if (!this->mActive) return;
	this->setActive(false);
	emit this->cleared();
}

void HyprlandFocusGrab::startMonitoring() {
	QPointer<HyprlandFocusGrab> grab = this;

	id globalMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:MOUSE_DOWN_EVENTS
	                                                          handler:^(NSEvent*) {
		                                                          if (grab) grab->clearGrab();
	                                                          }];

	id localMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:MOUSE_DOWN_EVENTS
	                                                        handler:^NSEvent*(NSEvent* event) {
		                                                        if (grab && !grab->containsNativeWindow(event.windowNumber)) {
			                                                        grab->clearGrab();
		                                                        }
		                                                        return event;
	                                                        }];

	this->globalMonitor = (__bridge_retained void*)globalMonitor;
	this->localMonitor = (__bridge_retained void*)localMonitor;

	auto windows = this->grabbedWindows();
	if (!windows.isEmpty() && windows.first()->handle()) {
		[NSApp activateIgnoringOtherApps:YES];
		windows.first()->requestActivate();
	}
}

void HyprlandFocusGrab::stopMonitoring() {
	if (this->globalMonitor) {
		[NSEvent removeMonitor:(__bridge_transfer id)this->globalMonitor];
		this->globalMonitor = nullptr;
	}
	if (this->localMonitor) {
		[NSEvent removeMonitor:(__bridge_transfer id)this->localMonitor];
		this->localMonitor = nullptr;
	}
}

}

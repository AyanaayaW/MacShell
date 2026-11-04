#include "input_region.hpp"

#include <qhash.h>
#include <qpointer.h>
#include <qregion.h>
#include <qtimer.h>
#include <qwindow.h>

#import <AppKit/AppKit.h>

namespace qs::macos {

namespace {

constexpr int CURSOR_POLL_INTERVAL_MILLISECONDS = 16;

struct TrackedInputRegion {
	QPointer<QWindow> window;
	QRegion region;
};

QHash<QWindow*, TrackedInputRegion>& trackedRegions() {
	static QHash<QWindow*, TrackedInputRegion> regions;
	return regions;
}

NSWindow* nativeWindow(QWindow* window) {
	if (!window || !window->handle()) return nil;
	auto* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
	return view.window;
}

void setAcceptsMouse(NSWindow* window, bool acceptsMouse) {
	if (window && window.ignoresMouseEvents == acceptsMouse) window.ignoresMouseEvents = !acceptsMouse;
}

bool cursorIsInside(NSWindow* window, const QRegion& region, NSPoint cursor) {
	NSRect frame = window.frame;
	auto localX = cursor.x - frame.origin.x;
	auto localY = frame.size.height - (cursor.y - frame.origin.y);
	return region.contains(QPoint(static_cast<int>(localX), static_cast<int>(localY)));
}

void updateTrackedWindows();

QTimer& cursorTimer() {
	static QTimer* timer = [] {
		auto* createdTimer = new QTimer();
		createdTimer->setInterval(CURSOR_POLL_INTERVAL_MILLISECONDS);
		QObject::connect(createdTimer, &QTimer::timeout, updateTrackedWindows);
		return createdTimer;
	}();
	return *timer;
}

void updateTrackedWindows() {
	auto& regions = trackedRegions();
	NSPoint cursor = NSEvent.mouseLocation;
	bool buttonHeld = NSEvent.pressedMouseButtons != 0;

	for (auto iterator = regions.begin(); iterator != regions.end();) {
		if (!iterator->window) {
			iterator = regions.erase(iterator);
			continue;
		}

		NSWindow* window = nativeWindow(iterator->window);
		if (window) {
			bool accepting = !window.ignoresMouseEvents;
			bool inside = cursorIsInside(window, iterator->region, cursor);
			if (!(accepting && buttonHeld)) setAcceptsMouse(window, inside);
		}
		++iterator;
	}

	if (regions.isEmpty()) cursorTimer().stop();
}

}

void applyInputRegion(QWindow* window, const std::optional<QRegion>& inputRegion) {
	auto& regions = trackedRegions();
	NSWindow* native = nativeWindow(window);

	if (!inputRegion) {
		regions.remove(window);
		setAcceptsMouse(native, true);
		return;
	}

	if (inputRegion->isEmpty()) {
		regions.remove(window);
		setAcceptsMouse(native, false);
		return;
	}

	regions.insert(window, {window, *inputRegion});
	if (!cursorTimer().isActive()) cursorTimer().start();
	updateTrackedWindows();
}

}

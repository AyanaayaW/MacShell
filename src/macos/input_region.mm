#include "input_region.hpp"

#include <algorithm>

#include <qcoreapplication.h>
#include <qevent.h>
#include <qguiapplication.h>
#include <qhash.h>
#include <qpoint.h>
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
	bool cursorInside = false;
	QPointF lastLocalPosition {-1.0, -1.0};
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

bool cursorWithinWindowFrame(NSWindow* window, NSPoint cursor) {
	NSRect frame = window.frame;
	return cursor.x >= frame.origin.x - 1.0 && cursor.x <= frame.origin.x + frame.size.width + 1.0
	    && cursor.y >= frame.origin.y - 1.0 && cursor.y <= frame.origin.y + frame.size.height + 1.0;
}

QPointF localCursorPosition(NSWindow* window, NSPoint cursor) {
	NSRect frame = window.frame;
	double maxLocalX = std::max(0.0, static_cast<double>(frame.size.width) - 1.0);
	double maxLocalY = std::max(0.0, static_cast<double>(frame.size.height) - 1.0);
	double clampedX = std::clamp(static_cast<double>(cursor.x - frame.origin.x), 0.0, maxLocalX);
	double clampedY = std::clamp(
	    static_cast<double>(frame.size.height - (cursor.y - frame.origin.y)),
	    0.0,
	    maxLocalY
	);
	return QPointF(clampedX, clampedY);
}

QPointF globalCursorPosition(QWindow* window, const QPointF& localPosition) {
	QPoint windowOrigin = window->geometry().topLeft();
	return QPointF(windowOrigin.x() + localPosition.x(), windowOrigin.y() + localPosition.y());
}

void deliverHoverEnterAndMove(QWindow* window, const QPointF& localPosition) {
	QPointF globalPosition = globalCursorPosition(window, localPosition);
	QEnterEvent enterEvent(localPosition, localPosition, globalPosition);
	QCoreApplication::sendEvent(window, &enterEvent);

	QMouseEvent moveEvent(
	    QEvent::MouseMove,
	    localPosition,
	    localPosition,
	    globalPosition,
	    Qt::NoButton,
	    Qt::NoButton,
	    QGuiApplication::keyboardModifiers()
	);
	QCoreApplication::sendEvent(window, &moveEvent);
}

void deliverHoverMove(QWindow* window, const QPointF& localPosition) {
	QPointF globalPosition = globalCursorPosition(window, localPosition);
	QMouseEvent moveEvent(
	    QEvent::MouseMove,
	    localPosition,
	    localPosition,
	    globalPosition,
	    Qt::NoButton,
	    Qt::NoButton,
	    QGuiApplication::keyboardModifiers()
	);
	QCoreApplication::sendEvent(window, &moveEvent);
}

void deliverHoverLeave(QWindow* window) {
	QEvent leaveEvent(QEvent::Leave);
	QCoreApplication::sendEvent(window, &leaveEvent);
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
			bool withinFrame = cursorWithinWindowFrame(window, cursor);
			QPointF localPosition = localCursorPosition(window, cursor);
			bool inside = withinFrame && iterator->region.contains(localPosition.toPoint());

			if (!(accepting && buttonHeld)) {
				setAcceptsMouse(window, inside);
			}

			if (inside) {
				if (!iterator->cursorInside) {
					iterator->cursorInside = true;
					iterator->lastLocalPosition = localPosition;
					if (!buttonHeld) deliverHoverEnterAndMove(iterator->window, localPosition);
				} else if (localPosition != iterator->lastLocalPosition) {
					iterator->lastLocalPosition = localPosition;
					if (!buttonHeld) deliverHoverMove(iterator->window, localPosition);
				}
			} else if (iterator->cursorInside && !buttonHeld) {
				iterator->cursorInside = false;
				iterator->lastLocalPosition = QPointF(-1.0, -1.0);
				deliverHoverLeave(iterator->window);
			}
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
		if (regions.contains(window) && regions[window].cursorInside) {
			deliverHoverLeave(window);
		}
		regions.remove(window);
		setAcceptsMouse(native, false);
		return;
	}

	auto existing = regions.find(window);
	if (existing != regions.end()) {
		existing->region = *inputRegion;
	} else {
		regions.insert(window, {window, *inputRegion, false, QPointF(-1.0, -1.0)});
	}
	if (!cursorTimer().isActive()) cursorTimer().start();
	updateTrackedWindows();
}

}

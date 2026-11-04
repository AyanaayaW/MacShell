#include "window_actions.hpp"
#include <cmath>
#include <limits>
#include <optional>

#include <qrect.h>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

extern "C" AXError _AXUIElementGetWindow(AXUIElementRef element, CGWindowID* windowId);

namespace qs::hyprland::macos {

namespace {

struct FocusedWindow {
	qint32 processId = 0;
	AXUIElementRef element = nullptr;

	FocusedWindow() = default;
	FocusedWindow(qint32 processId, AXUIElementRef element): processId(processId), element(element) {}
	~FocusedWindow() {
		if (this->element) CFRelease(this->element);
	}
	FocusedWindow(const FocusedWindow&) = delete;
	FocusedWindow& operator=(const FocusedWindow&) = delete;
	FocusedWindow(FocusedWindow&& other) noexcept: processId(other.processId), element(other.element) {
		other.element = nullptr;
	}
	FocusedWindow& operator=(FocusedWindow&&) = delete;
};

std::optional<FocusedWindow> copyFocusedWindow() {
	auto* application = NSWorkspace.sharedWorkspace.frontmostApplication;
	if (!application) return std::nullopt;

	AXUIElementRef applicationElement = AXUIElementCreateApplication(application.processIdentifier);
	CFTypeRef window = nullptr;
	auto status = AXUIElementCopyAttributeValue(applicationElement, kAXFocusedWindowAttribute, &window);
	CFRelease(applicationElement);
	if (status != kAXErrorSuccess || !window) return std::nullopt;

	return FocusedWindow(application.processIdentifier, static_cast<AXUIElementRef>(window));
}

AXUIElementRef copyWindowElement(qint32 processId, quint32 windowId) {
	AXUIElementRef applicationElement = AXUIElementCreateApplication(processId);
	CFArrayRef windows = nullptr;
	AXUIElementCopyAttributeValues(applicationElement, kAXWindowsAttribute, 0, 100, &windows);
	CFRelease(applicationElement);
	if (!windows) return nullptr;

	AXUIElementRef match = nullptr;
	for (CFIndex index = 0; index < CFArrayGetCount(windows) && !match; ++index) {
		auto window = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(windows, index));
		CGWindowID candidateId = 0;
		if (_AXUIElementGetWindow(window, &candidateId) == kAXErrorSuccess && candidateId == windowId) {
			match = static_cast<AXUIElementRef>(CFRetain(window));
		}
	}
	CFRelease(windows);
	return match;
}

std::optional<QRect> frameOf(AXUIElementRef window) {
	CFTypeRef positionValue = nullptr;
	CFTypeRef sizeValue = nullptr;
	AXUIElementCopyAttributeValue(window, kAXPositionAttribute, &positionValue);
	AXUIElementCopyAttributeValue(window, kAXSizeAttribute, &sizeValue);

	CGPoint position {};
	CGSize size {};
	bool valid = positionValue && sizeValue
	          && AXValueGetValue(static_cast<AXValueRef>(positionValue), kAXValueTypeCGPoint, &position)
	          && AXValueGetValue(static_cast<AXValueRef>(sizeValue), kAXValueTypeCGSize, &size);
	if (positionValue) CFRelease(positionValue);
	if (sizeValue) CFRelease(sizeValue);
	if (!valid) return std::nullopt;

	return QRect(
	    static_cast<int>(position.x),
	    static_cast<int>(position.y),
	    static_cast<int>(size.width),
	    static_cast<int>(size.height)
	);
}

void setFrame(AXUIElementRef window, const QRect& frame) {
	CGPoint position = CGPointMake(frame.x(), frame.y());
	CGSize size = CGSizeMake(frame.width(), frame.height());
	AXValueRef positionValue = AXValueCreate(kAXValueTypeCGPoint, &position);
	AXValueRef sizeValue = AXValueCreate(kAXValueTypeCGSize, &size);
	AXUIElementSetAttributeValue(window, kAXPositionAttribute, positionValue);
	AXUIElementSetAttributeValue(window, kAXSizeAttribute, sizeValue);
	AXUIElementSetAttributeValue(window, kAXPositionAttribute, positionValue);
	CFRelease(positionValue);
	CFRelease(sizeValue);
}

QRect usableAreaContaining(const QRect& frame) {
	auto primaryHeight = NSScreen.screens.firstObject.frame.size.height;
	NSScreen* bestScreen = NSScreen.mainScreen;
	for (NSScreen* screen in NSScreen.screens) {
		NSRect screenFrame = screen.frame;
		QRect topLeftFrame(
		    static_cast<int>(screenFrame.origin.x),
		    static_cast<int>(primaryHeight - screenFrame.origin.y - screenFrame.size.height),
		    static_cast<int>(screenFrame.size.width),
		    static_cast<int>(screenFrame.size.height)
		);
		if (topLeftFrame.contains(frame.center())) bestScreen = screen;
	}

	NSRect visible = bestScreen.visibleFrame;
	return {
	    static_cast<int>(visible.origin.x),
	    static_cast<int>(primaryHeight - visible.origin.y - visible.size.height),
	    static_cast<int>(visible.size.width),
	    static_cast<int>(visible.size.height),
	};
}

bool pressCloseButton(AXUIElementRef window) {
	CFTypeRef closeButton = nullptr;
	if (AXUIElementCopyAttributeValue(window, kAXCloseButtonAttribute, &closeButton) != kAXErrorSuccess
	    || !closeButton)
	{
		return false;
	}
	auto status = AXUIElementPerformAction(static_cast<AXUIElementRef>(closeButton), kAXPressAction);
	CFRelease(closeButton);
	return status == kAXErrorSuccess;
}

bool raiseAndActivate(qint32 processId, AXUIElementRef window) {
	AXUIElementPerformAction(window, kAXRaiseAction);
	AXUIElementSetAttributeValue(window, kAXMainAttribute, kCFBooleanTrue);
	auto* application = [NSRunningApplication runningApplicationWithProcessIdentifier:processId];
	return [application activateWithOptions:NSApplicationActivateAllWindows] || application != nil;
}

}

bool closeFocusedWindow() {
	auto focused = copyFocusedWindow();
	return focused && pressCloseButton(focused->element);
}

bool closeWindow(qint32 processId, quint32 windowId) {
	AXUIElementRef window = copyWindowElement(processId, windowId);
	if (!window) return false;
	auto closed = pressCloseButton(window);
	CFRelease(window);
	return closed;
}

bool toggleFocusedFullscreen() {
	auto focused = copyFocusedWindow();
	if (!focused) return false;

	CFTypeRef fullscreenValue = nullptr;
	AXUIElementCopyAttributeValue(focused->element, CFSTR("AXFullScreen"), &fullscreenValue);
	bool isFullscreen = fullscreenValue && CFBooleanGetValue(static_cast<CFBooleanRef>(fullscreenValue));
	if (fullscreenValue) CFRelease(fullscreenValue);

	return AXUIElementSetAttributeValue(
	           focused->element,
	           CFSTR("AXFullScreen"),
	           isFullscreen ? kCFBooleanFalse : kCFBooleanTrue
	       )
	    == kAXErrorSuccess;
}

bool maximizeFocusedWindow() {
	auto focused = copyFocusedWindow();
	if (!focused) return false;
	auto frame = frameOf(focused->element);
	if (!frame) return false;

	auto usable = usableAreaContaining(*frame);
	setFrame(focused->element, *frame == usable ? frame->adjusted(80, 60, -80, -60) : usable);
	return true;
}

bool centerFocusedWindow() {
	auto focused = copyFocusedWindow();
	if (!focused) return false;
	auto frame = frameOf(focused->element);
	if (!frame) return false;

	auto centered = *frame;
	centered.moveCenter(usableAreaContaining(*frame).center());
	setFrame(focused->element, centered);
	return true;
}

bool resizeFocusedWindow(int widthDelta, int heightDelta) {
	auto focused = copyFocusedWindow();
	if (!focused) return false;
	auto frame = frameOf(focused->element);
	if (!frame) return false;

	setFrame(focused->element, frame->adjusted(0, 0, widthDelta, heightDelta));
	return true;
}

bool snapFocusedWindow(Direction direction) {
	auto focused = copyFocusedWindow();
	if (!focused) return false;
	auto frame = frameOf(focused->element);
	if (!frame) return false;

	auto usable = usableAreaContaining(*frame);
	auto halfWidth = usable.width() / 2;
	auto halfHeight = usable.height() / 2;

	QRect target = usable;
	switch (direction) {
	case Direction::Left: target.setWidth(halfWidth); break;
	case Direction::Right: target.setLeft(usable.left() + halfWidth); break;
	case Direction::Up: target.setHeight(halfHeight); break;
	case Direction::Down: target.setTop(usable.top() + halfHeight); break;
	}
	setFrame(focused->element, target);
	return true;
}

bool focusWindow(qint32 processId, quint32 windowId) {
	AXUIElementRef window = copyWindowElement(processId, windowId);
	if (!window) return false;
	auto focused = raiseAndActivate(processId, window);
	CFRelease(window);
	return focused;
}

bool focusWindowInDirection(Direction direction, const QList<WindowDescription>& windows) {
	auto focused = copyFocusedWindow();
	if (!focused) return false;
	auto origin = frameOf(focused->element);
	if (!origin) return false;

	CGWindowID focusedWindowId = 0;
	_AXUIElementGetWindow(focused->element, &focusedWindowId);

	const WindowDescription* best = nullptr;
	double bestDistance = std::numeric_limits<double>::max();
	auto originCenter = origin->center();

	for (const auto& candidate: windows) {
		if (candidate.windowId == focusedWindowId) continue;
		auto center = candidate.frame.center();
		auto horizontal = center.x() - originCenter.x();
		auto vertical = center.y() - originCenter.y();

		bool inDirection = false;
		switch (direction) {
		case Direction::Left: inDirection = horizontal < 0 && std::abs(horizontal) >= std::abs(vertical); break;
		case Direction::Right: inDirection = horizontal > 0 && std::abs(horizontal) >= std::abs(vertical); break;
		case Direction::Up: inDirection = vertical < 0 && std::abs(vertical) >= std::abs(horizontal); break;
		case Direction::Down: inDirection = vertical > 0 && std::abs(vertical) >= std::abs(horizontal); break;
		}
		if (!inDirection) continue;

		auto distance = std::hypot(static_cast<double>(horizontal), static_cast<double>(vertical));
		if (distance < bestDistance) {
			bestDistance = distance;
			best = &candidate;
		}
	}

	return best && focusWindow(best->processId, best->windowId);
}

}

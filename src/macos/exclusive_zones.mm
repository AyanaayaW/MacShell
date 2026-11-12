#include "exclusive_zones.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <dlfcn.h>
#include <mutex>
#include <optional>

#include <qlist.h>
#include <qmargins.h>
#include <qobject.h>
#include <qrect.h>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

extern "C" AXError _AXUIElementGetWindow(AXUIElementRef element, CGWindowID* outWindowId);

namespace qs::macos {

namespace {

struct SkyLightWindowApi {
	int (*mainConnection)() = nullptr;
	int (*getWindowBounds)(int, uint32_t, CGRect*) = nullptr;
	int connection = 0;

	[[nodiscard]] bool available() const { return mainConnection && getWindowBounds; }
};

const SkyLightWindowApi& skyLightWindows() {
	static const SkyLightWindowApi api = [] {
		SkyLightWindowApi loaded;
		void* handle = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
		if (!handle) return loaded;
		loaded.mainConnection = reinterpret_cast<int (*)()>(dlsym(handle, "SLSMainConnectionID"));
		loaded.getWindowBounds =
		    reinterpret_cast<int (*)(int, uint32_t, CGRect*)>(dlsym(handle, "SLSGetWindowBounds"));
		if (loaded.mainConnection) loaded.connection = loaded.mainConnection();
		return loaded;
	}();
	return api;
}

dispatch_queue_t enforcementQueue() {
	static dispatch_queue_t queue = dispatch_queue_create("MacShell.ExclusiveZones", DISPATCH_QUEUE_SERIAL);
	return queue;
}

std::atomic<bool>& enforcementRunning() {
	static std::atomic<bool> running = false;
	return running;
}

std::mutex& reservationMutex() {
	static std::mutex mutex;
	return mutex;
}

QList<ScreenReservation>& sharedReservations() {
	static QList<ScreenReservation> reservations;
	return reservations;
}

enum class DragMode {
	None,
	Pending,
	Move,
	Resize,
};

struct ActiveWindowDrag {
	DragMode mode = DragMode::None;
	uint32_t windowId = 0;
	pid_t processId = 0;
	QRect initialFrame;
	QRect usableFrame;
	double highestCursorY = 0;
};

ActiveWindowDrag& activeWindowDrag() {
	static ActiveWindowDrag drag;
	return drag;
}

CFMachPortRef& activeEventTapPort() {
	static CFMachPortRef port = nullptr;
	return port;
}

constexpr int ENFORCE_INTERVAL_MILLISECONDS = 150;
constexpr float ACCESSIBILITY_TIMEOUT_SECONDS = 0.15F;
constexpr int ADJUSTMENT_TOLERANCE = 1;

int hardwareTopInsetForScreen(const QRect& screenGeometry) {
	NSArray<NSScreen*>* screens = NSScreen.screens;
	if (screens.count == 0) return 0;

	double primaryHeight = screens.firstObject.frame.size.height;
	for (NSScreen* screen in screens) {
		NSRect frame = screen.frame;
		QRect topLeftFrame(
		    static_cast<int>(frame.origin.x),
		    static_cast<int>(primaryHeight - frame.origin.y - frame.size.height),
		    static_cast<int>(frame.size.width),
		    static_cast<int>(frame.size.height)
		);
		if (topLeftFrame.contains(screenGeometry.center())) {
			return static_cast<int>(std::ceil(screen.safeAreaInsets.top));
		}
	}
	return 0;
}

QRect effectiveUsableRect(const ScreenReservation& reservation) {
	QMargins insets = reservation.reservedInsets;
	int safeTop = hardwareTopInsetForScreen(reservation.screenGeometry);
	if (safeTop > insets.top()) {
		insets.setTop(safeTop);
	}
	return reservation.screenGeometry.marginsRemoved(insets);
}

id copyAttribute(AXUIElementRef element, CFStringRef attribute) {
	CFTypeRef value = nullptr;
	if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess) return nil;
	return CFBridgingRelease(value);
}

bool isAdjustableWindow(AXUIElementRef window) {
	NSString* subrole = copyAttribute(window, kAXSubroleAttribute);
	if (![subrole isEqualToString:(__bridge NSString*)kAXStandardWindowSubrole]) return false;

	NSNumber* minimized = copyAttribute(window, kAXMinimizedAttribute);
	NSNumber* fullscreen = copyAttribute(window, CFSTR("AXFullScreen"));
	return !minimized.boolValue && !fullscreen.boolValue;
}

QRect windowFrame(AXUIElementRef window) {
	CGPoint position = CGPointZero;
	CGSize size = CGSizeZero;
	id positionValue = copyAttribute(window, kAXPositionAttribute);
	id sizeValue = copyAttribute(window, kAXSizeAttribute);
	if (!positionValue || !sizeValue) return {};

	AXValueGetValue((__bridge AXValueRef)positionValue, kAXValueTypeCGPoint, &position);
	AXValueGetValue((__bridge AXValueRef)sizeValue, kAXValueTypeCGSize, &size);
	return QRect(
	    static_cast<int>(position.x),
	    static_cast<int>(position.y),
	    static_cast<int>(size.width),
	    static_cast<int>(size.height)
	);
}

void applyWindowPosition(AXUIElementRef window, const QPoint& targetOrigin) {
	CGPoint position = CGPointMake(targetOrigin.x(), targetOrigin.y());
	AXValueRef positionValue = AXValueCreate(kAXValueTypeCGPoint, &position);
	AXUIElementSetAttributeValue(window, kAXPositionAttribute, positionValue);
	CFRelease(positionValue);
}

QRect fitWithin(const QRect& frame, const QRect& usable) {
	if (usable.isEmpty() || frame.isEmpty()) return frame;

	int width = std::min(frame.width(), usable.width());
	int height = std::min(frame.height(), usable.height());
	int maxLeft = std::max(usable.left(), usable.right() - width + 1);
	int maxTop = std::max(usable.top(), usable.bottom() - height + 1);
	int left = std::clamp(frame.left(), usable.left(), maxLeft);
	int top = std::clamp(frame.top(), usable.top(), maxTop);
	return QRect(left, top, width, height);
}

std::optional<QRect> usableForFrame(const QRect& frame, const QList<ScreenReservation>& reservations) {
	for (const auto& reservation: reservations) {
		if (reservation.screenGeometry.contains(frame.center())) {
			return effectiveUsableRect(reservation);
		}
	}
	return std::nullopt;
}

std::optional<QRect> skyLightWindowFrame(uint32_t windowId) {
	const auto& api = skyLightWindows();
	if (!api.available() || windowId == 0) return std::nullopt;

	CGRect bounds = CGRectZero;
	if (api.getWindowBounds(api.connection, windowId, &bounds) != 0) return std::nullopt;
	if (bounds.size.width <= 0 || bounds.size.height <= 0) return std::nullopt;

	return QRect(
	    static_cast<int>(bounds.origin.x),
	    static_cast<int>(bounds.origin.y),
	    static_cast<int>(bounds.size.width),
	    static_cast<int>(bounds.size.height)
	);
}

NSArray* candidateWindowElements(AXUIElementRef applicationElement) {
	NSMutableArray* candidates = [NSMutableArray array];
	NSArray* windows = copyAttribute(applicationElement, kAXWindowsAttribute);
	if (windows) [candidates addObjectsFromArray:windows];
	id mainWindow = copyAttribute(applicationElement, kAXMainWindowAttribute);
	if (mainWindow) [candidates addObject:mainWindow];
	id focusedWindow = copyAttribute(applicationElement, kAXFocusedWindowAttribute);
	if (focusedWindow) [candidates addObject:focusedWindow];
	return candidates;
}

void enforceSingleWindowAsync(pid_t processId, uint32_t windowId, QRect usableFrame) {
	if (usableFrame.isEmpty() || enforcementRunning().exchange(true)) return;

	dispatch_async(enforcementQueue(), [processId, windowId, usableFrame] {
		@autoreleasepool {
			AXUIElementRef applicationElement = AXUIElementCreateApplication(processId);
			AXUIElementSetMessagingTimeout(applicationElement, ACCESSIBILITY_TIMEOUT_SECONDS);
			NSArray* windows = candidateWindowElements(applicationElement);

			for (id windowObject in windows) {
				auto window = (__bridge AXUIElementRef)windowObject;
				CGWindowID candidateId = 0;
				if (_AXUIElementGetWindow(window, &candidateId) != kAXErrorSuccess || candidateId != windowId) {
					continue;
				}
				if (!isAdjustableWindow(window)) break;

				auto current = windowFrame(window);
				if (current.isEmpty()) break;

				auto fitted = fitWithin(current, usableFrame);
				if (std::abs(current.x() - fitted.x()) > ADJUSTMENT_TOLERANCE
				    || std::abs(current.y() - fitted.y()) > ADJUSTMENT_TOLERANCE)
				{
					applyWindowPosition(window, fitted.topLeft());
				}
				break;
			}

			CFRelease(applicationElement);
		}
		enforcementRunning() = false;
	});
}

void beginWindowDragCandidate(CGPoint mouseDownLocation) {
	auto& drag = activeWindowDrag();
	drag = ActiveWindowDrag {};

	QList<ScreenReservation> reservations;
	{
		std::lock_guard<std::mutex> lock(reservationMutex());
		reservations = sharedReservations();
	}
	if (reservations.isEmpty()) return;

	pid_t ownProcessId = NSProcessInfo.processInfo.processIdentifier;
	NSMutableSet<NSNumber*>* regularProcessIds = [NSMutableSet set];
	for (NSRunningApplication* application in NSWorkspace.sharedWorkspace.runningApplications) {
		if (application.processIdentifier != ownProcessId
		    && !application.isHidden
		    && application.activationPolicy == NSApplicationActivationPolicyRegular)
		{
			[regularProcessIds addObject:@(application.processIdentifier)];
		}
	}

	CFArrayRef rawWindows = CGWindowListCopyWindowInfo(
	    kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
	    kCGNullWindowID
	);
	if (!rawWindows) return;

	for (NSDictionary* window in (__bridge NSArray*)rawWindows) {
		if ([window[(__bridge NSString*)kCGWindowLayer] intValue] != 0) continue;

		NSNumber* ownerProcessId = window[(__bridge NSString*)kCGWindowOwnerPID];
		if (![regularProcessIds containsObject:ownerProcessId]) continue;

		uint32_t windowId = [window[(__bridge NSString*)kCGWindowNumber] unsignedIntValue];
		auto liveFrame = skyLightWindowFrame(windowId);
		if (!liveFrame.has_value()) continue;
		if (liveFrame->width() < 100 || liveFrame->height() < 100) continue;

		constexpr int edgeGrabSlop = 10;
		QRect hitBounds = liveFrame->adjusted(-edgeGrabSlop, -edgeGrabSlop, edgeGrabSlop, edgeGrabSlop);
		QPoint clickPoint(static_cast<int>(mouseDownLocation.x), static_cast<int>(mouseDownLocation.y));
		if (!hitBounds.contains(clickPoint)) continue;

		auto usable = usableForFrame(*liveFrame, reservations);
		if (!usable.has_value()) break;

		constexpr int resizeBorderInset = 12;
		QRect moveInterior = liveFrame->adjusted(
		    resizeBorderInset,
		    resizeBorderInset,
		    -resizeBorderInset,
		    -resizeBorderInset
		);
		drag.mode = moveInterior.contains(clickPoint) ? DragMode::Pending : DragMode::Resize;
		drag.windowId = windowId;
		drag.processId = ownerProcessId.intValue;
		drag.initialFrame = *liveFrame;
		drag.usableFrame = *usable;
		drag.highestCursorY = usable->top() + (mouseDownLocation.y - liveFrame->top());
		break;
	}

	CFRelease(rawWindows);
}

void handleWindowDragged() {
	auto& drag = activeWindowDrag();
	if (drag.mode == DragMode::None || drag.windowId == 0) return;

	auto liveFrame = skyLightWindowFrame(drag.windowId);
	if (!liveFrame.has_value()) return;

	if (liveFrame->size() != drag.initialFrame.size()) {
		drag.mode = DragMode::Resize;
		return;
	}

	if (drag.mode == DragMode::Pending) {
		if (*liveFrame == drag.initialFrame) return;
		drag.mode = DragMode::Move;
	}
}

void keepDraggedWindowBelowReservedTop(CGEventRef event) {
	const auto& drag = activeWindowDrag();
	if (drag.mode != DragMode::Move) return;

	CGPoint location = CGEventGetLocation(event);
	if (location.y >= drag.highestCursorY) return;
	CGEventSetLocation(event, CGPointMake(location.x, drag.highestCursorY));
	CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, 0);
}

void finishWindowDrag() {
	auto& drag = activeWindowDrag();
	if (drag.mode == DragMode::Move && drag.windowId != 0 && !drag.usableFrame.isEmpty()) {
		pid_t processId = drag.processId;
		uint32_t windowId = drag.windowId;
		QRect usableFrame = drag.usableFrame;
		drag = ActiveWindowDrag {};
		enforceSingleWindowAsync(processId, windowId, usableFrame);
		return;
	}
	drag = ActiveWindowDrag {};
}

CGEventRef dragEventTapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void*) {
	if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
		if (activeEventTapPort()) CGEventTapEnable(activeEventTapPort(), true);
		return event;
	}

	switch (type) {
	case kCGEventLeftMouseDown:
		beginWindowDragCandidate(CGEventGetLocation(event));
		break;
	case kCGEventLeftMouseDragged:
		handleWindowDragged();
		keepDraggedWindowBelowReservedTop(event);
		break;
	case kCGEventLeftMouseUp:
		finishWindowDrag();
		break;
	default:
		break;
	}

	return event;
}

CFRunLoopRef dragTapRunLoop() {
	static CFRunLoopRef runLoop = [] {
		__block CFRunLoopRef startedRunLoop = nullptr;
		dispatch_semaphore_t started = dispatch_semaphore_create(0);
		NSThread* thread = [[NSThread alloc] initWithBlock:^{
			startedRunLoop = CFRunLoopGetCurrent();
			CFRunLoopAddTimer(
			    startedRunLoop,
			    CFRunLoopTimerCreateWithHandler(nullptr, CFAbsoluteTimeGetCurrent() + 1e10, 1e10, 0, 0, ^(CFRunLoopTimerRef) {}),
			    kCFRunLoopCommonModes
			);
			dispatch_semaphore_signal(started);
			CFRunLoopRun();
		}];
		thread.name = @"MacShell window drag watcher";
		thread.qualityOfService = NSQualityOfServiceUserInteractive;
		[thread start];
		dispatch_semaphore_wait(started, DISPATCH_TIME_FOREVER);
		return startedRunLoop;
	}();
	return runLoop;
}

}

ExclusiveZoneEnforcer* ExclusiveZoneEnforcer::instance() {
	static auto* sharedInstance = new ExclusiveZoneEnforcer();
	return sharedInstance;
}

ExclusiveZoneEnforcer::ExclusiveZoneEnforcer() {
	QObject::connect(&this->mEnforceTimer, &QTimer::timeout, this, &ExclusiveZoneEnforcer::enforce);
}

void ExclusiveZoneEnforcer::startDragTap() {
	if (this->mEventTap) return;

	CGEventMask mask = CGEventMaskBit(kCGEventLeftMouseDown)
	                 | CGEventMaskBit(kCGEventLeftMouseDragged)
	                 | CGEventMaskBit(kCGEventLeftMouseUp);
	CFMachPortRef tap = CGEventTapCreate(
	    kCGHIDEventTap,
	    kCGHeadInsertEventTap,
	    kCGEventTapOptionDefault,
	    mask,
	    dragEventTapCallback,
	    nullptr
	);
	if (!tap) return;

	CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
	CFRunLoopAddSource(dragTapRunLoop(), source, kCFRunLoopCommonModes);
	activeEventTapPort() = tap;
	CGEventTapEnable(tap, true);

	this->mEventTap = tap;
	this->mRunLoopSource = source;
}

void ExclusiveZoneEnforcer::stopDragTap() {
	activeEventTapPort() = nullptr;
	if (this->mRunLoopSource) {
		auto source = static_cast<CFRunLoopSourceRef>(this->mRunLoopSource);
		CFRunLoopRemoveSource(dragTapRunLoop(), source, kCFRunLoopCommonModes);
		CFRelease(source);
		this->mRunLoopSource = nullptr;
	}
	if (this->mEventTap) {
		auto tap = static_cast<CFMachPortRef>(this->mEventTap);
		CGEventTapEnable(tap, false);
		CFMachPortInvalidate(tap);
		CFRelease(tap);
		this->mEventTap = nullptr;
	}
}

void ExclusiveZoneEnforcer::setReservations(const QList<ScreenReservation>& reservations) {
	this->mReservations.clear();
	for (const auto& reservation: reservations) {
		if (!reservation.reservedInsets.isNull()) this->mReservations.append(reservation);
	}

	{
		std::lock_guard<std::mutex> lock(reservationMutex());
		sharedReservations() = this->mReservations;
	}

	if (this->mReservations.isEmpty()) {
		this->mEnforceTimer.stop();
		this->stopDragTap();
		return;
	}

	if (!AXIsProcessTrusted() && !this->mRequestedAccessibility) {
		this->mRequestedAccessibility = true;
		NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
		AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
	}

	this->startDragTap();
	if (!this->mEnforceTimer.isActive()) this->mEnforceTimer.start(ENFORCE_INTERVAL_MILLISECONDS);
	this->enforce();
}

QRect ExclusiveZoneEnforcer::usableGeometry(const QRect& screenGeometry) const {
	for (const auto& reservation: this->mReservations) {
		if (reservation.screenGeometry == screenGeometry) {
			return effectiveUsableRect(reservation);
		}
	}
	return screenGeometry;
}

void ExclusiveZoneEnforcer::enforce() {
	if (this->mReservations.isEmpty() || !AXIsProcessTrusted()) return;
	if ((NSEvent.pressedMouseButtons & 1) != 0) return;
	if (enforcementRunning().exchange(true)) return;

	auto reservations = this->mReservations;
	auto ownProcessId = NSProcessInfo.processInfo.processIdentifier;

	dispatch_async(enforcementQueue(), [reservations, ownProcessId] {
		enforceReservations(reservations, ownProcessId);
		enforcementRunning() = false;
	});
}

void ExclusiveZoneEnforcer::enforceReservations(const QList<ScreenReservation>& reservations, pid_t ownProcessId) {
	@autoreleasepool {
		for (NSRunningApplication* application in NSWorkspace.sharedWorkspace.runningApplications) {
			if (application.activationPolicy != NSApplicationActivationPolicyRegular) continue;
			if (application.processIdentifier == ownProcessId || application.isHidden) continue;

			AXUIElementRef applicationElement = AXUIElementCreateApplication(application.processIdentifier);
			AXUIElementSetMessagingTimeout(applicationElement, ACCESSIBILITY_TIMEOUT_SECONDS);
			NSArray* windows = copyAttribute(applicationElement, kAXWindowsAttribute);

			for (id windowObject in windows) {
				auto window = (__bridge AXUIElementRef)windowObject;
				if (!isAdjustableWindow(window)) continue;

				auto frame = windowFrame(window);
				if (frame.isEmpty()) continue;

				for (const auto& reservation: reservations) {
					if (!reservation.screenGeometry.contains(frame.center())) continue;

					auto usable = effectiveUsableRect(reservation);
					auto fitted = fitWithin(frame, usable);
					if (frame.topLeft() != fitted.topLeft()) {
						applyWindowPosition(window, fitted.topLeft());
					}
					break;
				}
			}

			CFRelease(applicationElement);
		}
	}
}

}

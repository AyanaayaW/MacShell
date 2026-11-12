#include "toplevels_backend.hpp"

#include <qhash.h>
#include <qrect.h>
#include <qstring.h>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

extern "C" AXError _AXUIElementGetWindow(AXUIElementRef element, CGWindowID* windowId);

namespace qs::macos {

namespace {

constexpr float ACCESSIBILITY_TIMEOUT_SECONDS = 0.25F;
constexpr double MINIMUM_WINDOW_EDGE = 60;

struct AccessibleWindow {
	AXUIElementRef element = nullptr;
	QString title;
	bool minimized = false;
	bool fullscreen = false;
	QRect frame;
};

id copyAttribute(AXUIElementRef element, CFStringRef attribute) {
	CFTypeRef value = nullptr;
	if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess) return nil;
	return CFBridgingRelease(value);
}

bool boolAttribute(AXUIElementRef element, CFStringRef attribute) {
	id value = copyAttribute(element, attribute);
	return [value isKindOfClass:NSNumber.class] && [value boolValue];
}

QRect frameOf(AXUIElementRef element) {
	CGPoint position = CGPointZero;
	CGSize size = CGSizeZero;

	id positionValue = copyAttribute(element, kAXPositionAttribute);
	id sizeValue = copyAttribute(element, kAXSizeAttribute);
	if (positionValue) AXValueGetValue((__bridge AXValueRef)positionValue, kAXValueTypeCGPoint, &position);
	if (sizeValue) AXValueGetValue((__bridge AXValueRef)sizeValue, kAXValueTypeCGSize, &size);

	return QRect(
	    static_cast<int>(position.x),
	    static_cast<int>(position.y),
	    static_cast<int>(size.width),
	    static_cast<int>(size.height)
	);
}

QHash<quint32, AccessibleWindow> accessibleWindowsFor(qint32 processId) {
	QHash<quint32, AccessibleWindow> windows;
	if (!hasAccessibilityAccess()) return windows;

	AXUIElementRef application = AXUIElementCreateApplication(processId);
	AXUIElementSetMessagingTimeout(application, ACCESSIBILITY_TIMEOUT_SECONDS);

	NSMutableArray* candidates = [NSMutableArray array];
	NSArray* elements = copyAttribute(application, kAXWindowsAttribute);
	if (elements) [candidates addObjectsFromArray:elements];
	id mainWindow = copyAttribute(application, kAXMainWindowAttribute);
	if (mainWindow) [candidates addObject:mainWindow];
	id focusedWindow = copyAttribute(application, kAXFocusedWindowAttribute);
	if (focusedWindow) [candidates addObject:focusedWindow];

	for (id elementObject in candidates) {
		auto element = (__bridge AXUIElementRef)elementObject;

		CGWindowID windowId = 0;
		if (_AXUIElementGetWindow(element, &windowId) != kAXErrorSuccess || windowId == 0) continue;
		if (windows.contains(windowId)) continue;

		NSString* title = copyAttribute(element, kAXTitleAttribute);
		windows.insert(windowId, AccessibleWindow {
		    .element = static_cast<AXUIElementRef>(CFRetain(element)),
		    .title = QString::fromNSString([title isKindOfClass:NSString.class] ? title : @""),
		    .minimized = boolAttribute(element, kAXMinimizedAttribute),
		    .fullscreen = boolAttribute(element, CFSTR("AXFullScreen")),
		    .frame = frameOf(element),
		});
	}

	CFRelease(application);
	return windows;
}

void releaseAccessibleWindows(QHash<quint32, AccessibleWindow>& windows) {
	for (auto& window: windows) {
		if (window.element) CFRelease(window.element);
	}
	windows.clear();
}

template <typename Action>
void withWindowElement(qint32 processId, quint32 windowId, Action action) {
	auto windows = accessibleWindowsFor(processId);
	if (auto window = windows.find(windowId); window != windows.end()) action(window->element);
	releaseAccessibleWindows(windows);
}

NSRunningApplication* applicationFor(qint32 processId) {
	return [NSRunningApplication runningApplicationWithProcessIdentifier:processId];
}

}

bool hasAccessibilityAccess() { return AXIsProcessTrusted(); }

QList<NativeWindow> readNativeWindows() {
	QList<NativeWindow> windows;

	CFArrayRef rawWindows = CGWindowListCopyWindowInfo(
	    kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements,
	    kCGNullWindowID
	);
	if (!rawWindows) return windows;

	QHash<qint32, QHash<quint32, AccessibleWindow>> accessibleWindowsByProcess;
	QHash<qint32, NSRunningApplication*> regularApplications;
	for (NSRunningApplication* application in NSWorkspace.sharedWorkspace.runningApplications) {
		if (application.activationPolicy == NSApplicationActivationPolicyRegular) {
			regularApplications.insert(application.processIdentifier, application);
		}
	}

	for (NSDictionary* window in (__bridge NSArray*)rawWindows) {
		if ([window[(__bridge NSString*)kCGWindowLayer] intValue] != 0) continue;

		auto processId = [window[(__bridge NSString*)kCGWindowOwnerPID] intValue];
		NSRunningApplication* application = regularApplications.value(processId);
		if (!application) continue;

		CGRect bounds = CGRectZero;
		CGRectMakeWithDictionaryRepresentation(
		    (__bridge CFDictionaryRef)window[(__bridge NSString*)kCGWindowBounds],
		    &bounds
		);
		if (bounds.size.width < MINIMUM_WINDOW_EDGE || bounds.size.height < MINIMUM_WINDOW_EDGE) continue;

		if (!accessibleWindowsByProcess.contains(processId)) {
			accessibleWindowsByProcess.insert(processId, accessibleWindowsFor(processId));
		}

		auto windowId = [window[(__bridge NSString*)kCGWindowNumber] unsignedIntValue];
		const auto& accessibleWindows = accessibleWindowsByProcess[processId];
		auto accessible = accessibleWindows.find(windowId);
		auto isOnScreen = [window[(__bridge NSString*)kCGWindowIsOnscreen] boolValue];
		auto hasAccessibleWindow = accessible != accessibleWindows.end();

		if (!isOnScreen && !hasAccessibleWindow && hasAccessibilityAccess()) {
			NSString* windowName = window[(__bridge NSString*)kCGWindowName];
			if (windowName.length == 0) continue;
		}

		NativeWindow nativeWindow {
		    .windowId = windowId,
		    .processId = processId,
		    .appId = QString::fromNSString(application.bundleIdentifier ?: @""),
		    .applicationName = QString::fromNSString(application.localizedName ?: @""),
		    .title = QString::fromNSString(window[(__bridge NSString*)kCGWindowName] ?: @""),
		    .frame = QRect(
		        static_cast<int>(bounds.origin.x),
		        static_cast<int>(bounds.origin.y),
		        static_cast<int>(bounds.size.width),
		        static_cast<int>(bounds.size.height)
		    ),
		};

		if (hasAccessibleWindow) {
			if (!accessible->title.isEmpty()) nativeWindow.title = accessible->title;
			nativeWindow.minimized = accessible->minimized;
			nativeWindow.fullscreen = accessible->fullscreen;
		}
		if (nativeWindow.title.isEmpty()) nativeWindow.title = nativeWindow.applicationName;

		windows.append(nativeWindow);
	}

	for (auto& accessibleWindows: accessibleWindowsByProcess) releaseAccessibleWindows(accessibleWindows);
	CFRelease(rawWindows);
	return windows;
}

quint32 focusedWindowId() {
	pid_t ownProcessId = NSProcessInfo.processInfo.processIdentifier;
	NSRunningApplication* frontmost = NSWorkspace.sharedWorkspace.frontmostApplication;
	bool frontmostIsRegular = frontmost
	                       && frontmost.processIdentifier != ownProcessId
	                       && frontmost.activationPolicy == NSApplicationActivationPolicyRegular;

	if (frontmostIsRegular && hasAccessibilityAccess()) {
		AXUIElementRef application = AXUIElementCreateApplication(frontmost.processIdentifier);
		AXUIElementSetMessagingTimeout(application, ACCESSIBILITY_TIMEOUT_SECONDS);
		id focusedWindow = copyAttribute(application, kAXFocusedWindowAttribute);
		CFRelease(application);

		CGWindowID windowId = 0;
		if (focusedWindow && _AXUIElementGetWindow((__bridge AXUIElementRef)focusedWindow, &windowId) == kAXErrorSuccess) {
			return windowId;
		}
	}

	NSMutableSet<NSNumber*>* regularProcessIds = [NSMutableSet set];
	for (NSRunningApplication* application in NSWorkspace.sharedWorkspace.runningApplications) {
		if (application.processIdentifier != ownProcessId
		    && application.activationPolicy == NSApplicationActivationPolicyRegular)
		{
			[regularProcessIds addObject:@(application.processIdentifier)];
		}
	}

	CFArrayRef rawWindows = CGWindowListCopyWindowInfo(
	    kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
	    kCGNullWindowID
	);
	if (!rawWindows) return 0;

	quint32 windowId = 0;
	for (NSDictionary* window in (__bridge NSArray*)rawWindows) {
		if ([window[(__bridge NSString*)kCGWindowLayer] intValue] != 0) continue;
		NSNumber* ownerProcessId = window[(__bridge NSString*)kCGWindowOwnerPID];
		if (frontmostIsRegular) {
			if (ownerProcessId.intValue != frontmost.processIdentifier) continue;
		} else {
			if (![regularProcessIds containsObject:ownerProcessId]) continue;
		}
		windowId = [window[(__bridge NSString*)kCGWindowNumber] unsignedIntValue];
		break;
	}

	CFRelease(rawWindows);
	return windowId;
}

QRect visibleFrameForWindow(const QRect& frame) {
	auto primaryHeight = NSScreen.screens.firstObject.frame.size.height;
	auto windowCenter = NSMakePoint(frame.center().x(), primaryHeight - frame.center().y());

	NSScreen* targetScreen = NSScreen.mainScreen;
	for (NSScreen* screen in NSScreen.screens) {
		if (NSPointInRect(windowCenter, screen.frame)) targetScreen = screen;
	}

	auto visibleFrame = targetScreen.visibleFrame;
	return QRect(
	    static_cast<int>(visibleFrame.origin.x),
	    static_cast<int>(primaryHeight - visibleFrame.origin.y - visibleFrame.size.height),
	    static_cast<int>(visibleFrame.size.width),
	    static_cast<int>(visibleFrame.size.height)
	);
}

void activateWindow(qint32 processId, quint32 windowId) {
	withWindowElement(processId, windowId, [](AXUIElementRef element) {
		if (boolAttribute(element, kAXMinimizedAttribute)) {
			AXUIElementSetAttributeValue(element, kAXMinimizedAttribute, kCFBooleanFalse);
		}
		AXUIElementPerformAction(element, kAXRaiseAction);
		AXUIElementSetAttributeValue(element, kAXMainAttribute, kCFBooleanTrue);
	});
	[applicationFor(processId) activateWithOptions:NSApplicationActivateAllWindows];
}

void closeWindow(qint32 processId, quint32 windowId) {
	withWindowElement(processId, windowId, [](AXUIElementRef element) {
		id closeButton = copyAttribute(element, kAXCloseButtonAttribute);
		if (closeButton) AXUIElementPerformAction((__bridge AXUIElementRef)closeButton, kAXPressAction);
	});
}

void setWindowMinimized(qint32 processId, quint32 windowId, bool minimized) {
	withWindowElement(processId, windowId, [minimized](AXUIElementRef element) {
		AXUIElementSetAttributeValue(element, kAXMinimizedAttribute, minimized ? kCFBooleanTrue : kCFBooleanFalse);
	});
}

void setWindowFullscreen(qint32 processId, quint32 windowId, bool fullscreen) {
	withWindowElement(processId, windowId, [fullscreen](AXUIElementRef element) {
		AXUIElementSetAttributeValue(element, CFSTR("AXFullScreen"), fullscreen ? kCFBooleanTrue : kCFBooleanFalse);
	});
}

void setWindowFrame(qint32 processId, quint32 windowId, const QRect& frame) {
	withWindowElement(processId, windowId, [&frame](AXUIElementRef element) {
		CGPoint position = CGPointMake(frame.x(), frame.y());
		CGSize size = CGSizeMake(frame.width(), frame.height());
		AXValueRef positionValue = AXValueCreate(kAXValueTypeCGPoint, &position);
		AXValueRef sizeValue = AXValueCreate(kAXValueTypeCGSize, &size);
		AXUIElementSetAttributeValue(element, kAXPositionAttribute, positionValue);
		AXUIElementSetAttributeValue(element, kAXSizeAttribute, sizeValue);
		CFRelease(positionValue);
		CFRelease(sizeValue);
	});
}

}

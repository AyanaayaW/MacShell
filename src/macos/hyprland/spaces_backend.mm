#include "spaces_backend.hpp"
#include <dlfcn.h>
#include <utility>

#include <memory>

#include <qtimer.h>
#include <qfile.h>
#include <qprocess.h>
#include <qset.h>
#include <qstring.h>
#include <qstringlist.h>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

namespace qs::hyprland::macos {

namespace {

using MainConnectionFunction = int (*)();
using CopyManagedDisplaySpacesFunction = CFArrayRef (*)(int connection);
using CopySpacesForWindowsFunction = CFArrayRef (*)(int connection, int selector, CFArrayRef windows);

constexpr int ALL_SPACES_SELECTOR = 7;
constexpr int DESKTOP_SPACE_TYPE = 0;
constexpr int SWITCH_TO_DESKTOP_ONE_HOT_KEY_ID = 118;
constexpr int SWITCH_TO_DESKTOP_MAXIMUM = 16;
constexpr int MOVE_LEFT_SPACE_HOT_KEY_ID = 79;
constexpr int MOVE_RIGHT_SPACE_HOT_KEY_ID = 81;
constexpr useconds_t KEY_STEP_DELAY_MICROSECONDS = 120000;

constexpr int ACTIVE_SPACE_POLL_MILLISECONDS = 16;

struct SkyLight {
	MainConnectionFunction mainConnection = nullptr;
	CopyManagedDisplaySpacesFunction copyManagedDisplaySpaces = nullptr;
	CopySpacesForWindowsFunction copySpacesForWindows = nullptr;
	quint64 (*activeSpace)(int) = nullptr;
	int connection = 0;

	[[nodiscard]] bool isAvailable() const { return this->copyManagedDisplaySpaces != nullptr; }
};

const SkyLight& skyLight() {
	static const SkyLight library = []() {
		SkyLight loaded;
		void* handle = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
		if (!handle) return loaded;

		loaded.mainConnection = reinterpret_cast<MainConnectionFunction>(dlsym(handle, "SLSMainConnectionID"));
		loaded.copyManagedDisplaySpaces = reinterpret_cast<CopyManagedDisplaySpacesFunction>(
		    dlsym(handle, "SLSCopyManagedDisplaySpaces")
		);
		loaded.copySpacesForWindows =
		    reinterpret_cast<CopySpacesForWindowsFunction>(dlsym(handle, "SLSCopySpacesForWindows"));
		loaded.activeSpace = reinterpret_cast<quint64 (*)(int)>(dlsym(handle, "SLSGetActiveSpace"));
		if (loaded.mainConnection) loaded.connection = loaded.mainConnection();
		return loaded;
	}();
	return library;
}

quint64 spaceIdentifier(NSDictionary* space) {
	return [space[@"id64"] unsignedLongLongValue];
}

QString displayIdentifierFor(CGDirectDisplayID displayId) {
	CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(displayId);
	if (!uuid) return {};

	CFStringRef uuidString = CFUUIDCreateString(kCFAllocatorDefault, uuid);
	auto identifier = QString::fromCFString(uuidString);
	CFRelease(uuidString);
	CFRelease(uuid);
	return identifier;
}

CGDirectDisplayID displayIdFor(NSScreen* screen) {
	return [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
}

QList<quint64> spacesForWindow(quint32 windowId) {
	const auto& library = skyLight();
	if (!library.copySpacesForWindows) return {};

	NSArray* windows = @[@(windowId)];
	CFArrayRef spaces = library.copySpacesForWindows(
	    library.connection,
	    ALL_SPACES_SELECTOR,
	    (__bridge CFArrayRef)windows
	);
	if (!spaces) return {};

	QList<quint64> spaceIds;
	for (NSNumber* space in (__bridge NSArray*)spaces) {
		spaceIds.append(space.unsignedLongLongValue);
	}
	CFRelease(spaces);
	return spaceIds;
}

struct HotKey {
	CGKeyCode keyCode = 0;
	CGEventFlags modifiers = 0;
};

std::optional<HotKey> enabledSymbolicHotKey(int hotKeyId) {
	NSDictionary* hotKeys = [[NSUserDefaults alloc] initWithSuiteName:@"com.apple.symbolichotkeys"]
	                            .dictionaryRepresentation[@"AppleSymbolicHotKeys"];
	NSDictionary* entry = hotKeys[[NSString stringWithFormat:@"%d", hotKeyId]];
	if (![entry[@"enabled"] boolValue]) return std::nullopt;

	NSArray* parameters = entry[@"value"][@"parameters"];
	if (parameters.count < 3) return std::nullopt;

	return HotKey {
	    .keyCode = static_cast<CGKeyCode>([parameters[1] unsignedShortValue]),
	    .modifiers = static_cast<CGEventFlags>([parameters[2] unsignedLongLongValue]),
	};
}

std::optional<HotKey> defaultMoveSpaceHotKey(bool movingRight) {
	constexpr CGKeyCode leftArrowKeyCode = 123;
	constexpr CGKeyCode rightArrowKeyCode = 124;
	return HotKey {
	    .keyCode = movingRight ? rightArrowKeyCode : leftArrowKeyCode,
	    .modifiers = kCGEventFlagMaskControl | kCGEventFlagMaskSecondaryFn,
	};
}

bool ensureAccessibilityTrust() {
	NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
	return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void postHotKey(const HotKey& hotKey) {
	CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);

	CGEventRef keyDown = CGEventCreateKeyboardEvent(source, hotKey.keyCode, true);
	CGEventRef keyUp = CGEventCreateKeyboardEvent(source, hotKey.keyCode, false);
	CGEventSetFlags(keyDown, hotKey.modifiers);
	CGEventSetFlags(keyUp, hotKey.modifiers);
	CGEventPost(kCGHIDEventTap, keyDown);
	CGEventPost(kCGHIDEventTap, keyUp);

	CFRelease(keyUp);
	CFRelease(keyDown);
	if (source) CFRelease(source);
}

}

QList<DisplaySpaces> readDisplaySpaces() {
	const auto& library = skyLight();
	if (!library.isAvailable()) return {};

	CFArrayRef rawDisplays = library.copyManagedDisplaySpaces(library.connection);
	if (!rawDisplays) return {};

	auto activeSpace = library.activeSpace ? library.activeSpace(library.connection) : 0;

	QList<DisplaySpaces> displays;
	for (NSDictionary* display in (__bridge NSArray*)rawDisplays) {
		DisplaySpaces description;
		description.displayIdentifier = QString::fromNSString(display[@"Display Identifier"] ?: @"");
		description.currentSpaceId = spaceIdentifier(display[@"Current Space"]);

		for (NSDictionary* space in display[@"Spaces"]) {
			auto isDesktop = [space[@"type"] intValue] == DESKTOP_SPACE_TYPE;
			(isDesktop ? description.desktopSpaceIds : description.fullscreenSpaceIds)
			    .append(spaceIdentifier(space));
		}

		auto ownsActiveSpace = description.desktopSpaceIds.contains(activeSpace)
		                    || description.fullscreenSpaceIds.contains(activeSpace);
		if (ownsActiveSpace) description.currentSpaceId = activeSpace;

		displays.append(description);
	}

	CFRelease(rawDisplays);
	return displays;
}

QList<ScreenDescription> readScreens() {
	QList<ScreenDescription> screens;
	auto primaryHeight = NSScreen.screens.firstObject.frame.size.height;

	for (NSScreen* screen in NSScreen.screens) {
		auto frame = screen.frame;
		auto displayId = displayIdFor(screen);

		screens.append(ScreenDescription {
		    .name = QString::fromNSString(screen.localizedName),
		    .displayIdentifier = displayIdentifierFor(displayId),
		    .frame = QRect(
		        static_cast<int>(frame.origin.x),
		        static_cast<int>(primaryHeight - frame.origin.y - frame.size.height),
		        static_cast<int>(frame.size.width),
		        static_cast<int>(frame.size.height)
		    ),
		    .scale = screen.backingScaleFactor,
		    .displayId = displayId,
		});
	}

	return screens;
}

QList<WindowDescription> readWindows() {
	constexpr double minimumWindowEdge = 60;

	QList<WindowDescription> windows;
	CFArrayRef rawWindows = CGWindowListCopyWindowInfo(
	    kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements,
	    kCGNullWindowID
	);
	if (!rawWindows) return windows;

	NSMutableDictionary<NSNumber*, NSRunningApplication*>* regularApplications =
	    [NSMutableDictionary dictionary];
	for (NSRunningApplication* application in NSWorkspace.sharedWorkspace.runningApplications) {
		if (application.activationPolicy == NSApplicationActivationPolicyRegular) {
			regularApplications[@(application.processIdentifier)] = application;
		}
	}

	for (NSDictionary* window in (__bridge NSArray*)rawWindows) {
		if ([window[(__bridge NSString*)kCGWindowLayer] intValue] != 0) continue;

		NSNumber* processId = window[(__bridge NSString*)kCGWindowOwnerPID];
		NSRunningApplication* application = regularApplications[processId];
		if (!application) continue;

		CGRect bounds = CGRectZero;
		CGRectMakeWithDictionaryRepresentation(
		    (__bridge CFDictionaryRef)window[(__bridge NSString*)kCGWindowBounds],
		    &bounds
		);
		if (bounds.size.width < minimumWindowEdge || bounds.size.height < minimumWindowEdge) continue;

		auto windowId = [window[(__bridge NSString*)kCGWindowNumber] unsignedIntValue];
		auto spaceIds = spacesForWindow(windowId);
		if (spaceIds.isEmpty()) continue;

		windows.append(WindowDescription {
		    .windowId = windowId,
		    .processId = processId.intValue,
		    .applicationName = QString::fromNSString(application.localizedName ?: @""),
		    .bundleIdentifier = QString::fromNSString(application.bundleIdentifier ?: @""),
		    .title = QString::fromNSString(window[(__bridge NSString*)kCGWindowName] ?: @""),
		    .frame = QRect(
		        static_cast<int>(bounds.origin.x),
		        static_cast<int>(bounds.origin.y),
		        static_cast<int>(bounds.size.width),
		        static_cast<int>(bounds.size.height)
		    ),
		    .spaceIds = spaceIds,
		});
	}

	CFRelease(rawWindows);
	return windows;
}

quint32 frontmostWindowId() {
	pid_t ownProcessId = NSProcessInfo.processInfo.processIdentifier;
	NSRunningApplication* frontmost = NSWorkspace.sharedWorkspace.frontmostApplication;
	bool frontmostIsRegular = frontmost
	                       && frontmost.processIdentifier != ownProcessId
	                       && frontmost.activationPolicy == NSApplicationActivationPolicyRegular;

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

QString focusedDisplayIdentifier() {
	auto mouseLocation = NSEvent.mouseLocation;
	for (NSScreen* screen in NSScreen.screens) {
		if (NSPointInRect(mouseLocation, screen.frame)) {
			return displayIdentifierFor(displayIdFor(screen));
		}
	}
	return NSScreen.mainScreen ? displayIdentifierFor(displayIdFor(NSScreen.mainScreen)) : QString();
}

bool switchToDesktop(int globalDesktopNumber, int stepsOnCurrentDisplay) {
	if (!ensureAccessibilityTrust()) return false;

	if (globalDesktopNumber >= 1 && globalDesktopNumber <= SWITCH_TO_DESKTOP_MAXIMUM) {
		if (auto hotKey = enabledSymbolicHotKey(SWITCH_TO_DESKTOP_ONE_HOT_KEY_ID + globalDesktopNumber - 1)) {
			postHotKey(*hotKey);
			return true;
		}
	}

	if (stepsOnCurrentDisplay == 0) return true;

	auto movingRight = stepsOnCurrentDisplay > 0;
	auto hotKey = enabledSymbolicHotKey(movingRight ? MOVE_RIGHT_SPACE_HOT_KEY_ID : MOVE_LEFT_SPACE_HOT_KEY_ID);
	if (!hotKey) hotKey = defaultMoveSpaceHotKey(movingRight);

	for (int step = 0; step < std::abs(stepsOnCurrentDisplay); ++step) {
		if (step > 0) usleep(KEY_STEP_DELAY_MICROSECONDS);
		postHotKey(*hotKey);
	}
	return true;
}

void launchDetached(const QString& command) {
	QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

SystemEventObserver::SystemEventObserver(
    std::function<void()> onSpacesChanged,
    std::function<void()> onFocusChanged
) {
	NSNotificationCenter* workspaceCenter = NSWorkspace.sharedWorkspace.notificationCenter;

	auto spacesHandler = std::make_shared<std::function<void()>>(std::move(onSpacesChanged));
	auto focusHandler = std::make_shared<std::function<void()>>(std::move(onFocusChanged));

	auto observe = [&](NSNotificationCenter* center, NSNotificationName name, auto handler) {
		id token = [center addObserverForName:name
		                               object:nil
		                                queue:nil
		                           usingBlock:^(NSNotification*) { (*handler)(); }];
		this->registrations.append(Registration {
		    .notificationCenter = (__bridge void*)center,
		    .observerToken = (__bridge_retained void*)token,
		});
	};

	observe(workspaceCenter, NSWorkspaceActiveSpaceDidChangeNotification, spacesHandler);

	if (skyLight().activeSpace) {
		auto* poller = new QTimer();
		poller->setInterval(ACTIVE_SPACE_POLL_MILLISECONDS);
		auto lastActiveSpace = std::make_shared<quint64>(skyLight().activeSpace(skyLight().connection));
		QObject::connect(poller, &QTimer::timeout, [spacesHandler, lastActiveSpace] {
			auto activeSpace = skyLight().activeSpace(skyLight().connection);
			if (activeSpace == *lastActiveSpace) return;
			*lastActiveSpace = activeSpace;
			(*spacesHandler)();
		});
		poller->start();
		this->activeSpacePoller = poller;
	}
	observe(NSNotificationCenter.defaultCenter, NSApplicationDidChangeScreenParametersNotification, spacesHandler);
	observe(workspaceCenter, NSWorkspaceDidActivateApplicationNotification, focusHandler);
	observe(workspaceCenter, NSWorkspaceDidLaunchApplicationNotification, focusHandler);
	observe(workspaceCenter, NSWorkspaceDidTerminateApplicationNotification, focusHandler);
}

SystemEventObserver::~SystemEventObserver() {
	delete this->activeSpacePoller;
	for (const auto& registration: this->registrations) {
		id token = (__bridge_transfer id)registration.observerToken;
		auto* center = (__bridge NSNotificationCenter*)registration.notificationCenter;
		[center removeObserver:token];
	}
}

}

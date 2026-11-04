#include "cocoa.hpp"

#include <array>
#include <dlfcn.h>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace qs::macos {

namespace {

constexpr int BACKGROUND_LAYER = 0;
constexpr int BOTTOM_LAYER = 1;
constexpr int TOP_LAYER = 2;

constexpr int OVERLAY_SPACE_ABSOLUTE_LEVEL = 100;
constexpr int ALL_SPACES_MASK = 7;

struct SkyLightSpaceApi {
	int (*mainConnection)() = nullptr;
	uint64_t (*createSpace)(int, int, CFDictionaryRef) = nullptr;
	void (*setAbsoluteLevel)(int, uint64_t, int) = nullptr;
	void (*showSpaces)(int, CFArrayRef) = nullptr;
	void (*addWindows)(int, uint64_t, CFArrayRef, int) = nullptr;
	void (*removeWindows)(int, CFArrayRef, CFArrayRef) = nullptr;

	[[nodiscard]] bool available() const {
		return mainConnection && createSpace && setAbsoluteLevel && showSpaces && addWindows && removeWindows;
	}
};

const SkyLightSpaceApi& skyLightSpaces() {
	static const SkyLightSpaceApi api = [] {
		SkyLightSpaceApi loaded;
		void* skyLight = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
		if (!skyLight) return loaded;
		loaded.mainConnection = reinterpret_cast<int (*)()>(dlsym(skyLight, "SLSMainConnectionID"));
		loaded.createSpace = reinterpret_cast<uint64_t (*)(int, int, CFDictionaryRef)>(dlsym(skyLight, "SLSSpaceCreate"));
		loaded.setAbsoluteLevel =
		    reinterpret_cast<void (*)(int, uint64_t, int)>(dlsym(skyLight, "SLSSpaceSetAbsoluteLevel"));
		loaded.showSpaces = reinterpret_cast<void (*)(int, CFArrayRef)>(dlsym(skyLight, "SLSShowSpaces"));
		loaded.addWindows = reinterpret_cast<void (*)(int, uint64_t, CFArrayRef, int)>(
		    dlsym(skyLight, "SLSSpaceAddWindowsAndRemoveFromSpaces")
		);
		loaded.removeWindows =
		    reinterpret_cast<void (*)(int, CFArrayRef, CFArrayRef)>(dlsym(skyLight, "SLSRemoveWindowsFromSpaces"));
		return loaded;
	}();
	return api;
}

uint64_t overlaySpace() {
	static const uint64_t space = [] -> uint64_t {
		const auto& api = skyLightSpaces();
		if (!api.available()) return 0;
		auto connection = api.mainConnection();
		auto createdSpace = api.createSpace(connection, 1, nullptr);
		if (createdSpace == 0) return 0;
		api.setAbsoluteLevel(connection, createdSpace, OVERLAY_SPACE_ABSOLUTE_LEVEL);
		api.showSpaces(connection, (__bridge CFArrayRef) @[@(createdSpace)]);
		return createdSpace;
	}();
	return space;
}

void placeInOverlaySpace(NSWindow* window, bool inOverlay) {
	const auto& api = skyLightSpaces();
	auto space = overlaySpace();
	if (space == 0 || window.windowNumber <= 0) return;

	NSArray* windowNumbers = @[@(window.windowNumber)];
	if (inOverlay) {
		api.addWindows(api.mainConnection(), space, (__bridge CFArrayRef)windowNumbers, ALL_SPACES_MASK);
	} else {
		api.removeWindows(api.mainConnection(), (__bridge CFArrayRef)windowNumbers, (__bridge CFArrayRef) @[@(space)]);
	}
}

NSWindow* windowForHandle(WId viewHandle) {
	auto* view = (__bridge NSView*)reinterpret_cast<void*>(viewHandle);
	return view.window;
}

NSWindowLevel windowLevelForLayer(int layer) {
	switch (layer) {
	case BACKGROUND_LAYER: return CGWindowLevelForKey(kCGDesktopWindowLevelKey) + 1;
	case BOTTOM_LAYER: return CGWindowLevelForKey(kCGDesktopIconWindowLevelKey) + 1;
	case TOP_LAYER: return NSStatusWindowLevel;
	default: return CGWindowLevelForKey(kCGOverlayWindowLevelKey);
	}
}

}

void configurePanel(WId viewHandle, int layer) {
	NSWindow* window = windowForHandle(viewHandle);
	if (!window) return;

	window.level = windowLevelForLayer(layer);
	window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
	                          | NSWindowCollectionBehaviorStationary
	                          | NSWindowCollectionBehaviorIgnoresCycle
	                          | NSWindowCollectionBehaviorFullScreenAuxiliary;
	window.hasShadow = NO;
	window.hidesOnDeactivate = NO;
	window.opaque = NO;
	placeInOverlaySpace(window, layer >= TOP_LAYER);
}

void focusPanel(WId viewHandle) {
	NSWindow* window = windowForHandle(viewHandle);
	if (!window) return;

	[NSApp activateIgnoringOtherApps:YES];
	[window makeKeyAndOrderFront:nil];
}

namespace {

constexpr NSApplicationPresentationOptions LOCKDOWN_PRESENTATION_OPTIONS =
    NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar
    | NSApplicationPresentationDisableProcessSwitching | NSApplicationPresentationDisableForceQuit
    | NSApplicationPresentationDisableSessionTermination
    | NSApplicationPresentationDisableHideApplication | NSApplicationPresentationDisableAppleMenu;

struct LockdownState {
	bool active = false;
	NSRunningApplication* previouslyFrontmost = nil;
	id resignObserver = nil;
};

LockdownState& lockdownState() {
	static LockdownState state;
	return state;
}

}

void configureLockWindow(WId viewHandle) {
	NSWindow* window = windowForHandle(viewHandle);
	if (!window) return;

	window.level = CGShieldingWindowLevel();
	window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
	                          | NSWindowCollectionBehaviorStationary
	                          | NSWindowCollectionBehaviorIgnoresCycle
	                          | NSWindowCollectionBehaviorFullScreenAuxiliary;
	window.hasShadow = NO;
	window.hidesOnDeactivate = NO;
	window.styleMask = NSWindowStyleMaskBorderless;
	if (window.screen) [window setFrame:window.screen.frame display:YES];
	[window makeKeyAndOrderFront:nil];
}

void beginLockdown() {
	auto& state = lockdownState();
	if (state.active) return;

	NSRunningApplication* frontmost = NSWorkspace.sharedWorkspace.frontmostApplication;
	if (frontmost.processIdentifier != NSProcessInfo.processInfo.processIdentifier) {
		state.previouslyFrontmost = frontmost;
	}

	[NSApp activateIgnoringOtherApps:YES];
	NSApp.presentationOptions = LOCKDOWN_PRESENTATION_OPTIONS;
	state.resignObserver = [NSNotificationCenter.defaultCenter
	    addObserverForName:NSApplicationDidResignActiveNotification
	                object:nil
	                 queue:nil
	            usingBlock:^(NSNotification*) { [NSApp activateIgnoringOtherApps:YES]; }];
	state.active = true;
}

void endLockdown() {
	auto& state = lockdownState();
	if (!state.active) return;

	if (state.resignObserver) [NSNotificationCenter.defaultCenter removeObserver:state.resignObserver];
	state.resignObserver = nil;
	NSApp.presentationOptions = NSApplicationPresentationDefault;
	[state.previouslyFrontmost activateWithOptions:0];
	state.previouslyFrontmost = nil;
	state.active = false;
}

namespace {

void lockWithSystemLockScreen() {
	using LockScreenFunction = int (*)();
	void* loginFramework = dlopen("/System/Library/PrivateFrameworks/login.framework/login", RTLD_LAZY);
	auto lockScreen = loginFramework
	                    ? reinterpret_cast<LockScreenFunction>(dlsym(loginFramework, "SACLockScreenImmediate"))
	                    : nullptr;
	if (lockScreen) lockScreen();
}

}

void showFallbackLockShield() {
	static NSMutableArray<NSWindow*>* shieldWindows = [NSMutableArray array];
	if (shieldWindows.count > 0) return;

	beginLockdown();
	for (NSScreen* screen in NSScreen.screens) {
		NSWindow* shield = [[NSWindow alloc] initWithContentRect:screen.frame
		                                               styleMask:NSWindowStyleMaskBorderless
		                                                 backing:NSBackingStoreBuffered
		                                                   defer:NO];
		shield.backgroundColor = NSColor.blackColor;
		shield.releasedWhenClosed = NO;
		shield.level = CGShieldingWindowLevel();
		shield.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
		                          | NSWindowCollectionBehaviorStationary
		                          | NSWindowCollectionBehaviorFullScreenAuxiliary;
		[shield setFrame:screen.frame display:YES];
		[shield orderFrontRegardless];
		[shieldWindows addObject:shield];
	}

	__block id unlockObserver = nil;
	unlockObserver = [NSDistributedNotificationCenter.defaultCenter
	    addObserverForName:@"com.apple.screenIsUnlocked"
	                object:nil
	                 queue:nil
	            usingBlock:^(NSNotification*) {
		            for (NSWindow* shield in shieldWindows) [shield orderOut:nil];
		            [shieldWindows removeAllObjects];
		            endLockdown();
		            [NSDistributedNotificationCenter.defaultCenter removeObserver:unlockObserver];
		            unlockObserver = nil;
	            }];

	lockWithSystemLockScreen();
}

bool isLockdownActive() {
	return lockdownState().active
	    && (NSApp.currentSystemPresentationOptions & NSApplicationPresentationDisableProcessSwitching) != 0;
}

namespace {

NSString* const BLUR_VIEW_IDENTIFIER = @"MacShellBackgroundBlur";

NSVisualEffectView* existingBlurView(NSView* container) {
	for (NSView* subview in container.subviews) {
		if ([subview.identifier isEqualToString:BLUR_VIEW_IDENTIFIER]) return (NSVisualEffectView*)subview;
	}
	return nil;
}

NSImage* nativeMaskImage(const QImage& maskImage) {
	auto image = maskImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGContextRef context = CGBitmapContextCreate(
	    image.bits(),
	    static_cast<size_t>(image.width()),
	    static_cast<size_t>(image.height()),
	    8,
	    static_cast<size_t>(image.bytesPerLine()),
	    colorSpace,
	    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host
	);
	CGColorSpaceRelease(colorSpace);
	if (!context) return nil;

	CGImageRef cgImage = CGBitmapContextCreateImage(context);
	CGContextRelease(context);
	if (!cgImage) return nil;

	NSImage* nativeImage = [[NSImage alloc] initWithCGImage:cgImage
	                                                   size:NSMakeSize(image.width(), image.height())];
	CGImageRelease(cgImage);
	return nativeImage;
}

}

void setBackgroundBlur(WId viewHandle, const QImage& maskImage) {
	auto* contentView = (__bridge NSView*)reinterpret_cast<void*>(viewHandle);
	NSView* container = contentView.superview;
	if (!container) return;

	NSVisualEffectView* blurView = existingBlurView(container);
	if (!blurView) {
		blurView = [[NSVisualEffectView alloc] initWithFrame:contentView.frame];
		blurView.identifier = BLUR_VIEW_IDENTIFIER;
		blurView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
		blurView.material = NSVisualEffectMaterialHUDWindow;
		blurView.state = NSVisualEffectStateActive;
		blurView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		[container addSubview:blurView positioned:NSWindowBelow relativeTo:contentView];
	}

	blurView.frame = contentView.frame;
	blurView.maskImage = nativeMaskImage(maskImage);
}

void removeBackgroundBlur(WId viewHandle) {
	auto* contentView = (__bridge NSView*)reinterpret_cast<void*>(viewHandle);
	[existingBlurView(contentView.superview) removeFromSuperview];
}

void setAccessoryApp() {
	NSRunningApplication* previouslyFrontmost = NSWorkspace.sharedWorkspace.frontmostApplication;
	[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

	auto ownProcessId = NSProcessInfo.processInfo.processIdentifier;
	if (!previouslyFrontmost || previouslyFrontmost.processIdentifier == ownProcessId) return;

	__block id startupActivationObserver = nil;
	startupActivationObserver = [NSNotificationCenter.defaultCenter
	    addObserverForName:NSApplicationDidBecomeActiveNotification
	                object:nil
	                 queue:nil
	            usingBlock:^(NSNotification*) {
		            [previouslyFrontmost activateWithOptions:0];
		            [NSNotificationCenter.defaultCenter removeObserver:startupActivationObserver];
		            startupActivationObserver = nil;
	            }];
}

namespace {

struct ManagedPreference {
	CFStringRef domain;
	CFStringRef key;
	id hiddenValue;
};

const std::array<ManagedPreference, 3>& managedPreferences() {
	static const std::array<ManagedPreference, 3> preferences {{
	    {CFSTR("com.apple.dock"), CFSTR("autohide"), @YES},
	    {CFSTR("com.apple.dock"), CFSTR("autohide-delay"), @1000.0},
	    {kCFPreferencesAnyApplication, CFSTR("_HIHideMenuBar"), @YES},
	}};
	return preferences;
}

bool systemUiHidden = false;

NSString* backupFilePath() {
	NSString* directory = [NSHomeDirectory()
	    stringByAppendingPathComponent:@"Library/Application Support/MacShell"];
	[[NSFileManager defaultManager] createDirectoryAtPath:directory
	                          withIntermediateDirectories:YES
	                                           attributes:nil
	                                                error:nil];
	return [directory stringByAppendingPathComponent:@"system-ui-backup.plist"];
}

NSString* backupKey(const ManagedPreference& preference) {
	return [NSString stringWithFormat:@"%@|%@",
	                                  (__bridge NSString*)preference.domain,
	                                  (__bridge NSString*)preference.key];
}

void writePreference(const ManagedPreference& preference, id value) {
	CFPreferencesSetAppValue(preference.key, (__bridge CFPropertyListRef)value, preference.domain);
	CFPreferencesAppSynchronize(preference.domain);
}

void applyPreferenceChanges() {
	[[NSDistributedNotificationCenter defaultCenter]
	    postNotificationName:@"AppleInterfaceMenuBarHidingChangedNotification"
	                  object:nil];

	NSArray<NSRunningApplication*>* dockProcesses =
	    [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"];
	for (NSRunningApplication* dockProcess in dockProcesses) {
		[dockProcess terminate];
	}
}

}

void restoreSystemUi() {
	NSString* path = backupFilePath();
	NSDictionary* backup = [NSDictionary dictionaryWithContentsOfFile:path];
	if (!backup) return;

	for (const auto& preference: managedPreferences()) {
		writePreference(preference, backup[backupKey(preference)]);
	}

	applyPreferenceChanges();
	[[NSFileManager defaultManager] removeItemAtPath:path error:nil];
	systemUiHidden = false;
}

void hideSystemUi() {
	if (systemUiHidden) return;
	restoreSystemUi();

	NSMutableDictionary* backup = [NSMutableDictionary dictionary];
	for (const auto& preference: managedPreferences()) {
		id currentValue =
		    CFBridgingRelease(CFPreferencesCopyAppValue(preference.key, preference.domain));
		if (currentValue) backup[backupKey(preference)] = currentValue;
	}

	if (![backup writeToFile:backupFilePath() atomically:YES]) {
		NSLog(@"MacShell: unable to save system UI backup, leaving Dock and menu bar unchanged");
		return;
	}

	for (const auto& preference: managedPreferences()) {
		writePreference(preference, preference.hiddenValue);
	}

	applyPreferenceChanges();
	systemUiHidden = true;
}

}

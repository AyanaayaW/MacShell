#include "cocoa.hpp"

#include <array>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace qs::macos {

namespace {

constexpr int BACKGROUND_LAYER = 0;
constexpr int BOTTOM_LAYER = 1;
constexpr int TOP_LAYER = 2;

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
	                 queue:NSOperationQueue.mainQueue
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
	                 queue:NSOperationQueue.mainQueue
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
		id originalValue = backup[backupKey(preference)];
		writePreference(preference, [originalValue isKindOfClass:[NSNull class]] ? nil : originalValue);
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
		backup[backupKey(preference)] = currentValue ?: [NSNull null];
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

#include "systemtray.hpp"

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>

#include "../../core/iconimageprovider.hpp"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace qs::service::sni {

namespace {

Q_LOGGING_CATEGORY(logSystemTray, "quickshell.service.sni.macos", QtWarningMsg);

constexpr int POLL_INTERVAL_MILLISECONDS = 5000;
constexpr int REFRESH_DEBOUNCE_MILLISECONDS = 500;
constexpr float ACCESSIBILITY_TIMEOUT_SECONDS = 0.15F;

id copyAttribute(AXUIElementRef element, CFStringRef attribute) {
	CFTypeRef value = nullptr;
	if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess) return nil;
	return CFBridgingRelease(value);
}

QString stringAttribute(AXUIElementRef element, CFStringRef attribute) {
	id value = copyAttribute(element, attribute);
	return [value isKindOfClass:NSString.class] ? QString::fromNSString(value) : QString();
}

bool isSystemOwnedApplication(NSRunningApplication* application) {
	static NSSet<NSString*>* systemBundles = [NSSet setWithArray:@[
		@"com.apple.controlcenter",
		@"com.apple.systemuiserver",
		@"com.apple.Spotlight",
		@"com.apple.TextInputMenuAgent",
		@"com.apple.Siri",
		@"com.apple.MenuBarAgent",
	]];
	return [systemBundles containsObject:application.bundleIdentifier ?: @""];
}

NSArray* menuExtrasFor(pid_t processId) {
	AXUIElementRef application = AXUIElementCreateApplication(processId);
	AXUIElementSetMessagingTimeout(application, ACCESSIBILITY_TIMEOUT_SECONDS);
	id extrasBar = copyAttribute(application, CFSTR("AXExtrasMenuBar"));
	CFRelease(application);
	if (!extrasBar) return @[];

	NSArray* children = copyAttribute((__bridge AXUIElementRef)extrasBar, kAXChildrenAttribute);
	return children ?: @[];
}

QList<MenuExtraInfo> readMenuExtras() {
	QList<MenuExtraInfo> extras;
	if (!AXIsProcessTrusted()) return extras;

	auto ownProcessId = NSProcessInfo.processInfo.processIdentifier;

	@autoreleasepool {
		for (NSRunningApplication* application in NSWorkspace.sharedWorkspace.runningApplications) {
			if (application.processIdentifier == ownProcessId || isSystemOwnedApplication(application)) continue;
			if (application.activationPolicy == NSApplicationActivationPolicyProhibited) continue;

			NSArray* items = menuExtrasFor(application.processIdentifier);
			qint32 indexInApplication = 0;

			for (id itemObject in items) {
				auto item = (__bridge AXUIElementRef)itemObject;
				auto bundleIdentifier = QString::fromNSString(application.bundleIdentifier ?: @"");
				auto applicationName = QString::fromNSString(application.localizedName ?: @"");

				MenuExtraInfo info;
				info.processId = application.processIdentifier;
				info.indexInApplication = indexInApplication;
				info.bundleIdentifier = bundleIdentifier;
				info.applicationName = applicationName;
				info.identifier = (bundleIdentifier.isEmpty() ? applicationName : bundleIdentifier)
				                + u'#' + QString::number(indexInApplication);
				info.title = stringAttribute(item, kAXTitleAttribute);
				if (info.title.isEmpty()) info.title = stringAttribute(item, kAXDescriptionAttribute);
				if (info.title.isEmpty()) info.title = applicationName;
				info.help = stringAttribute(item, kAXHelpAttribute);

				extras.append(info);
				++indexInApplication;
			}
		}
	}

	return extras;
}

void performOnMenuExtra(const MenuExtraInfo& info, CFStringRef action) {
	if (!AXIsProcessTrusted()) {
		qCWarning(logSystemTray) << "Opening tray menus needs Accessibility permission for MacShell.";
		return;
	}

	NSArray* items = menuExtrasFor(info.processId);
	if (info.indexInApplication >= static_cast<qint32>(items.count)) return;

	auto item = (__bridge AXUIElementRef)items[static_cast<NSUInteger>(info.indexInApplication)];
	AXUIElementPerformAction(item, action);
}

}

void StatusNotifierItem::applyInfo(const MenuExtraInfo& info) {
	this->mInfo = info;

	Qt::beginPropertyUpdateGroup();
	this->bId = info.identifier;
	this->bTitle = info.title;
	this->bIcon = IconImageProvider::requestString(
	    info.bundleIdentifier.isEmpty() ? info.applicationName : info.bundleIdentifier
	);
	Qt::endPropertyUpdateGroup();

	if (info.title != this->mTooltipTitle) {
		this->mTooltipTitle = info.title;
		emit this->tooltipTitleChanged();
	}
	if (info.help != this->mTooltipDescription) {
		this->mTooltipDescription = info.help;
		emit this->tooltipDescriptionChanged();
	}
}

void StatusNotifierItem::activate() { performOnMenuExtra(this->mInfo, kAXPressAction); }

void StatusNotifierItem::secondaryActivate() { performOnMenuExtra(this->mInfo, kAXShowMenuAction); }

void StatusNotifierItem::scroll([[maybe_unused]] qint32 delta, [[maybe_unused]] bool horizontal) {}

void StatusNotifierItem::display(
    [[maybe_unused]] QObject* parentWindow,
    [[maybe_unused]] qint32 relativeX,
    [[maybe_unused]] qint32 relativeY
) {
	performOnMenuExtra(this->mInfo, kAXPressAction);
}

SystemTrayHost* SystemTrayHost::instance() {
	static auto* sharedInstance = new SystemTrayHost();
	return sharedInstance;
}

SystemTrayHost::SystemTrayHost() {
	this->mRefreshDebounce.setSingleShot(true);
	this->mRefreshDebounce.setInterval(REFRESH_DEBOUNCE_MILLISECONDS);
	QObject::connect(&this->mRefreshDebounce, &QTimer::timeout, this, &SystemTrayHost::refresh);

	QObject::connect(&this->mPollTimer, &QTimer::timeout, this, &SystemTrayHost::refresh);
	this->mPollTimer.start(POLL_INTERVAL_MILLISECONDS);

	NSNotificationCenter* workspaceCenter = NSWorkspace.sharedWorkspace.notificationCenter;
	for (NSNotificationName name in @[NSWorkspaceDidLaunchApplicationNotification, NSWorkspaceDidTerminateApplicationNotification]) {
		[workspaceCenter addObserverForName:name
		                             object:nil
		                              queue:nil
		                         usingBlock:^(NSNotification*) { SystemTrayHost::instance()->scheduleRefresh(); }];
	}

	if (!AXIsProcessTrusted()) {
		NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
		AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
	}

	this->refresh();
}

void SystemTrayHost::scheduleRefresh() { this->mRefreshDebounce.start(); }

void SystemTrayHost::refresh() {
	QSet<QString> presentIdentifiers;

	for (const auto& info: readMenuExtras()) {
		presentIdentifiers.insert(info.identifier);

		auto* item = this->mItemsByIdentifier.value(info.identifier);
		if (!item) {
			item = new StatusNotifierItem(this);
			item->applyInfo(info);
			this->mItemsByIdentifier.insert(info.identifier, item);
			this->mItems.insertObject(item);
			continue;
		}

		item->applyInfo(info);
	}

	for (auto iterator = this->mItemsByIdentifier.begin(); iterator != this->mItemsByIdentifier.end();) {
		if (presentIdentifiers.contains(iterator.key())) {
			++iterator;
			continue;
		}

		auto* removedItem = iterator.value();
		iterator = this->mItemsByIdentifier.erase(iterator);
		this->mItems.removeObject(removedItem);
		removedItem->deleteLater();
	}
}

}

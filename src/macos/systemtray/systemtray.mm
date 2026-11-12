#include "systemtray.hpp"

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qset.h>
#include <qstring.h>

#include "../../core/iconimageprovider.hpp"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <unistd.h>

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
	if (AXUIElementPerformAction(item, action) != kAXErrorSuccess) {
		AXUIElementPerformAction(item, kAXPressAction);
	}
}

constexpr int MENU_OPEN_ATTEMPTS = 20;
constexpr useconds_t MENU_OPEN_WAIT_MICROSECONDS = 25000;
constexpr int MAXIMUM_SUBMENU_DEPTH = 3;
constexpr float MENU_ACTION_TIMEOUT_SECONDS = 0.5F;

dispatch_queue_t menuQueue() {
	static dispatch_queue_t queue = dispatch_queue_create("MacShell.SystemTrayMenus", DISPATCH_QUEUE_SERIAL);
	return queue;
}

id menuExtraElement(const MenuExtraInfo& info) {
	NSArray* items = menuExtrasFor(info.processId);
	if (info.indexInApplication >= static_cast<qint32>(items.count)) return nil;
	return items[static_cast<NSUInteger>(info.indexInApplication)];
}

id attachedMenu(AXUIElementRef element) {
	NSArray* children = copyAttribute(element, kAXChildrenAttribute);
	for (id child in children) {
		NSString* role = copyAttribute((__bridge AXUIElementRef)child, kAXRoleAttribute);
		if ([role isEqualToString:(__bridge NSString*)kAXMenuRole]) return child;
	}
	return nil;
}

id pressAndWaitForMenu(AXUIElementRef element) {
	AXUIElementSetMessagingTimeout(element, MENU_ACTION_TIMEOUT_SECONDS);
	AXUIElementPerformAction(element, kAXPressAction);
	for (int attempt = 0; attempt < MENU_OPEN_ATTEMPTS; ++attempt) {
		if (id menu = attachedMenu(element)) return menu;
		usleep(MENU_OPEN_WAIT_MICROSECONDS);
	}
	return nil;
}

NSArray* menuItemsOf(id menu) {
	NSMutableArray* menuItems = [NSMutableArray array];
	NSArray* children = copyAttribute((__bridge AXUIElementRef)menu, kAXChildrenAttribute);
	for (id child in children) {
		NSString* role = copyAttribute((__bridge AXUIElementRef)child, kAXRoleAttribute);
		if ([role isEqualToString:(__bridge NSString*)kAXMenuItemRole]) [menuItems addObject:child];
	}
	return menuItems;
}

QList<MenuExtraEntryInfo> readMenuEntries(id menu, int depth) {
	QList<MenuExtraEntryInfo> entries;
	for (id itemObject in menuItemsOf(menu)) {
		auto item = (__bridge AXUIElementRef)itemObject;
		MenuExtraEntryInfo entry;
		entry.text = stringAttribute(item, kAXTitleAttribute);
		entry.isSeparator = entry.text.isEmpty();
		NSNumber* enabled = copyAttribute(item, kAXEnabledAttribute);
		entry.enabled = enabled == nil || enabled.boolValue;
		entry.isChecked = !stringAttribute(item, kAXMenuItemMarkCharAttribute).isEmpty();
		if (depth < MAXIMUM_SUBMENU_DEPTH) {
			if (id submenu = attachedMenu(item)) entry.children = readMenuEntries(submenu, depth + 1);
		}
		entries.append(entry);
	}
	return entries;
}

void closeMenu(id menu) { AXUIElementPerformAction((__bridge AXUIElementRef)menu, kAXCancelAction); }

void pressMenuEntry(id rootMenu, const QList<qint32>& path) {
	id currentMenu = rootMenu;
	for (qsizetype level = 0; level < path.size(); ++level) {
		NSArray* items = menuItemsOf(currentMenu);
		if (path.at(level) >= static_cast<qint32>(items.count)) break;
		auto item = (__bridge AXUIElementRef)items[static_cast<NSUInteger>(path.at(level))];

		if (level == path.size() - 1) {
			AXUIElementPerformAction(item, kAXPressAction);
			return;
		}

		id submenu = attachedMenu(item) ?: pressAndWaitForMenu(item);
		if (!submenu) break;
		currentMenu = submenu;
	}
	closeMenu(rootMenu);
}

}

MenuExtraMenuEntry::MenuExtraMenuEntry(StatusNotifierItem* item, QList<qint32> path, QObject* parent)
    : QsMenuEntry(parent)
    , mItem(item)
    , mPath(std::move(path)) {
	if (this->mPath.isEmpty()) {
		QObject::connect(this, &QsMenuEntry::opened, item, &StatusNotifierItem::loadMenu);
	} else {
		QObject::connect(this, &QsMenuEntry::triggered, item, [item, path = this->mPath] { item->triggerMenuEntry(path); });
	}
}

qs::menu::QsMenuButtonType::Enum MenuExtraMenuEntry::buttonType() const {
	return this->mInfo.isChecked ? qs::menu::QsMenuButtonType::CheckBox : qs::menu::QsMenuButtonType::None;
}

Qt::CheckState MenuExtraMenuEntry::checkState() const { return this->mInfo.isChecked ? Qt::Checked : Qt::Unchecked; }

void MenuExtraMenuEntry::applyEntryInfo(const MenuExtraEntryInfo& info) {
	this->mInfo = info;
	emit this->isSeparatorChanged();
	emit this->enabledChanged();
	emit this->textChanged();
	emit this->buttonTypeChanged();
	emit this->checkStateChanged();
	emit this->hasChildrenChanged();

	const auto previousChildren = this->mChildren.valueList();
	for (auto* child: previousChildren) {
		this->mChildren.removeObject(child);
		child->deleteLater();
	}

	for (qint32 index = 0; index < static_cast<qint32>(info.children.size()); ++index) {
		auto* child = new MenuExtraMenuEntry(this->mItem, this->mPath + QList<qint32> {index}, this);
		child->applyEntryInfo(info.children.at(index));
		this->mChildren.insertObject(child);
	}
}

StatusNotifierItem::StatusNotifierItem(QObject* parent)
    : QObject(parent)
    , mMenu(new MenuExtraMenuEntry(this, {}, this)) {}

void StatusNotifierItem::loadMenu() {
	if (!AXIsProcessTrusted()) return;
	auto info = this->mInfo;
	QPointer<StatusNotifierItem> guardedItem = this;

	dispatch_async(menuQueue(), ^{
		MenuExtraEntryInfo rootEntry;
		@autoreleasepool {
			id extra = menuExtraElement(info);
			if (extra) {
				if (id menu = pressAndWaitForMenu((__bridge AXUIElementRef)extra)) {
					rootEntry.children = readMenuEntries(menu, 0);
					closeMenu(menu);
				}
			}
		}
		QMetaObject::invokeMethod(SystemTrayHost::instance(), [guardedItem, rootEntry] {
			if (guardedItem) guardedItem->mMenu->applyEntryInfo(rootEntry);
		}, Qt::QueuedConnection);
	});
}

void StatusNotifierItem::triggerMenuEntry(const QList<qint32>& path) {
	auto info = this->mInfo;
	QList<qint32> entryPath = path;
	dispatch_async(menuQueue(), ^{
		@autoreleasepool {
			id extra = menuExtraElement(info);
			if (!extra) return;
			if (id menu = pressAndWaitForMenu((__bridge AXUIElementRef)extra)) pressMenuEntry(menu, entryPath);
		}
	});
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
	this->mReader.request(this, readMenuExtras, [this](const QList<MenuExtraInfo>& extras) {
		this->applyMenuExtras(extras);
	});
}

void SystemTrayHost::applyMenuExtras(const QList<MenuExtraInfo>& extras) {
	QSet<QString> presentIdentifiers;

	for (const auto& info: extras) {
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

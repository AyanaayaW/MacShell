#include "applications.hpp"

#include <utility>

#include <qfileinfo.h>
#include <qfilesystemwatcher.h>
#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtimer.h>

#include "../core/desktopentry.hpp"

#import <AppKit/AppKit.h>

namespace qs::macos {

namespace {

constexpr int MAXIMUM_FOLDER_DEPTH = 2;
constexpr int RESCAN_DELAY_MILLISECONDS = 1000;

QStringList applicationDirectories() {
	return {
	    QStringLiteral("/Applications"),
	    QStringLiteral("/System/Applications"),
	    QString::fromNSString([NSHomeDirectory() stringByAppendingPathComponent:@"Applications"]),
	};
}

QStringList additionalApplicationBundles() {
	return {QStringLiteral("/System/Library/CoreServices/Finder.app")};
}

const QHash<QString, QStringList>& freedesktopCategoriesByAppleCategory() {
	static const QHash<QString, QStringList> categories {
	    {QStringLiteral("public.app-category.developer-tools"), {QStringLiteral("Development")}},
	    {QStringLiteral("public.app-category.games"), {QStringLiteral("Game")}},
	    {QStringLiteral("public.app-category.graphics-design"), {QStringLiteral("Graphics")}},
	    {QStringLiteral("public.app-category.photography"), {QStringLiteral("Graphics"), QStringLiteral("Photography")}},
	    {QStringLiteral("public.app-category.video"), {QStringLiteral("AudioVideo"), QStringLiteral("Video")}},
	    {QStringLiteral("public.app-category.music"), {QStringLiteral("AudioVideo"), QStringLiteral("Audio")}},
	    {QStringLiteral("public.app-category.entertainment"), {QStringLiteral("AudioVideo")}},
	    {QStringLiteral("public.app-category.social-networking"), {QStringLiteral("Network"), QStringLiteral("Chat")}},
	    {QStringLiteral("public.app-category.productivity"), {QStringLiteral("Office")}},
	    {QStringLiteral("public.app-category.business"), {QStringLiteral("Office")}},
	    {QStringLiteral("public.app-category.finance"), {QStringLiteral("Office"), QStringLiteral("Finance")}},
	    {QStringLiteral("public.app-category.education"), {QStringLiteral("Education")}},
	    {QStringLiteral("public.app-category.reference"), {QStringLiteral("Education")}},
	    {QStringLiteral("public.app-category.utilities"), {QStringLiteral("Utility")}},
	    {QStringLiteral("public.app-category.news"), {QStringLiteral("Network"), QStringLiteral("News")}},
	    {QStringLiteral("public.app-category.weather"), {QStringLiteral("Utility")}},
	    {QStringLiteral("public.app-category.travel"), {QStringLiteral("Utility")}},
	    {QStringLiteral("public.app-category.lifestyle"), {QStringLiteral("Utility")}},
	};
	return categories;
}

bool isBackgroundOnly(NSDictionary* information) {
	return [information[@"LSUIElement"] boolValue] || [information[@"LSBackgroundOnly"] boolValue];
}

ParsedDesktopEntryData entryForBundle(NSString* bundlePath) {
	NSBundle* bundle = [NSBundle bundleWithPath:bundlePath];
	NSDictionary* information = bundle.infoDictionary;

	auto bundleIdentifier = QString::fromNSString(information[@"CFBundleIdentifier"] ?: @"");
	auto path = QString::fromNSString(bundlePath);
	auto displayName = QString::fromNSString([NSFileManager.defaultManager displayNameAtPath:bundlePath]);
	if (displayName.endsWith(QStringLiteral(".app"))) displayName.chop(4);

	ParsedDesktopEntryData entry;
	entry.id = bundleIdentifier.isEmpty() ? displayName : bundleIdentifier;
	entry.name = displayName;
	entry.startupClass = bundleIdentifier;
	entry.icon = bundleIdentifier.isEmpty() ? path : bundleIdentifier;
	entry.command = {QStringLiteral("/usr/bin/open"), path};
	entry.execString = QStringLiteral("/usr/bin/open \"%1\"").arg(path);
	entry.noDisplay = isBackgroundOnly(information);
	entry.keywords = {displayName, QString::fromNSString(information[@"CFBundleName"] ?: @"")};

	auto appleCategory = QString::fromNSString(information[@"LSApplicationCategoryType"] ?: @"");
	entry.categories = freedesktopCategoriesByAppleCategory().value(appleCategory);
	entry.genericName = entry.categories.isEmpty() ? QString() : entry.categories.first();

	return entry;
}

void collectBundles(NSString* directory, int depth, QList<ParsedDesktopEntryData>& entries, QSet<QString>& seenIdentifiers) {
	NSArray<NSString*>* children = [NSFileManager.defaultManager contentsOfDirectoryAtPath:directory error:nil];

	for (NSString* child in children) {
		if ([child hasPrefix:@"."]) continue;
		NSString* childPath = [directory stringByAppendingPathComponent:child];

		if ([child.pathExtension isEqualToString:@"app"]) {
			auto entry = entryForBundle(childPath);
			if (entry.name.isEmpty() || seenIdentifiers.contains(entry.id)) continue;
			seenIdentifiers.insert(entry.id);
			entries.append(std::move(entry));
			continue;
		}

		BOOL isDirectory = NO;
		if (depth < MAXIMUM_FOLDER_DEPTH
		    && [NSFileManager.defaultManager fileExistsAtPath:childPath isDirectory:&isDirectory]
		    && isDirectory)
		{
			collectBundles(childPath, depth + 1, entries, seenIdentifiers);
		}
	}
}

}

QList<ParsedDesktopEntryData> scanInstalledApplications() {
	QList<ParsedDesktopEntryData> entries;
	QSet<QString> seenIdentifiers;

	@autoreleasepool {
		for (const auto& directory: applicationDirectories()) {
			collectBundles(directory.toNSString(), 0, entries, seenIdentifiers);
		}

		for (const auto& bundlePath: additionalApplicationBundles()) {
			auto entry = entryForBundle(bundlePath.toNSString());
			if (!entry.name.isEmpty() && !seenIdentifiers.contains(entry.id)) entries.append(std::move(entry));
		}
	}

	return entries;
}

void watchApplicationDirectories() {
	auto* watcher = new QFileSystemWatcher(DesktopEntryManager::instance());
	auto* rescanTimer = new QTimer(watcher);
	rescanTimer->setSingleShot(true);
	rescanTimer->setInterval(RESCAN_DELAY_MILLISECONDS);

	for (const auto& directory: applicationDirectories()) {
		if (QFileInfo::exists(directory)) watcher->addPath(directory);
	}

	QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, rescanTimer, qOverload<>(&QTimer::start));
	QObject::connect(rescanTimer, &QTimer::timeout, DesktopEntryManager::instance(), &DesktopEntryManager::handleFileChanges);
}

}

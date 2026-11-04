#include "keyboard_layouts.hpp"

#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qregularexpression.h>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

namespace qs::hyprland::macos {

namespace {

QString sanitizedLayoutName(NSString* localizedName) {
	static const QRegularExpression unsupportedCharacters(QStringLiteral("[^A-Za-z() ]"));
	auto name = QString::fromNSString(localizedName).remove(unsupportedCharacters).simplified();
	return name.isEmpty() ? QStringLiteral("Unknown") : name;
}

QString layoutCodeFor(NSString* sourceId) {
	auto identifier = QString::fromNSString(sourceId);
	auto code = identifier.section(u'.', -1).toLower();
	static const QRegularExpression unsupportedCharacters(QStringLiteral("[^a-z]"));
	code.remove(unsupportedCharacters);
	return code.size() >= 2 ? code : QStringLiteral("us");
}

KeyboardLayout describeSource(TISInputSourceRef source) {
	auto* sourceId = (__bridge NSString*)TISGetInputSourceProperty(source, kTISPropertyInputSourceID);
	auto* localizedName = (__bridge NSString*)TISGetInputSourceProperty(source, kTISPropertyLocalizedName);
	return {
	    .sourceId = QString::fromNSString(sourceId),
	    .code = layoutCodeFor(sourceId),
	    .name = sanitizedLayoutName(localizedName),
	};
}

NSArray* selectableKeyboardSources() {
	NSDictionary* filter = @{
		(__bridge NSString*)kTISPropertyInputSourceCategory: (__bridge NSString*)kTISCategoryKeyboardInputSource,
		(__bridge NSString*)kTISPropertyInputSourceIsSelectCapable: @YES,
	};
	return CFBridgingRelease(TISCreateInputSourceList((__bridge CFDictionaryRef)filter, false));
}

}

QList<KeyboardLayout> enabledKeyboardLayouts() {
	QList<KeyboardLayout> layouts;
	for (id source in selectableKeyboardSources()) {
		layouts.append(describeSource((__bridge TISInputSourceRef)source));
	}
	return layouts;
}

std::optional<KeyboardLayout> currentKeyboardLayout() {
	TISInputSourceRef source = TISCopyCurrentKeyboardInputSource();
	if (!source) return std::nullopt;
	auto layout = describeSource(source);
	CFRelease(source);
	return layout;
}

bool selectKeyboardLayout(const QString& selector) {
	NSArray* sources = selectableKeyboardSources();
	if (sources.count == 0) return false;

	auto current = currentKeyboardLayout();
	NSUInteger currentIndex = 0;
	for (NSUInteger index = 0; index < sources.count; ++index) {
		if (current && describeSource((__bridge TISInputSourceRef)sources[index]).sourceId == current->sourceId) {
			currentIndex = index;
		}
	}

	NSUInteger targetIndex = currentIndex;
	if (selector == QStringLiteral("next")) {
		targetIndex = (currentIndex + 1) % sources.count;
	} else if (selector == QStringLiteral("prev")) {
		targetIndex = (currentIndex + sources.count - 1) % sources.count;
	} else {
		bool isNumber = false;
		auto requestedIndex = selector.toUInt(&isNumber);
		if (!isNumber || requestedIndex >= sources.count) return false;
		targetIndex = requestedIndex;
	}

	return TISSelectInputSource((__bridge TISInputSourceRef)sources[targetIndex]) == noErr;
}

bool capsLockEnabled() {
	return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskAlphaShift) != 0;
}

void watchKeyboardLayout(std::function<void()> onChange) {
	[NSDistributedNotificationCenter.defaultCenter
	    addObserverForName:(__bridge NSString*)kTISNotifySelectedKeyboardInputSourceChanged
	                object:nil
	                 queue:nil
	            usingBlock:^(NSNotification*) {
		            QMetaObject::invokeMethod(QCoreApplication::instance(), onChange, Qt::QueuedConnection);
	            }];
}

QByteArray xkbRulesListing() {
	QByteArray listing = "! model\n  pc105           Generic 105-key PC\n\n! layout\n";
	for (const auto& layout: enabledKeyboardLayouts()) {
		listing += "  " + layout.code.toUtf8().leftJustified(15, ' ') + ' ' + layout.name.toUtf8() + '\n';
	}
	return listing + "\n! variant\n\n! option\n\n";
}

}

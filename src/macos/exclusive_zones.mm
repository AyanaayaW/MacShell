#include "exclusive_zones.hpp"
#include <algorithm>

#include <qlist.h>
#include <qmargins.h>
#include <qobject.h>
#include <qrect.h>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace qs::macos {

namespace {

constexpr int ENFORCE_INTERVAL_MILLISECONDS = 1000;
constexpr float ACCESSIBILITY_TIMEOUT_SECONDS = 0.2F;
constexpr int ADJUSTMENT_TOLERANCE = 1;

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

void applyWindowFrame(AXUIElementRef window, const QRect& frame) {
	CGPoint position = CGPointMake(frame.x(), frame.y());
	CGSize size = CGSizeMake(frame.width(), frame.height());
	AXValueRef positionValue = AXValueCreate(kAXValueTypeCGPoint, &position);
	AXValueRef sizeValue = AXValueCreate(kAXValueTypeCGSize, &size);
	AXUIElementSetAttributeValue(window, kAXPositionAttribute, positionValue);
	AXUIElementSetAttributeValue(window, kAXSizeAttribute, sizeValue);
	CFRelease(positionValue);
	CFRelease(sizeValue);
}

QRect fitWithin(const QRect& frame, const QRect& usable) {
	auto width = std::min(frame.width(), usable.width());
	auto height = std::min(frame.height(), usable.height());
	auto left = std::clamp(frame.left(), usable.left(), usable.right() - width + 1);
	auto top = std::clamp(frame.top(), usable.top(), usable.bottom() - height + 1);
	return QRect(left, top, width, height);
}

bool differsNoticeably(const QRect& first, const QRect& second) {
	return std::abs(first.x() - second.x()) > ADJUSTMENT_TOLERANCE
	    || std::abs(first.y() - second.y()) > ADJUSTMENT_TOLERANCE
	    || std::abs(first.width() - second.width()) > ADJUSTMENT_TOLERANCE
	    || std::abs(first.height() - second.height()) > ADJUSTMENT_TOLERANCE;
}

}

ExclusiveZoneEnforcer* ExclusiveZoneEnforcer::instance() {
	static auto* sharedInstance = new ExclusiveZoneEnforcer();
	return sharedInstance;
}

ExclusiveZoneEnforcer::ExclusiveZoneEnforcer() {
	QObject::connect(&this->mEnforceTimer, &QTimer::timeout, this, &ExclusiveZoneEnforcer::enforce);
}

void ExclusiveZoneEnforcer::setReservations(const QList<ScreenReservation>& reservations) {
	this->mReservations.clear();
	for (const auto& reservation: reservations) {
		if (!reservation.reservedInsets.isNull()) this->mReservations.append(reservation);
	}

	if (this->mReservations.isEmpty()) {
		this->mEnforceTimer.stop();
		return;
	}

	if (!AXIsProcessTrusted() && !this->mRequestedAccessibility) {
		this->mRequestedAccessibility = true;
		NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
		AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
	}

	if (!this->mEnforceTimer.isActive()) this->mEnforceTimer.start(ENFORCE_INTERVAL_MILLISECONDS);
	this->enforce();
}

QRect ExclusiveZoneEnforcer::usableGeometry(const QRect& screenGeometry) const {
	for (const auto& reservation: this->mReservations) {
		if (reservation.screenGeometry == screenGeometry) {
			return screenGeometry.marginsRemoved(reservation.reservedInsets);
		}
	}
	return screenGeometry;
}

void ExclusiveZoneEnforcer::enforce() {
	if (this->mReservations.isEmpty() || !AXIsProcessTrusted()) return;

	auto ownProcessId = NSProcessInfo.processInfo.processIdentifier;

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

				for (const auto& reservation: this->mReservations) {
					if (!reservation.screenGeometry.contains(frame.center())) continue;

					auto usable = reservation.screenGeometry.marginsRemoved(reservation.reservedInsets);
					auto fitted = fitWithin(frame, usable);
					if (differsNoticeably(frame, fitted)) applyWindowFrame(window, fitted);
					break;
				}
			}

			CFRelease(applicationElement);
		}
	}
}

}

#include "power_profiles.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qstring.h>

#import <Foundation/Foundation.h>

namespace qs::service::upower {

namespace {

Q_LOGGING_CATEGORY(logPowerProfiles, "quickshell.service.powerprofiles", QtWarningMsg);

bool isLowPowerModeEnabled() { return NSProcessInfo.processInfo.lowPowerModeEnabled; }

bool setLowPowerModeEnabled(bool enabled) {
	NSString* source = [NSString
	    stringWithFormat:@"do shell script \"/usr/bin/pmset -a lowpowermode %d\" with administrator privileges",
	                     enabled ? 1 : 0];
	NSAppleScript* script = [[NSAppleScript alloc] initWithSource:source];

	NSDictionary* errorInfo = nil;
	[script executeAndReturnError:&errorInfo];
	return errorInfo == nil;
}

}

QString PowerProfile::toString(PowerProfile::Enum profile) {
	switch (profile) {
	case PowerProfile::PowerSaver: return QStringLiteral("PowerSaver");
	case PowerProfile::Balanced: return QStringLiteral("Balanced");
	case PowerProfile::Performance: return QStringLiteral("Performance");
	default: return QStringLiteral("Invalid");
	}
}

QString PerformanceDegradationReason::toString(PerformanceDegradationReason::Enum reason) {
	switch (reason) {
	case PerformanceDegradationReason::None: return QStringLiteral("None");
	case PerformanceDegradationReason::LapDetected: return QStringLiteral("LapDetected");
	case PerformanceDegradationReason::HighTemperature: return QStringLiteral("HighTemperature");
	default: return QStringLiteral("Invalid");
	}
}

PowerProfiles* PowerProfiles::instance() {
	static auto* sharedInstance = new PowerProfiles();
	return sharedInstance;
}

PowerProfiles::PowerProfiles() {
	[[NSNotificationCenter defaultCenter]
	    addObserverForName:NSProcessInfoPowerStateDidChangeNotification
	                object:nil
	                 queue:[NSOperationQueue mainQueue]
	            usingBlock:^(NSNotification*) {
		              QMetaObject::invokeMethod(this, [this]() { this->refreshFromSystem(); });
	            }];

	this->refreshFromSystem();
}

void PowerProfiles::refreshFromSystem() {
	this->bProfile = isLowPowerModeEnabled() ? PowerProfile::PowerSaver : PowerProfile::Balanced;
}

void PowerProfiles::setProfile(PowerProfile::Enum profile) {
	if (profile == PowerProfile::Performance) {
		qCWarning(logPowerProfiles) << "The Performance profile is not available on this Mac.";
		return;
	}

	auto wantsLowPowerMode = profile == PowerProfile::PowerSaver;
	if (wantsLowPowerMode == isLowPowerModeEnabled()) return;

	if (!setLowPowerModeEnabled(wantsLowPowerMode)) {
		qCWarning(logPowerProfiles) << "Changing Low Power Mode was cancelled or failed.";
	}

	this->refreshFromSystem();
}

PowerProfilesQml::PowerProfilesQml(QObject* parent): QObject(parent) {
	QObject::connect(
	    PowerProfiles::instance(),
	    &PowerProfiles::profileChanged,
	    this,
	    &PowerProfilesQml::profileChanged
	);
}

}

#include "power_profiles.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qprocess.h>
#include <qstring.h>

#import <Foundation/Foundation.h>

namespace qs::service::upower {

namespace {

Q_LOGGING_CATEGORY(logPowerProfiles, "quickshell.service.powerprofiles", QtWarningMsg);

bool isLowPowerModeEnabled() { return NSProcessInfo.processInfo.lowPowerModeEnabled; }

void requestLowPowerMode(bool enabled) {
	auto script = QStringLiteral(
	                  "do shell script \"/usr/bin/pmset -a lowpowermode %1\" with administrator privileges "
	                  "with prompt \"MacShell wants to change Low Power Mode.\""
	)
	                  .arg(enabled ? 1 : 0);
	auto* process = new QProcess();
	QObject::connect(process, &QProcess::finished, process, [process](int exitCode) {
		if (exitCode != 0) qCWarning(logPowerProfiles) << "Changing Low Power Mode was cancelled or failed.";
		PowerProfiles::instance()->refreshFromSystem();
		process->deleteLater();
	});
	process->start(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), script});
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
	                 queue:nil
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
		qCInfo(logPowerProfiles) << "This Mac has no Performance profile; using Balanced.";
	}

	auto wantsLowPowerMode = profile == PowerProfile::PowerSaver;
	if (wantsLowPowerMode == isLowPowerModeEnabled()) {
		this->refreshFromSystem();
		return;
	}

	requestLowPowerMode(wantsLowPowerMode);
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

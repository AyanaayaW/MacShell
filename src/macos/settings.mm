#include "settings.hpp"

#include <qdir.h>
#include <qfile.h>
#include <qlogging.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>
#include <qtextstream.h>

#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace qs::macos {

namespace {

bool parseBoolean(const QString& value) {
	auto normalized = value.trimmed().toLower();
	return normalized == QStringLiteral("true") || normalized == QStringLiteral("yes")
	    || normalized == QStringLiteral("on") || normalized == QStringLiteral("1");
}

}

QString settingsFilePath() {
	auto configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
	if (configHome.isEmpty()) configHome = QDir::home().filePath(QStringLiteral(".config"));
	return QDir(configHome).filePath(QStringLiteral("macshell/settings.conf"));
}

MacShellSettings readSettings() {
	MacShellSettings settings;

	QFile file(settingsFilePath());
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream stream(&file);
		while (!stream.atEnd()) {
			auto line = stream.readLine().trimmed();
			auto commentStart = line.indexOf(u'#');
			if (commentStart != -1) line = line.first(commentStart).trimmed();

			auto separator = line.indexOf(u'=');
			if (separator == -1) continue;

			auto key = line.first(separator).trimmed();
			auto value = line.sliced(separator + 1);

			if (key == QStringLiteral("hide_system_ui")) settings.hideSystemUi = parseBoolean(value);
			else if (key == QStringLiteral("launch_at_login")) settings.launchAtLogin = parseBoolean(value);
			else if (key == QStringLiteral("super_key")) settings.superKey = value.trimmed().toLower();
			else if (key == QStringLiteral("hyprland_autostart")) settings.hyprlandAutostart = parseBoolean(value);
		}
	}

	if (qEnvironmentVariable("MACSHELL_HIDE_SYSTEM_UI") == QStringLiteral("1")) settings.hideSystemUi = true;
	return settings;
}

quint32 carbonModifierNamed(const QString& name) {
	if (name == QStringLiteral("command") || name == QStringLiteral("cmd")) return cmdKey;
	if (name == QStringLiteral("control") || name == QStringLiteral("ctrl")) return controlKey;
	return optionKey;
}

void applyLaunchAtLogin(bool enabled) {
	if (![NSBundle.mainBundle.bundlePath hasSuffix:@".app"]) return;

	if (@available(macOS 13.0, *)) {
		SMAppService* service = SMAppService.mainAppService;
		auto isRegistered = service.status == SMAppServiceStatusEnabled;
		if (enabled == isRegistered) return;

		NSError* error = nil;
		auto succeeded = enabled ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
		if (!succeeded) {
			qWarning() << "Could not update launch at login:" << QString::fromNSString(error.localizedDescription);
		}
	}
}

}

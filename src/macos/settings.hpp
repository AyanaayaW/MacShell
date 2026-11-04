#pragma once

#include <qstring.h>
#include <qtypes.h>

namespace qs::macos {

struct MacShellSettings {
	QString superKey = QStringLiteral("option");
	bool hideSystemUi = false;
	bool launchAtLogin = false;
};

QString settingsFilePath();
MacShellSettings readSettings();
void applyLaunchAtLogin(bool enabled);
quint32 carbonModifierNamed(const QString& name);

}

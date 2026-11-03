#pragma once

#include <qstring.h>

namespace qs::macos {

struct MacShellSettings {
	bool hideSystemUi = false;
	bool launchAtLogin = false;
};

QString settingsFilePath();
MacShellSettings readSettings();
void applyLaunchAtLogin(bool enabled);

}

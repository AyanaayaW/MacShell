#pragma once

#include <qstring.h>
#include <qstringlist.h>

namespace qs::macos::compat {

struct LinuxInstallRequest {
	QStringList packages;
	QStringList repositories;
	QStringList skippedCommands;
	QStringList errors;
	bool skipConfirmation = false;
};

LinuxInstallRequest parseLinuxInstallCommand(const QStringList& words);
QStringList fontCaskCandidates(const QString& linuxPackage);
QString homebrewNameFor(const QString& linuxPackage);
QString quickshellConfigNameFor(const QString& linuxPackage);
bool isCoveredByMacShell(const QString& linuxPackage);

}

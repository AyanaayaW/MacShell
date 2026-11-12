#pragma once

#include <qstring.h>
#include <qstringlist.h>

namespace qs::macos::compat {

struct LinuxInstallRequest {
	QStringList packages;
	QStringList repositories;
	QStringList installerScripts;
	QStringList skippedCommands;
	QStringList errors;
	bool skipConfirmation = false;
};

LinuxInstallRequest parseLinuxInstallCommand(const QStringList& words);
QStringList fontCaskCandidates(const QString& linuxPackage);
QString homebrewNameFor(const QString& linuxPackage);
QString quickshellConfigNameFor(const QString& linuxPackage);
bool isCoveredByMacShell(const QString& linuxPackage);
QString githubRepositoryFromWord(const QString& word);
QStringList githubRepositoriesIn(const QString& text);
QString configNameFromInstallerUrl(const QString& url);
QStringList importedModuleUris(const QString& qmlSource);
bool isBuiltinModuleUri(const QString& uri);
qsizetype matchingModuleDepth(const QStringList& directoryComponents, const QStringList& uriComponents);
QString goBinaryName(const QString& goModText);
QStringList cargoBinaryNames(const QString& cargoTomlText);
bool mentionsProgram(const QString& text, const QString& programName);
QString startCommandFor(const QString& programName, const QString& fileText);
QStringList linuxOnlyFeaturesIn(const QString& buildOutput);
QStringList shellShortcutBinds(const QString& hyprlandConfigText);

}

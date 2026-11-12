#include <algorithm>
#include <array>
#include <cstdio>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qdiriterator.h>
#include <qfileinfo.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qprocess.h>
#include <qstandardpaths.h>
#include <qtemporarydir.h>
#include <qurl.h>
#include <qurlquery.h>

#include "commands.hpp"
#include "linux_install_parser.hpp"

namespace qs::macos::compat {

namespace {

enum class InstallAction : quint8 {
	CoveredByMacShell,
	HomebrewFormula,
	HomebrewCask,
	SourceRepository,
	Unavailable,
};

struct ResolvedPackage {
	QString linuxName;
	InstallAction action = InstallAction::Unavailable;
	QString target;
	QString configName;
};

void printLine(const QString& line) {
	std::puts(line.toUtf8().constData());
	std::fflush(stdout);
}

void printError(const QString& line) {
	std::fputs((line + '\n').toUtf8().constData(), stderr);
}

QString findHomebrew() {
	QString onPath = QStandardPaths::findExecutable("brew");
	if (!onPath.isEmpty()) return onPath;
	for (const auto* candidate: {"/opt/homebrew/bin/brew", "/usr/local/bin/brew"}) {
		if (QFileInfo(candidate).isExecutable()) return candidate;
	}
	return {};
}

bool runQuietly(const QString& program, const QStringList& arguments, QByteArray* output = nullptr) {
	QProcess process;
	process.setProcessChannelMode(QProcess::SeparateChannels);
	process.start(program, arguments);
	if (!process.waitForFinished(60000)) return false;
	if (output) *output = process.readAllStandardOutput();
	return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool runVisibly(const QString& program, const QStringList& arguments) {
	QProcess process;
	process.setProcessChannelMode(QProcess::ForwardedChannels);
	process.setInputChannelMode(QProcess::ForwardedInputChannel);
	process.start(program, arguments);
	if (!process.waitForFinished(-1)) return false;
	return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool homebrewHasCask(const QString& homebrew, const QString& name) {
	if (homebrew.isEmpty()) return false;
	return runQuietly(homebrew, {"info", "--json=v2", "--cask", name});
}

bool homebrewHasMacFormula(const QString& homebrew, const QString& name) {
	if (homebrew.isEmpty()) return false;
	QByteArray response;
	if (!runQuietly(homebrew, {"info", "--json=v2", "--formula", name}, &response)) return false;
	auto formulae = QJsonDocument::fromJson(response).object().value("formulae").toArray();
	if (formulae.isEmpty()) return false;
	auto bottleFiles = formulae.first().toObject().value("bottle").toObject().value("stable").toObject().value("files").toObject();
	for (const auto& platform: bottleFiles.keys()) {
		if (!platform.contains("linux")) return true;
	}
	return false;
}

QString aurUpstreamUrl(const QString& package) {
	QUrl requestUrl("https://aur.archlinux.org/rpc/v5/info");
	QUrlQuery query;
	query.addQueryItem("arg[]", package);
	requestUrl.setQuery(query);

	QByteArray response;
	if (!runQuietly("/usr/bin/curl", {"-fsSL", "--max-time", "15", requestUrl.toString(QUrl::FullyEncoded)}, &response)) {
		return {};
	}
	auto results = QJsonDocument::fromJson(response).object().value("results").toArray();
	if (results.isEmpty()) return {};
	return results.first().toObject().value("URL").toString();
}

bool isGitForgeUrl(const QString& url) {
	static const QStringList forgeHosts {"github.com", "gitlab.com", "codeberg.org", "git.sr.ht", "bitbucket.org"};
	return forgeHosts.contains(QUrl(url).host().toLower());
}

ResolvedPackage resolvePackage(const QString& linuxName, const QString& homebrew) {
	ResolvedPackage resolved {.linuxName = linuxName};

	if (isCoveredByMacShell(linuxName)) {
		resolved.action = InstallAction::CoveredByMacShell;
		return resolved;
	}

	for (const auto& cask: fontCaskCandidates(linuxName)) {
		if (homebrewHasCask(homebrew, cask)) {
			resolved.action = InstallAction::HomebrewCask;
			resolved.target = cask;
			return resolved;
		}
	}

	QString homebrewName = homebrewNameFor(linuxName);
	if (homebrewHasMacFormula(homebrew, homebrewName)) {
		resolved.action = InstallAction::HomebrewFormula;
		resolved.target = homebrewName;
		return resolved;
	}
	if (homebrewHasCask(homebrew, homebrewName)) {
		resolved.action = InstallAction::HomebrewCask;
		resolved.target = homebrewName;
		return resolved;
	}

	QString upstream = aurUpstreamUrl(linuxName);
	if (isGitForgeUrl(upstream)) {
		resolved.action = InstallAction::SourceRepository;
		resolved.target = upstream;
		resolved.configName = quickshellConfigNameFor(linuxName);
	}
	return resolved;
}

QString configNameFromRepositoryUrl(const QString& url) {
	static const QStringList genericRepositoryNames {"shell", "dots", "dotfiles", "quickshell", "config", "configs", "rice"};
	QStringList pathParts = QUrl(url).path().split('/', Qt::SkipEmptyParts);
	if (pathParts.isEmpty()) return "imported";
	QString repositoryName = pathParts.last();
	if (repositoryName.endsWith(".git")) repositoryName.chop(4);
	if (genericRepositoryNames.contains(repositoryName.toLower()) && pathParts.size() > 1) {
		return quickshellConfigNameFor(pathParts.at(pathParts.size() - 2).toLower());
	}
	return quickshellConfigNameFor(repositoryName.toLower());
}

QString findShellEntryDirectory(const QString& repositoryPath) {
	QString bestDirectory;
	qsizetype bestDepth = -1;
	QDirIterator iterator(repositoryPath, {"shell.qml"}, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString relativePath = QDir(repositoryPath).relativeFilePath(iterator.next());
		if (relativePath.startsWith(".git/")) continue;
		qsizetype depth = relativePath.count('/');
		if (bestDepth < 0 || depth < bestDepth) {
			bestDepth = depth;
			bestDirectory = QFileInfo(iterator.filePath()).absolutePath();
		}
	}
	return bestDirectory;
}

bool containsCompiledPlugin(const QString& repositoryPath) {
	if (QFileInfo::exists(repositoryPath + "/CMakeLists.txt")) return true;
	QDirIterator iterator(repositoryPath, {"*.cpp", "*.cc", "*.rs"}, QDir::Files, QDirIterator::Subdirectories);
	return iterator.hasNext();
}

bool installQuickshellConfig(const QString& repositoryUrl, QString configName) {
	QTemporaryDir workspace;
	if (!workspace.isValid()) {
		printError(":: could not create a temporary folder");
		return false;
	}
	QString checkoutPath = workspace.filePath("repository");

	printLine(":: Downloading " + repositoryUrl);
	if (!runVisibly("/usr/bin/git", {"clone", "--depth", "1", repositoryUrl, checkoutPath})) {
		printError(":: git clone failed for " + repositoryUrl);
		return false;
	}

	QString entryDirectory = findShellEntryDirectory(checkoutPath);
	if (entryDirectory.isEmpty()) {
		printError(":: " + repositoryUrl + " has no shell.qml, so it is not a Quickshell config and has no macOS install");
		return false;
	}

	static const QStringList genericFolderNames {"quickshell", "shell", "qs", "config", ".config", "repository"};
	QString entryFolderName = QFileInfo(entryDirectory).fileName();
	if (entryDirectory != checkoutPath && !genericFolderNames.contains(entryFolderName.toLower())) {
		configName = entryFolderName;
	}
	if (configName.isEmpty()) configName = configNameFromRepositoryUrl(repositoryUrl);

	QString configsRoot = QDir::homePath() + "/.config/quickshell";
	QString destination = configsRoot + '/' + configName;
	if (QFileInfo::exists(destination)) {
		printLine(":: " + destination + " already exists, leaving it untouched");
		return true;
	}
	QDir().mkpath(configsRoot);
	if (!runQuietly("/usr/bin/ditto", {entryDirectory, destination})) {
		printError(":: could not copy the config to " + destination);
		return false;
	}

	printLine(":: Installed Quickshell config '" + configName + "' to " + destination);
	if (containsCompiledPlugin(checkoutPath)) {
		printLine("   It ships a compiled plugin, which needs a macOS port before every part works.");
	}
	printLine("   Start it with: qs -c " + configName);
	return true;
}

QString describe(const ResolvedPackage& package) {
	switch (package.action) {
	case InstallAction::CoveredByMacShell: return "already built into MacShell";
	case InstallAction::HomebrewFormula: return "brew install " + package.target;
	case InstallAction::HomebrewCask: return "brew install --cask " + package.target;
	case InstallAction::SourceRepository: return "Quickshell config from " + package.target;
	case InstallAction::Unavailable: return "no macOS equivalent found";
	}
	return {};
}

bool confirmProceed() {
	std::fputs(":: Proceed with installation? [Y/n] ", stdout);
	std::fflush(stdout);
	std::array<char, 16> answer {};
	if (!std::fgets(answer.data(), static_cast<int>(answer.size()), stdin)) return false;
	return answer[0] == '\n' || answer[0] == 'y' || answer[0] == 'Y';
}

QStringList commandWords(const QStringList& arguments) {
	QStringList words = arguments.mid(2);
	if (words.size() == 1 && words.first().contains(' ')) return QProcess::splitCommand(words.first());
	return words;
}

}

int runLinuxInstall(int argc, char** argv) {
	QCoreApplication application(argc, argv);

	QStringList words = commandWords(application.arguments());
	if (words.isEmpty()) {
		printError("usage: qs --install <linux install command>");
		printError("example: qs --install sudo pacman -S ttf-rubik fish btop");
		return 1;
	}

	LinuxInstallRequest request = parseLinuxInstallCommand(words);
	for (const auto& error: request.errors) printError(":: " + error);
	for (const auto& skipped: request.skippedCommands) {
		printLine(":: Skipping '" + skipped + "' (Linux-only step, not run on macOS)");
	}
	if (request.packages.isEmpty() && request.repositories.isEmpty()) {
		return request.errors.isEmpty() ? 0 : 1;
	}

	QString homebrew = findHomebrew();
	if (homebrew.isEmpty()) printLine(":: Homebrew not found; tools and fonts cannot be installed (https://brew.sh)");

	printLine(":: Resolving packages...");
	QList<ResolvedPackage> resolvedPackages;
	qsizetype longestName = 0;
	for (const auto& package: request.packages) longestName = std::max(longestName, package.size());
	for (const auto& package: request.packages) {
		resolvedPackages << resolvePackage(package, homebrew);
		printLine("   " + package.leftJustified(longestName + 2) + describe(resolvedPackages.last()));
	}
	for (const auto& repository: request.repositories) printLine("   Quickshell config from " + repository);

	QStringList formulae;
	QStringList casks;
	QList<ResolvedPackage> repositories;
	bool everythingResolved = true;
	for (const auto& package: resolvedPackages) {
		if (package.action == InstallAction::HomebrewFormula) formulae << package.target;
		if (package.action == InstallAction::HomebrewCask) casks << package.target;
		if (package.action == InstallAction::SourceRepository) repositories << package;
		if (package.action == InstallAction::Unavailable) everythingResolved = false;
	}
	for (const auto& repository: request.repositories) repositories << ResolvedPackage {.action = InstallAction::SourceRepository, .target = repository};

	if (formulae.isEmpty() && casks.isEmpty() && repositories.isEmpty()) {
		printLine(":: Nothing to install");
		return everythingResolved ? 0 : 1;
	}
	if (!request.skipConfirmation && !confirmProceed()) {
		printLine(":: Cancelled");
		return 1;
	}

	bool allSucceeded = everythingResolved;
	if (!formulae.isEmpty()) allSucceeded &= runVisibly(homebrew, QStringList {"install", "--formula"} + formulae);
	if (!casks.isEmpty()) allSucceeded &= runVisibly(homebrew, QStringList {"install", "--cask"} + casks);
	for (const auto& repository: repositories) {
		allSucceeded &= installQuickshellConfig(repository.target, repository.configName);
	}
	return allSucceeded ? 0 : 1;
}

}

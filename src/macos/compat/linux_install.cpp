#include <algorithm>
#include <array>
#include <cstdio>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qdiriterator.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qprocess.h>
#include <qregularexpression.h>
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

qsizetype countQmlFiles(const QString& directory) {
	qsizetype count = 0;
	QDirIterator iterator(directory, {"*.qml"}, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		iterator.next();
		++count;
	}
	return count;
}

QString findShellEntryDirectory(const QString& repositoryPath) {
	QString bestDirectory;
	qsizetype mostQmlFiles = -1;
	QDirIterator iterator(repositoryPath, {"shell.qml"}, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString entryPath = iterator.next();
		if (QDir(repositoryPath).relativeFilePath(entryPath).startsWith(".git/")) continue;
		QString directory = QFileInfo(entryPath).absolutePath();
		qsizetype qmlFiles = countQmlFiles(directory);
		if (qmlFiles > mostQmlFiles) {
			mostQmlFiles = qmlFiles;
			bestDirectory = directory;
		}
	}
	return bestDirectory;
}

QString readText(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return {};
	return QString::fromUtf8(file.readAll());
}

QStringList moduleUrisImportedUnder(const QString& directory) {
	QStringList uris;
	QDirIterator iterator(directory, {"*.qml"}, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) uris << importedModuleUris(readText(iterator.next()));
	uris.removeDuplicates();
	return uris;
}

QString declaredModuleName(const QString& qmldirPath) {
	static const QRegularExpression moduleLine(R"(^\s*module\s+(\S+))", QRegularExpression::MultilineOption);
	return moduleLine.match(readText(qmldirPath)).captured(1);
}

QHash<QString, QString> moduleDirectoriesUnder(const QString& root) {
	QHash<QString, QString> modules;
	QDirIterator iterator(root, {"qmldir"}, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString qmldirPath = iterator.next();
		if (qmldirPath.contains("/.git/")) continue;
		QString moduleName = declaredModuleName(qmldirPath);
		if (!moduleName.isEmpty() && !modules.contains(moduleName)) {
			modules.insert(moduleName, QFileInfo(qmldirPath).absolutePath());
		}
	}
	return modules;
}

QStringList siblingConfigsUsedBy(const QString& entryDirectory) {
	QDir parentDirectory = QFileInfo(entryDirectory).dir();
	QStringList siblingNames = parentDirectory.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	QString entryName = QFileInfo(entryDirectory).fileName();

	QStringList included {entryName};
	for (qsizetype index = 0; index < included.size(); ++index) {
		for (const auto& uri: moduleUrisImportedUnder(parentDirectory.filePath(included.at(index)))) {
			QString firstComponent = uri.section('.', 0, 0);
			if (siblingNames.contains(firstComponent) && !included.contains(firstComponent)) included << firstComponent;
		}
	}
	return included;
}

bool importsModulesNamedAfter(const QString& directory, const QStringList& folderNames) {
	auto uris = moduleUrisImportedUnder(directory);
	return std::ranges::any_of(uris, [&](const QString& uri) { return folderNames.contains(uri.section('.', 0, 0)); });
}

QString macShellModuleRoot() { return QDir::homePath() + "/Library/Application Support/MacShell/qml"; }

QString findTool(const QString& name) {
	QString onPath = QStandardPaths::findExecutable(name);
	if (!onPath.isEmpty()) return onPath;
	for (const auto& prefix: {QStringLiteral("/opt/homebrew/bin/"), QStringLiteral("/usr/local/bin/"),
	                          QDir::homePath() + "/.cargo/bin/", QStringLiteral("/usr/local/go/bin/")}) {
		if (QFileInfo(prefix + name).isExecutable()) return prefix + name;
	}
	return {};
}

QStringList commandOutputLines(const QString& program, const QStringList& arguments) {
	QByteArray output;
	if (!runQuietly(program, arguments, &output)) return {};
	return QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
}

void bringInBuiltLibraries(const QString& moduleDirectory, const QString& buildDirectory) {
	static const QRegularExpression runtimeLibraryReference(R"(^\s*@rpath/([^/\s]+\.dylib))");
	QStringList pending;
	QDirIterator modules(moduleDirectory, {"*.dylib", "*.so"}, QDir::Files);
	while (modules.hasNext()) pending << modules.next();

	while (!pending.isEmpty()) {
		QString library = pending.takeFirst();
		for (const auto& line: commandOutputLines("/usr/bin/otool", {"-L", library})) {
			auto match = runtimeLibraryReference.match(line);
			if (!match.hasMatch()) continue;
			QString libraryName = match.captured(1);
			QString destination = moduleDirectory + '/' + libraryName;
			if (QFileInfo::exists(destination)) continue;
			QDirIterator builtFiles(buildDirectory, {libraryName}, QDir::Files, QDirIterator::Subdirectories);
			if (!builtFiles.hasNext()) continue;
			QFile::copy(builtFiles.next(), destination);
			pending << destination;
		}
	}
}

void relinkAgainstBundledQt(const QString& moduleDirectory) {
	static const QRegularExpression qtFrameworkReference(R"(^\s*(\S*/(Qt\w+\.framework/\S+)))");
	static const QRegularExpression runtimePathEntry(R"(^\s*path\s+(\S+))");
	QDirIterator iterator(moduleDirectory, {"*.dylib", "*.so"}, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString library = iterator.next();
		for (const auto& line: commandOutputLines("/usr/bin/otool", {"-L", library})) {
			auto match = qtFrameworkReference.match(line);
			if (match.hasMatch() && !match.captured(1).startsWith("@rpath/")) {
				runQuietly("/usr/bin/install_name_tool", {"-change", match.captured(1), "@rpath/" + match.captured(2), library});
			}
		}
		for (const auto& line: commandOutputLines("/usr/bin/otool", {"-l", library})) {
			auto match = runtimePathEntry.match(line);
			if (match.hasMatch() && match.captured(1).startsWith('/')) {
				runQuietly("/usr/bin/install_name_tool", {"-delete_rpath", match.captured(1), library});
			}
		}
		runQuietly("/usr/bin/install_name_tool", {"-add_rpath", "@loader_path", library});
		runQuietly("/usr/bin/codesign", {"--force", "--sign", "-", library});
	}
}

bool copyModuleDirectory(const QString& source, const QString& uriPath, const QString& buildDirectory = {}) {
	QString destination = macShellModuleRoot() + '/' + uriPath;
	if (QFileInfo::exists(destination)) {
		printLine("   " + uriPath.split('/').join('.') + " already installed");
		return true;
	}
	QDir().mkpath(QFileInfo(destination).absolutePath());
	if (!runQuietly("/usr/bin/ditto", {source, destination})) {
		printError(":: could not copy module to " + destination);
		return false;
	}
	if (!buildDirectory.isEmpty()) bringInBuiltLibraries(destination, buildDirectory);
	relinkAgainstBundledQt(destination);
	printLine("   Installed module " + uriPath.split('/').join('.'));
	return true;
}

QString findCMakeProjectDeclaring(const QString& root, const QString& uri) {
	QRegularExpression uriDeclaration("\\bURI\\s+" + QRegularExpression::escape(uri) + "\\b");
	QDirIterator iterator(root, {"CMakeLists.txt"}, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString cmakePath = iterator.next();
		if (uriDeclaration.match(readText(cmakePath)).hasMatch()) return QFileInfo(cmakePath).absolutePath();
	}
	return {};
}

QString buildCMakeProject(const QString& projectDirectory, const QString& buildDirectory, const QString& homebrew) {
	QString cmake = findTool("cmake");
	QString ninja = findTool("ninja");
	if (cmake.isEmpty() || ninja.isEmpty()) {
		printError(":: building " + projectDirectory + " needs cmake and ninja: qs --install sudo pacman -S cmake ninja");
		return {};
	}
	QString qtPrefix = homebrew.isEmpty() ? QString() : QFileInfo(homebrew).absolutePath() + "/../opt/qt";
	printLine(":: Building compiled modules in " + QFileInfo(projectDirectory).fileName());
	bool configured = runVisibly(
	    cmake,
	    {"-S", projectDirectory, "-B", buildDirectory, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
	     "-DCMAKE_MAKE_PROGRAM=" + ninja, "-DCMAKE_PREFIX_PATH=" + QDir::cleanPath(qtPrefix)}
	);
	if (!configured || !runVisibly(cmake, {"--build", buildDirectory})) {
		printError(":: build failed in " + projectDirectory);
		return {};
	}
	return buildDirectory;
}

bool installModulesFor(const QStringList& uris, const QString& checkoutPath, const QString& workspacePath, const QString& homebrew) {
	QHash<QString, QString> sourceModules = moduleDirectoriesUnder(checkoutPath);
	QHash<QString, QString> builtProjects;
	QStringList orderedUris = uris;
	std::sort(orderedUris.begin(), orderedUris.end(), [](const QString& left, const QString& right) {
		return left.size() < right.size();
	});

	bool allInstalled = true;
	QStringList missingModules;
	for (const auto& uri: orderedUris) {
		QStringList uriComponents = uri.split('.');
		QString uriPath = uriComponents.join('/');
		if (QFileInfo::exists(macShellModuleRoot() + '/' + uriPath + "/qmldir")) continue;

		if (sourceModules.contains(uri)) {
			QString moduleDirectory = sourceModules.value(uri);
			qsizetype depth = matchingModuleDepth(QDir::fromNativeSeparators(moduleDirectory).split('/'), uriComponents);
			QDir sourceRoot(moduleDirectory);
			for (qsizetype level = 1; level < depth; ++level) sourceRoot.cdUp();
			allInstalled &= copyModuleDirectory(sourceRoot.absolutePath(), uriComponents.mid(0, uriComponents.size() - depth + 1).join('/'));
			continue;
		}

		QString projectDirectory = findCMakeProjectDeclaring(checkoutPath, uri);
		if (projectDirectory.isEmpty()) {
			missingModules << uri;
			continue;
		}
		if (!builtProjects.contains(projectDirectory)) {
			QString buildDirectory = workspacePath + "/build-" + QString::number(builtProjects.size());
			builtProjects.insert(projectDirectory, buildCMakeProject(projectDirectory, buildDirectory, homebrew));
		}
		QString buildDirectory = builtProjects.value(projectDirectory);
		QString builtModule = buildDirectory.isEmpty() ? QString() : moduleDirectoriesUnder(buildDirectory).value(uri);
		if (builtModule.isEmpty()) {
			allInstalled = false;
			missingModules << uri;
			continue;
		}
		allInstalled &= copyModuleDirectory(builtModule, uriPath, buildDirectory);
	}

	for (const auto& uri: missingModules) {
		printLine("   Module " + uri + " was not found or could not be built; parts using it will not load");
	}
	return allInstalled;
}

struct BackgroundProgram {
	QString name;
	QString projectDirectory;
	bool isRust = false;
};

bool isIgnoredSourcePath(const QString& path) {
	return path.contains("/.git/") || path.contains("/target/") || path.contains("/vendor/") || path.contains("/node_modules/");
}

QString concatenatedSources(const QString& directory, const QStringList& patterns) {
	QString text;
	QDirIterator iterator(directory, patterns, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString path = iterator.next();
		if (!isIgnoredSourcePath(path)) text += readText(path) + '\n';
	}
	return text;
}

bool hasGoMainPackage(const QString& directory) {
	static const QRegularExpression mainPackage(R"(^\s*package\s+main\b)", QRegularExpression::MultilineOption);
	QDirIterator iterator(directory, {"*.go"}, QDir::Files);
	while (iterator.hasNext()) {
		if (mainPackage.match(readText(iterator.next())).hasMatch()) return true;
	}
	return false;
}

QList<BackgroundProgram> findBackgroundPrograms(const QString& checkoutPath, const QString& configText) {
	QList<BackgroundProgram> programs;
	QDirIterator iterator(checkoutPath, {"go.mod", "Cargo.toml"}, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString manifestPath = iterator.next();
		if (isIgnoredSourcePath(manifestPath)) continue;
		QString projectDirectory = QFileInfo(manifestPath).absolutePath();
		bool isRust = manifestPath.endsWith("Cargo.toml");
		QStringList names = isRust ? cargoBinaryNames(readText(manifestPath)) : QStringList {goBinaryName(readText(manifestPath))};
		if (!isRust && !hasGoMainPackage(projectDirectory)) continue;
		for (const auto& name: names) {
			if (!name.isEmpty() && mentionsProgram(configText, name)) programs << BackgroundProgram {name, projectDirectory, isRust};
		}
	}
	return programs;
}

bool launchesQuickshellItself(const QString& projectDirectory) {
	static const QRegularExpression quickshellLaunch(R"pattern("(qs|quickshell)")pattern");
	return quickshellLaunch.match(concatenatedSources(projectDirectory, {"*.go", "*.rs"})).hasMatch();
}

QString findStartCommand(const QString& checkoutPath, const QString& programName) {
	QDirIterator iterator(checkoutPath, {"*.service", "*.conf", "*.lua", "*.sh"}, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString path = iterator.next();
		if (isIgnoredSourcePath(path)) continue;
		QString command = startCommandFor(programName, readText(path));
		if (!command.isEmpty()) return command;
	}
	return {};
}

bool runBuild(const QString& program, const QStringList& arguments, const QString& directory,
              const QProcessEnvironment& environment, QString* output) {
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.setWorkingDirectory(directory);
	process.setProcessEnvironment(environment);
	process.start(program, arguments);
	bool finished = process.waitForFinished(-1);
	*output = QString::fromUtf8(process.readAll());
	return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString buildBackgroundProgram(const BackgroundProgram& program, const QString& workspacePath) {
	QString toolName = program.isRust ? "cargo" : "go";
	QString tool = findTool(toolName);
	if (tool.isEmpty()) {
		printLine("   " + program.name + " needs " + (program.isRust ? "Rust" : "Go") + " to build: qs --install sudo pacman -S "
		          + (program.isRust ? "rust" : "go"));
		return {};
	}

	printLine(":: Building background program " + program.name);
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	QString output;
	QString builtBinary;
	bool built = false;
	if (program.isRust) {
		environment.insert("CARGO_TARGET_DIR", workspacePath + "/cargo-target");
		built = runBuild(tool, {"build", "--release", "--bin", program.name}, program.projectDirectory, environment, &output);
		builtBinary = workspacePath + "/cargo-target/release/" + program.name;
	} else {
		builtBinary = workspacePath + "/go-bin/" + program.name;
		built = runBuild(tool, {"build", "-o", builtBinary, "."}, program.projectDirectory, environment, &output);
	}

	if (built && QFileInfo(builtBinary).isExecutable()) return builtBinary;

	QStringList features = linuxOnlyFeaturesIn(output);
	if (!features.isEmpty()) {
		printLine("   " + program.name + " uses Linux-only system features (" + features.join(", ") + ") and needs a macOS port");
	} else {
		printLine("   " + program.name + " failed to build:");
		for (const auto& line: output.split('\n', Qt::SkipEmptyParts).mid(-8)) printLine("     " + line);
	}
	return {};
}

QString installedProgramsRecord() { return QDir::homePath() + "/Library/Application Support/MacShell/installed-programs"; }

bool installProgramBinary(const QString& builtBinary, const QString& programName) {
	QString binDirectory = QDir::homePath() + "/.local/bin";
	QString destination = binDirectory + '/' + programName;
	QStringList ownedPrograms = readText(installedProgramsRecord()).split('\n', Qt::SkipEmptyParts);
	if (QFileInfo::exists(destination) && !ownedPrograms.contains(programName)) {
		printLine("   " + destination + " already exists and was not installed by MacShell, keeping it");
		return false;
	}

	QDir().mkpath(binDirectory);
	QFile::remove(destination);
	if (!QFile::copy(builtBinary, destination)) {
		printError(":: could not install " + destination);
		return false;
	}
	QFile::setPermissions(destination, QFile::permissions(destination) | QFileDevice::ExeOwner | QFileDevice::ExeUser);

	if (!ownedPrograms.contains(programName)) {
		QDir().mkpath(QFileInfo(installedProgramsRecord()).absolutePath());
		QFile record(installedProgramsRecord());
		if (record.open(QIODevice::Append)) record.write((programName + '\n').toUtf8());
	}
	printLine("   Installed " + programName + " to " + destination);
	return true;
}

void registerCompanion(const QString& configDirectory, const QString& command) {
	QString companionsPath = configDirectory + "/.macshell-companions";
	if (readText(companionsPath).split('\n').contains(command)) return;
	QFile companions(companionsPath);
	if (companions.open(QIODevice::Append)) companions.write((command + '\n').toUtf8());
}

void installBackgroundPrograms(const QString& checkoutPath, const QString& configText, const QString& entryConfigDirectory,
                               const QString& workspacePath) {
	auto programs = findBackgroundPrograms(checkoutPath, configText);
	for (const auto& program: programs) {
		QString builtBinary = buildBackgroundProgram(program, workspacePath);
		if (builtBinary.isEmpty() || !installProgramBinary(builtBinary, program.name)) continue;

		QString startCommand = findStartCommand(checkoutPath, program.name);
		if (launchesQuickshellItself(program.projectDirectory)) {
			printLine("   " + program.name + " starts the shell itself; launch the setup with: "
			          + (startCommand.isEmpty() ? program.name : startCommand));
		} else if (!startCommand.isEmpty()) {
			registerCompanion(entryConfigDirectory, startCommand);
			printLine("   " + program.name + " will start and stop together with the shell");
		} else {
			printLine("   " + program.name + " is run by the shell when it needs it");
		}
	}
}

bool installQuickshellConfig(const QString& repositoryUrl, QString configName, const QString& homebrew) {
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

	QString entryFolderName = QFileInfo(entryDirectory).fileName();
	QStringList configFolders = entryDirectory == checkoutPath ? QStringList {entryFolderName} : siblingConfigsUsedBy(entryDirectory);
	bool linkedConfigs = configFolders.size() > 1 || importsModulesNamedAfter(entryDirectory, configFolders);

	static const QStringList genericFolderNames {"quickshell", "shell", "qs", "config", ".config", "repository"};
	if (linkedConfigs) {
		configName = entryFolderName;
	} else if (entryDirectory != checkoutPath && !genericFolderNames.contains(entryFolderName.toLower())) {
		configName = entryFolderName;
	}
	if (configName.isEmpty()) configName = configNameFromRepositoryUrl(repositoryUrl);

	QString configsRoot = QDir::homePath() + "/.config/quickshell";
	QDir().mkpath(configsRoot);
	QDir entryParent = QFileInfo(entryDirectory).dir();
	QStringList importedUris;
	bool allSucceeded = true;

	for (const auto& folder: configFolders) {
		QString source = folder == entryFolderName ? entryDirectory : entryParent.filePath(folder);
		QString installedName = folder == entryFolderName ? configName : folder;
		QString destination = configsRoot + '/' + installedName;
		importedUris << moduleUrisImportedUnder(source);
		if (QFileInfo::exists(destination)) {
			printLine(":: " + destination + " already exists, leaving it untouched");
			continue;
		}
		if (!runQuietly("/usr/bin/ditto", {source, destination})) {
			printError(":: could not copy the config to " + destination);
			allSucceeded = false;
			continue;
		}
		printLine(":: Installed Quickshell config '" + installedName + "' to " + destination);
	}

	importedUris.removeDuplicates();
	importedUris.removeIf([&](const QString& uri) {
		return isBuiltinModuleUri(uri) || configFolders.contains(uri.section('.', 0, 0));
	});
	if (!importedUris.isEmpty()) {
		printLine(":: Installing the QML modules it imports");
		allSucceeded &= installModulesFor(importedUris, checkoutPath, workspace.path(), homebrew);
	}

	QString installedConfigsText;
	for (const auto& folder: configFolders) {
		QString source = folder == entryFolderName ? entryDirectory : entryParent.filePath(folder);
		installedConfigsText += concatenatedSources(source, {"*.qml", "*.js"});
	}
	installBackgroundPrograms(checkoutPath, installedConfigsText, configsRoot + '/' + configName, workspace.path());

	printLine("   Start it with: qs -c " + configName);
	return allSucceeded;
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
		allSucceeded &= installQuickshellConfig(repository.target, repository.configName, homebrew);
	}
	return allSucceeded ? 0 : 1;
}

}

#include "linux_install_parser.hpp"

#include <qfileinfo.h>
#include <qhash.h>
#include <qregularexpression.h>
#include <qset.h>

namespace qs::macos::compat {

namespace {

const QSet<QString>& packagesCoveredByMacShell() {
	static const QSet<QString> packages {
	    "quickshell",
	    "hyprland",
	    "hyprctl",
	    "brightnessctl",
	    "ddcutil",
	    "wl-clipboard",
	    "cliphist",
	    "swww",
	    "awww",
	    "grim",
	    "hyprpicker",
	    "libnotify",
	    "pipewire",
	    "pipewire-pulse",
	    "pipewire-alsa",
	    "pipewire-jack",
	    "wireplumber",
	    "pulseaudio",
	    "networkmanager",
	    "bluez",
	    "bluez-utils",
	    "upower",
	    "power-profiles-daemon",
	    "xdg-utils",
	    "polkit",
	    "polkit-kde-agent",
	    "hyprpolkitagent",
	    "xdg-desktop-portal",
	    "xdg-desktop-portal-hyprland",
	    "xdg-desktop-portal-gtk",
	    "wayland",
	    "wayland-protocols",
	    "gnome-keyring",
	    "linux-pam",
	    "pam",
	};
	return packages;
}

const QHash<QString, QString>& homebrewAliases() {
	static const QHash<QString, QString> aliases {
	    {"nodejs", "node"},
	    {"github-cli", "gh"},
	    {"python-pip", "python"},
	    {"python3", "python"},
	    {"imagemagick6", "imagemagick"},
	    {"ttf-font-awesome", "font-fontawesome"},
	};
	return aliases;
}

const QStringList& pacmanStyleManagers() {
	static const QStringList managers {"pacman", "yay", "paru", "pikaur", "aura", "trizen", "pamac"};
	return managers;
}

const QStringList& verbStyleManagers() {
	static const QStringList managers {"apt", "apt-get", "dnf", "yum", "zypper", "xbps-install", "apk", "emerge", "nix-env"};
	return managers;
}

QString stripPackageSuffixes(QString name) {
	static const QStringList suffixes {"-git", "-bin", "-stable", "-nightly"};
	bool stripped = true;
	while (stripped) {
		stripped = false;
		for (const auto& suffix: suffixes) {
			if (name.endsWith(suffix) && name.size() > suffix.size()) {
				name.chop(suffix.size());
				stripped = true;
			}
		}
	}
	return name;
}

QString packageNameFromArgument(const QString& argument) {
	QString name = argument.section('/', -1);
	static const QRegularExpression versionConstraint("[<>=].*$");
	name.remove(versionConstraint);
	return name.toLower();
}

bool isNoConfirmFlag(const QString& argument) {
	return argument == "--noconfirm" || argument == "-y" || argument == "--yes" || argument == "--assume-yes"
	    || argument == "--non-interactive" || argument == "-n";
}

void parsePacmanStyle(const QString& manager, const QStringList& arguments, LinuxInstallRequest& request) {
	static const QRegularExpression syncOperation("^-S[yuw]*$");
	bool hasSyncOperation = false;
	bool hasOtherOperation = false;
	QStringList packages;

	for (const auto& argument: arguments) {
		if (argument == "--sync" || syncOperation.match(argument).hasMatch()) {
			hasSyncOperation = true;
		} else if (argument == "--noconfirm") {
			request.skipConfirmation = true;
		} else if (argument.startsWith("--")) {
			continue;
		} else if (argument.startsWith('-')) {
			hasOtherOperation = true;
		} else {
			packages << packageNameFromArgument(argument);
		}
	}

	bool implicitInstall = manager != "pacman" && !hasOtherOperation && !packages.isEmpty();
	if (hasOtherOperation || (!hasSyncOperation && !implicitInstall)) {
		request.errors << QString("%1 %2: only install commands (-S) are supported").arg(manager, arguments.join(' '));
		return;
	}
	request.packages << packages;
}

void parseVerbStyle(const QString& manager, const QStringList& arguments, LinuxInstallRequest& request) {
	static const QStringList installVerbs {"install", "in", "add", "-i", "-iA"};
	bool verbSeen = manager == "xbps-install" || manager == "emerge";
	QStringList packages;

	for (const auto& argument: arguments) {
		if (isNoConfirmFlag(argument)) {
			request.skipConfirmation = true;
		} else if (!verbSeen && installVerbs.contains(argument)) {
			verbSeen = true;
		} else if (argument.startsWith('-')) {
			continue;
		} else if (!verbSeen) {
			request.errors << QString("%1 %2: only install commands are supported").arg(manager, arguments.join(' '));
			return;
		} else {
			packages << packageNameFromArgument(argument);
		}
	}

	if (!verbSeen || packages.isEmpty()) {
		request.errors << QString("%1 %2: no packages to install").arg(manager, arguments.join(' '));
		return;
	}
	request.packages << packages;
}

void parseGitClone(const QStringList& arguments, LinuxInstallRequest& request) {
	QStringList positional;
	for (const auto& argument: arguments) {
		if (!argument.startsWith('-')) positional << argument;
	}
	if (positional.size() < 2 || positional.first() != "clone") {
		request.skippedCommands << "git " + arguments.join(' ');
		return;
	}
	request.repositories << positional.at(1);
}

void parseSegment(const QStringList& segment, LinuxInstallRequest& request) {
	qsizetype commandIndex = 0;
	while (commandIndex < segment.size()) {
		const auto& word = segment.at(commandIndex);
		bool isPrivilegeWrapper = word == "sudo" || word == "doas" || word == "run0";
		bool isEnvironmentAssignment = !word.startsWith('-') && word.contains('=') && !word.contains('/');
		if (!isPrivilegeWrapper && !isEnvironmentAssignment) break;
		++commandIndex;
	}
	if (commandIndex >= segment.size()) return;

	QString manager = QFileInfo(segment.at(commandIndex)).fileName();
	QStringList arguments = segment.mid(commandIndex + 1);

	if (pacmanStyleManagers().contains(manager)) {
		parsePacmanStyle(manager, arguments, request);
	} else if (verbStyleManagers().contains(manager)) {
		parseVerbStyle(manager, arguments, request);
	} else if (manager == "git") {
		parseGitClone(arguments, request);
	} else {
		qsizetype repositoriesBefore = request.repositories.size();
		for (const auto& word: segment) {
			QString repository = githubRepositoryFromWord(word);
			if (!repository.isEmpty()) request.repositories << repository;
		}
		if (request.repositories.size() != repositoriesBefore) return;
		static const QRegularExpression webAddress(R"pattern(https?://[^\s"'()<>|;]+)pattern");
		bool foundInstallerScript = false;
		for (const auto& word: segment) {
			auto match = webAddress.match(word);
			if (!match.hasMatch()) continue;
			request.installerScripts << match.captured(0);
			foundInstallerScript = true;
		}
		if (!foundInstallerScript) request.skippedCommands << segment.mid(commandIndex).join(' ');
	}
}

}

LinuxInstallRequest parseLinuxInstallCommand(const QStringList& words) {
	static const QStringList separators {"&&", "||", ";", "|"};
	LinuxInstallRequest request;
	QStringList segment;

	for (QString word: words) {
		bool endsSegment = false;
		if (word.size() > 1 && word.endsWith(';')) {
			word.chop(1);
			endsSegment = true;
		}
		if (separators.contains(word)) {
			parseSegment(segment, request);
			segment.clear();
			continue;
		}
		segment << word;
		if (endsSegment) {
			parseSegment(segment, request);
			segment.clear();
		}
	}
	parseSegment(segment, request);
	request.packages.removeDuplicates();
	request.repositories.removeDuplicates();
	request.installerScripts.removeDuplicates();
	return request;
}

QStringList fontCaskCandidates(const QString& linuxPackage) {
	static const QStringList fontPrefixes {"ttf-", "otf-", "woff2-", "nerd-fonts-"};
	QString base = linuxPackage;
	bool nerdFont = base.startsWith("nerd-fonts-");
	bool isFont = false;
	for (const auto& prefix: fontPrefixes) {
		if (base.startsWith(prefix)) {
			base.remove(0, prefix.size());
			isFont = true;
			break;
		}
	}
	if (!isFont) return {};

	static const QStringList fontSuffixes {"-git", "-bin", "-vf", "-variable", "-static", "-nerd", "-nerd-font"};
	bool stripped = true;
	while (stripped) {
		stripped = false;
		for (const auto& suffix: fontSuffixes) {
			if (base.endsWith(suffix) && base.size() > suffix.size()) {
				if (suffix.startsWith("-nerd")) nerdFont = true;
				base.chop(suffix.size());
				stripped = true;
			}
		}
	}

	if (homebrewAliases().contains(linuxPackage)) return {homebrewAliases().value(linuxPackage)};
	if (nerdFont) return {"font-" + base + "-nerd-font"};
	return {"font-" + base, "font-" + base + "-nerd-font"};
}

QString homebrewNameFor(const QString& linuxPackage) {
	QString name = stripPackageSuffixes(linuxPackage);
	return homebrewAliases().value(name, name);
}

QString quickshellConfigNameFor(const QString& linuxPackage) {
	static const QStringList configSuffixes {"-shell", "-dots", "-dotfiles", "-quickshell", "-qs"};
	QString name = stripPackageSuffixes(linuxPackage);
	for (const auto& suffix: configSuffixes) {
		if (name.endsWith(suffix) && name.size() > suffix.size()) {
			name.chop(suffix.size());
			break;
		}
	}
	return name;
}

bool isCoveredByMacShell(const QString& linuxPackage) {
	QString name = stripPackageSuffixes(linuxPackage);
	return name.startsWith("qt6-") || name.startsWith("qt5-") || packagesCoveredByMacShell().contains(name);
}

QString githubRepositoryFromWord(const QString& word) {
	static const QRegularExpression githubPath(
	    R"((?:https?://)?(?:raw\.githubusercontent\.com|github\.com)/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+))"
	);
	auto match = githubPath.match(word);
	if (!match.hasMatch()) return {};
	QString repository = match.captured(2);
	if (repository.endsWith(".git")) repository.chop(4);
	return "https://github.com/" + match.captured(1) + '/' + repository;
}

QStringList githubRepositoriesIn(const QString& text) {
	static const QRegularExpression githubAddress(
	    R"((?:https?://)?(?:raw\.githubusercontent\.com|github\.com)/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)"
	);
	QStringList repositories;
	auto matches = githubAddress.globalMatch(text);
	while (matches.hasNext()) repositories << githubRepositoryFromWord(matches.next().captured(0));
	repositories.removeDuplicates();
	return repositories;
}

QString configNameFromInstallerUrl(const QString& url) {
	static const QRegularExpression hostName(R"(^https?://(?:www\.)?([A-Za-z0-9-]+))");
	return hostName.match(url).captured(1).toLower();
}

QStringList importedModuleUris(const QString& qmlSource) {
	static const QRegularExpression moduleImport(R"(^\s*import\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*))",
	                                             QRegularExpression::MultilineOption);
	QStringList uris;
	auto matches = moduleImport.globalMatch(qmlSource);
	while (matches.hasNext()) uris << matches.next().captured(1);
	uris.removeDuplicates();
	return uris;
}

bool isBuiltinModuleUri(const QString& uri) {
	QString firstComponent = uri.section('.', 0, 0);
	return firstComponent.startsWith("Qt") || firstComponent == "Quickshell" || firstComponent == "qs"
	    || firstComponent == "org";
}

qsizetype matchingModuleDepth(const QStringList& directoryComponents, const QStringList& uriComponents) {
	if (directoryComponents.isEmpty() || uriComponents.isEmpty()) return 0;
	qsizetype depth = 1;
	while (depth < uriComponents.size() && depth < directoryComponents.size()
	       && directoryComponents.at(directoryComponents.size() - 1 - depth)
	              == uriComponents.at(uriComponents.size() - 1 - depth))
	{
		++depth;
	}
	return depth;
}

QString goBinaryName(const QString& goModText) {
	static const QRegularExpression moduleLine(R"(^\s*module\s+(\S+))", QRegularExpression::MultilineOption);
	return moduleLine.match(goModText).captured(1).section('/', -1);
}

QStringList cargoBinaryNames(const QString& cargoTomlText) {
	static const QRegularExpression sectionHeader(R"(^\s*\[+([^\]]+)\]+\s*$)");
	static const QRegularExpression nameEntry(R"pattern(^\s*name\s*=\s*"([^"]+)")pattern");
	QStringList names;
	QString currentSection;
	for (const auto& line: cargoTomlText.split('\n')) {
		auto header = sectionHeader.match(line);
		if (header.hasMatch()) {
			currentSection = header.captured(1).trimmed();
			continue;
		}
		auto name = nameEntry.match(line);
		if (name.hasMatch() && (currentSection == "package" || currentSection == "bin")) names << name.captured(1);
	}
	names.removeDuplicates();
	return names;
}

bool mentionsProgram(const QString& text, const QString& programName) {
	QRegularExpression invocation("[\"'`/\\s]" + QRegularExpression::escape(programName) + "[\"'`\\s]");
	return invocation.match(text).hasMatch();
}

QString startCommandFor(const QString& programName, const QString& fileText) {
	static const QRegularExpression startLine(R"(^\s*(?:ExecStart\s*=|exec-once\s*=|exec\s+)\s*(.+?)\s*$)",
	                                          QRegularExpression::MultilineOption);
	auto lines = startLine.globalMatch(fileText);
	while (lines.hasNext()) {
		QString command = lines.next().captured(1);
		QStringList words = command.split(' ', Qt::SkipEmptyParts);
		while (!words.isEmpty() && (words.first().startsWith('-') || words.first().startsWith('@'))) words.removeFirst();
		if (words.isEmpty()) continue;
		QString executable = words.first().section('/', -1);
		if (executable != programName) continue;
		words[0] = programName;
		return words.join(' ');
	}
	return {};
}

QStringList linuxOnlyFeaturesIn(const QString& buildOutput) {
	static const QList<std::pair<QString, QString>> markers {
	    {"inotify", "inotify (file watching)"},
	    {"eventfd", "eventfd"},
	    {"epoll", "epoll"},
	    {"netlink", "netlink (device events)"},
	    {"pidfd", "pidfd"},
	    {"clock_boottime", "CLOCK_BOOTTIME"},
	    {"signalfd", "signalfd"},
	    {"timerfd", "timerfd"},
	    {"memfd", "memfd"},
	    {"peercred", "SO_PEERCRED"},
	    {"ucred", "ucred"},
	    {"prctl", "prctl"},
	    {"/proc/", "/proc"},
	    {"os::linux", "Linux-only standard library APIs"},
	    {"wayland", "Wayland"},
	    {"dbus", "D-Bus"},
	};
	QString lowered = buildOutput.toLower();
	QStringList features;
	for (const auto& [needle, label]: markers) {
		if (lowered.contains(needle)) features << label;
	}
	return features;
}

namespace {

bool isShellDispatch(const QString& dispatcher, const QString& argument) {
	static const QRegularExpression quickshellCommand(R"(^(qs|quickshell)\s)");
	return dispatcher == "global" || (dispatcher == "exec" && quickshellCommand.match(argument).hasMatch());
}

QString evaluateLuaConcatenation(const QString& expression, const QHash<QString, QString>& luaStrings) {
	QString result;
	for (const auto& part: expression.split("..")) {
		QString token = part.trimmed();
		if (token.size() >= 2 && token.startsWith('"') && token.endsWith('"')) result += token.mid(1, token.size() - 2);
		else if (luaStrings.contains(token)) result += luaStrings.value(token);
		else return {};
	}
	return result;
}

QString bindLineFromChord(const QString& chord, const QString& dispatcher, const QString& argument) {
	QStringList parts = chord.split('+', Qt::SkipEmptyParts);
	for (auto& part: parts) part = part.trimmed();
	if (parts.isEmpty()) return {};
	QString key = parts.takeLast();
	return "bind = " + parts.join(' ') + ", " + key + ", " + dispatcher + ", " + argument;
}

QStringList shortcutsFromLua(const QString& text) {
	static const QRegularExpression luaString(R"pattern(^\s*local\s+(\w+)\s*=\s*"([^"]*)"\s*$)pattern", QRegularExpression::MultilineOption);
	static const QRegularExpression luaBind(
	    R"pattern(hl\.bind\(\s*(?:K\(\s*([^()]*?)\s*\)|([^,()]+?))\s*,\s*hl\.dsp\.(global|exec_cmd)\(\s*"([^"]*)"\s*\))pattern"
	);

	QHash<QString, QString> luaStrings;
	auto assignments = luaString.globalMatch(text);
	while (assignments.hasNext()) {
		auto assignment = assignments.next();
		luaStrings.insert(assignment.captured(1), assignment.captured(2));
	}

	QStringList binds;
	auto matches = luaBind.globalMatch(text);
	while (matches.hasNext()) {
		auto match = matches.next();
		QString chord = evaluateLuaConcatenation(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1), luaStrings);
		QString dispatcher = match.captured(3) == "exec_cmd" ? "exec" : "global";
		if (chord.isEmpty() || !isShellDispatch(dispatcher, match.captured(4))) continue;
		binds << bindLineFromChord(chord, dispatcher, match.captured(4));
	}
	return binds;
}

QStringList shortcutsFromHyprlang(const QString& text) {
	static const QRegularExpression variable(R"(^\s*\$(\w+)\s*=\s*(.*?)\s*$)");
	static const QRegularExpression bind(R"(^\s*bind[a-z]*\s*=\s*(.*?)\s*$)");

	QHash<QString, QString> variables;
	QStringList binds;
	for (const auto& line: text.split('\n')) {
		if (auto match = variable.match(line); match.hasMatch()) {
			variables.insert(match.captured(1), match.captured(2));
			continue;
		}
		auto match = bind.match(line);
		if (!match.hasMatch()) continue;
		QString fields = match.captured(1);
		for (const auto& [name, value]: variables.asKeyValueRange()) fields.replace('$' + name, value);
		QStringList parts = fields.split(',');
		if (parts.size() < 3) continue;
		QString dispatcher = parts.at(2).trimmed();
		QString argument = parts.mid(3).join(',').trimmed();
		if (isShellDispatch(dispatcher, argument)) {
			binds << "bind = " + parts.at(0).trimmed() + ", " + parts.at(1).trimmed() + ", " + dispatcher + ", " + argument;
		}
	}
	return binds;
}

}

QStringList shellShortcutBinds(const QString& hyprlandConfigText) {
	return hyprlandConfigText.contains("hl.bind") ? shortcutsFromLua(hyprlandConfigText) : shortcutsFromHyprlang(hyprlandConfigText);
}


}

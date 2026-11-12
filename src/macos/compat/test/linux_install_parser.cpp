#include <qobject.h>
#include <qtest.h>
#include <qtestcase.h>

#include "../linux_install_parser.hpp"

using namespace qs::macos::compat;

class TestLinuxInstallParser: public QObject {
	Q_OBJECT;

private slots:
	static void pacmanSyncCollectsPackages() {
		auto request = parseLinuxInstallCommand({"sudo", "pacman", "-Syu", "--needed", "fish", "extra/btop", "--noconfirm"});
		QCOMPARE(request.packages, QStringList({"fish", "btop"}));
		QVERIFY(request.skipConfirmation);
		QVERIFY(request.errors.isEmpty());
	}

	static void aurHelperWithoutFlagsInstalls() {
		auto request = parseLinuxInstallCommand({"yay", "caelestia-shell", "ttf-rubik-vf"});
		QCOMPARE(request.packages, QStringList({"caelestia-shell", "ttf-rubik-vf"}));
	}

	static void pacmanRemoveIsRejected() {
		auto request = parseLinuxInstallCommand({"sudo", "pacman", "-Rns", "fish"});
		QVERIFY(request.packages.isEmpty());
		QCOMPARE(request.errors.size(), 1);
	}

	static void aptInstallWithYes() {
		auto request = parseLinuxInstallCommand({"sudo", "apt", "install", "-y", "cava", "jq=1.6"});
		QCOMPARE(request.packages, QStringList({"cava", "jq"}));
		QVERIFY(request.skipConfirmation);
	}

	static void chainedCommandsSplit() {
		auto request = parseLinuxInstallCommand(
		    {"git", "clone", "https://github.com/someone/dots", "&&", "cd", "dots;", "./install.sh", "&&", "paru", "-S", "foot"}
		);
		QCOMPARE(request.repositories, QStringList({"https://github.com/someone/dots"}));
		QCOMPARE(request.packages, QStringList({"foot"}));
		QCOMPARE(request.skippedCommands, QStringList({"cd dots", "./install.sh"}));
	}

	static void fontCandidates() {
		QCOMPARE(fontCaskCandidates("ttf-rubik-vf"), QStringList({"font-rubik", "font-rubik-nerd-font"}));
		QCOMPARE(fontCaskCandidates("ttf-jetbrains-mono-nerd"), QStringList({"font-jetbrains-mono-nerd-font"}));
		QCOMPARE(fontCaskCandidates("ttf-material-symbols-variable-git"), QStringList({"font-material-symbols", "font-material-symbols-nerd-font"}));
		QVERIFY(fontCaskCandidates("fish").isEmpty());
	}

	static void namesAndCoverage() {
		QCOMPARE(homebrewNameFor("nodejs"), QString("node"));
		QCOMPARE(homebrewNameFor("cava-git"), QString("cava"));
		QCOMPARE(quickshellConfigNameFor("caelestia-shell"), QString("caelestia"));
		QCOMPARE(quickshellConfigNameFor("noctalia-shell-git"), QString("noctalia"));
		QVERIFY(isCoveredByMacShell("quickshell-git"));
		QVERIFY(isCoveredByMacShell("qt6-declarative"));
		QVERIFY(!isCoveredByMacShell("foot"));
	}

	static void curlInstallerBecomesRepository() {
		auto request = parseLinuxInstallCommand(
		    {"curl", "-fsSL", "https://raw.githubusercontent.com/ryoku-dev/ryoku/main/ryoku-shell-installer/install.sh", "|", "bash"}
		);
		QCOMPARE(request.repositories, QStringList({"https://github.com/ryoku-dev/ryoku"}));
		QCOMPARE(request.skippedCommands, QStringList({"bash"}));
	}

	static void plainGithubLinkBecomesRepository() {
		auto request = parseLinuxInstallCommand({"https://github.com/caelestia-dots/shell.git"});
		QCOMPARE(request.repositories, QStringList({"https://github.com/caelestia-dots/shell"}));
		QVERIFY(request.skippedCommands.isEmpty());
	}

	static void importsAreCollected() {
		auto uris = importedModuleUris("import QtQuick\n  import Ryoku.Ui.Singletons\nimport \"../kit\"\nimport shell.services 1.0\n");
		QCOMPARE(uris, QStringList({"QtQuick", "Ryoku.Ui.Singletons", "shell.services"}));
		QVERIFY(isBuiltinModuleUri("Qt5Compat.GraphicalEffects"));
		QVERIFY(isBuiltinModuleUri("Quickshell.Io"));
		QVERIFY(isBuiltinModuleUri("qs.Commons"));
		QVERIFY(!isBuiltinModuleUri("Ryoku.Blobs"));
	}

	static void moduleDepthClimbsMatchingFolders() {
		QCOMPARE(matchingModuleDepth({"repo", "quickshell", "stage", "services"}, {"stage", "services"}), qsizetype(2));
		QCOMPARE(matchingModuleDepth({"repo", "ryoku", "ui"}, {"Ryoku", "Ui"}), qsizetype(1));
		QCOMPARE(matchingModuleDepth({"build", "qml", "Ryoku", "Blobs"}, {"Ryoku", "Blobs"}), qsizetype(2));
	}

	static void backgroundProgramDetection() {
		QCOMPARE(goBinaryName("module github.com/someone/ryoku-shell\n\ngo 1.26\n"), QString("ryoku-shell"));
		QCOMPARE(
		    cargoBinaryNames("[package]\nname = \"rice-daemon\"\n[dependencies]\nname = \"ignored\"\n[[bin]]\nname = \"rice-ctl\"\n"),
		    QStringList({"rice-daemon", "rice-ctl"})
		);
		QVERIFY(mentionsProgram("Process { command: [\"rice-daemon\", \"--watch\"] }", "rice-daemon"));
		QVERIFY(!mentionsProgram("property string riceDaemonPath", "rice-daemon"));
	}

	static void startCommandsFromLinuxConfigs() {
		QCOMPARE(startCommandFor("rice-daemon", "[Service]\nExecStart=/usr/bin/rice-daemon --serve\n"), QString("rice-daemon --serve"));
		QCOMPARE(startCommandFor("rice-daemon", "exec-once = rice-daemon\nexec-once = waybar\n"), QString("rice-daemon"));
		QVERIFY(startCommandFor("rice-daemon", "exec-once = waybar\n").isEmpty());
	}

	static void linuxFeaturesNamedFromBuildErrors() {
		QCOMPARE(
		    linuxOnlyFeaturesIn("./watch.go:12: undefined: unix.InotifyInit\n./wake.go:3: undefined: unix.Eventfd\n"),
		    QStringList({"inotify (file watching)", "eventfd"})
		);
		QVERIFY(linuxOnlyFeaturesIn("error: expected ';'").isEmpty());
	}
	static void shellShortcutsFromHyprlandConfigs() {
		QCOMPARE(
		    shellShortcutBinds(
		        "local mod = \"SUPER\"\n"
		        "hl.bind(K(mod .. \" + Space\"), hl.dsp.global(\"ryoku:launcher\")) -- open\n"
		        "hl.bind(K(\"ALT + Space\"), hl.dsp.global(\"ryoku:ask\"))\n"
		        "hl.bind(K(mod .. \" + SHIFT + G\"), hl.dsp.global(\"ryoku:gamebar\"), { dont_inhibit = true })\n"
		        "hl.bind(K(mod .. \" + K\"), hl.dsp.exec_cmd(\"qs -c shell ipc call kit toggle\"))\n"
		        "hl.bind(K(mod .. \" + E\"), hl.dsp.exec_cmd(\"ryoku-app files\"))\n"
		        "hl.bind(K(mod .. \" + F\"), hl.dsp.window.fullscreen())\n"
		    ),
		    QStringList({
		        "bind = SUPER, Space, global, ryoku:launcher",
		        "bind = ALT, Space, global, ryoku:ask",
		        "bind = SUPER SHIFT, G, global, ryoku:gamebar",
		        "bind = SUPER, K, exec, qs -c shell ipc call kit toggle",
		    })
		);
		QCOMPARE(
		    shellShortcutBinds("$mod = SUPER\nbind = $mod, A, global, caelestia:launcher\nbind = $mod, T, exec, foot\nbindr = $mod, D, exec, qs ipc call drawers toggle\n"),
		    QStringList({"bind = SUPER, A, global, caelestia:launcher", "bind = SUPER, D, exec, qs ipc call drawers toggle"})
		);
	}
};

QTEST_MAIN(TestLinuxInstallParser);
#include "linux_install_parser.moc"

#include "companions.hpp"

#include <qcoreapplication.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qlist.h>
#include <qpointer.h>
#include <qprocess.h>
#include <qset.h>
#include <qstandardpaths.h>

namespace qs::macos {

namespace {

constexpr int COMPANION_STOP_TIMEOUT_MILLISECONDS = 2000;

QList<QPointer<QProcess>>& runningCompanions() {
	static QList<QPointer<QProcess>> companions;
	return companions;
}

QSet<QString>& startedConfigDirectories() {
	static QSet<QString> directories;
	return directories;
}

bool isProgramAlreadyRunning(const QString& programName) {
	QProcess pgrep;
	pgrep.start("/usr/bin/pgrep", {"-x", programName.left(15)});
	pgrep.waitForFinished(2000);
	return pgrep.exitStatus() == QProcess::NormalExit && pgrep.exitCode() == 0;
}

void stopCompanions() {
	for (const auto& companion: runningCompanions()) {
		if (!companion || companion->state() == QProcess::NotRunning) continue;
		companion->terminate();
		if (!companion->waitForFinished(COMPANION_STOP_TIMEOUT_MILLISECONDS)) companion->kill();
	}
	runningCompanions().clear();
}

void startCompanion(const QStringList& command, const QString& logDirectory) {
	QString programName = QFileInfo(command.first()).fileName();
	if (isProgramAlreadyRunning(programName)) return;

	QString program = command.first().contains('/') ? command.first() : QStandardPaths::findExecutable(command.first());
	if (program.isEmpty()) {
		qWarning() << "Background program" << command.first() << "was not found on PATH";
		return;
	}

	static const QString watcherScript = QStringLiteral(
	    "\"$@\" & child=$!; trap 'kill $child 2>/dev/null; exit' TERM INT HUP; "
	    "while kill -0 %1 2>/dev/null && kill -0 $child 2>/dev/null; do sleep 1; done; kill $child 2>/dev/null"
	);

	auto* process = new QProcess(qApp);
	process->setStandardOutputFile(logDirectory + '/' + programName + ".log", QIODevice::Append);
	process->setStandardErrorFile(logDirectory + '/' + programName + ".log", QIODevice::Append);
	process->start(
	    "/bin/sh",
	    QStringList {"-c", watcherScript.arg(QCoreApplication::applicationPid()), "macshell-companion", program} + command.mid(1)
	);
	runningCompanions() << process;
}

}

void startConfigCompanions(const QString& configDirectory) {
	QString companionsPath = configDirectory + "/.macshell-companions";
	if (startedConfigDirectories().contains(configDirectory) || !QFileInfo::exists(companionsPath)) return;
	startedConfigDirectories().insert(configDirectory);

	QFile companionsFile(companionsPath);
	if (!companionsFile.open(QIODevice::ReadOnly)) return;

	QString logDirectory = QDir::homePath() + "/Library/Logs/MacShell";
	QDir().mkpath(logDirectory);

	const auto lines = QString::fromUtf8(companionsFile.readAll()).split('\n', Qt::SkipEmptyParts);
	for (const auto& line: lines) {
		QStringList command = QProcess::splitCommand(line.trimmed());
		if (!command.isEmpty() && !command.first().startsWith('#')) startCompanion(command, logDirectory);
	}

	static bool stopHookConnected = false;
	if (!stopHookConnected) {
		QObject::connect(qApp, &QCoreApplication::aboutToQuit, stopCompanions);
		stopHookConnected = true;
	}
}

}

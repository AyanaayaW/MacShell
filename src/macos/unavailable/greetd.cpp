#include "greetd.hpp"

#include <qlist.h>
#include <qstring.h>

QString GreetdState::toString(GreetdState::Enum value) {
	switch (value) {
	case GreetdState::Inactive: return QStringLiteral("Inactive");
	case GreetdState::Authenticating: return QStringLiteral("Authenticating");
	case GreetdState::ReadyToLaunch: return QStringLiteral("Ready to Launch");
	case GreetdState::Launching: return QStringLiteral("Launching");
	case GreetdState::Launched: return QStringLiteral("Launched");
	}
	return QStringLiteral("Invalid");
}

void Greetd::reportUnavailable() {
	emit this->error(QStringLiteral("greetd is not available on macOS; the macOS login window manages sessions."));
}

void Greetd::createSession([[maybe_unused]] const QString& user) { this->reportUnavailable(); }

void Greetd::cancelSession() {}

void Greetd::respond([[maybe_unused]] const QString& response) { this->reportUnavailable(); }

void Greetd::launch([[maybe_unused]] const QList<QString>& command) { this->reportUnavailable(); }

void Greetd::launch(
    [[maybe_unused]] const QList<QString>& command,
    [[maybe_unused]] const QList<QString>& environment
) {
	this->reportUnavailable();
}

void Greetd::launch(
    [[maybe_unused]] const QList<QString>& command,
    [[maybe_unused]] const QList<QString>& environment,
    [[maybe_unused]] bool quit
) {
	this->reportUnavailable();
}

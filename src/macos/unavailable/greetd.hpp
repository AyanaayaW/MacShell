#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

class GreetdState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Inactive = 0,
		Authenticating = 1,
		ReadyToLaunch = 2,
		Launching = 3,
		Launched = 4,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(GreetdState::Enum value);
};

class Greetd: public QObject {
	Q_OBJECT;
	Q_PROPERTY(bool available READ isAvailable CONSTANT);
	Q_PROPERTY(GreetdState::Enum state READ state NOTIFY stateChanged);
	Q_PROPERTY(QString user READ user NOTIFY userChanged);
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Greetd(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static bool isAvailable() { return false; }
	[[nodiscard]] static GreetdState::Enum state() { return GreetdState::Inactive; }
	[[nodiscard]] static QString user() { return {}; }

	Q_INVOKABLE void createSession(const QString& user);
	Q_INVOKABLE void cancelSession();
	Q_INVOKABLE void respond(const QString& response);
	Q_INVOKABLE void launch(const QList<QString>& command);
	Q_INVOKABLE void launch(const QList<QString>& command, const QList<QString>& environment);
	Q_INVOKABLE void launch(const QList<QString>& command, const QList<QString>& environment, bool quit);

signals:
	void authMessage(QString message, bool error, bool responseRequired, bool echoResponse);
	void authFailure(QString message);
	void readyToLaunch();
	void launched();
	void error(QString error);
	void stateChanged();
	void userChanged();

private:
	void reportUnavailable();
};

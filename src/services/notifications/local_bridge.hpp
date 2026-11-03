#pragma once

#include <qobject.h>
#include <qstring.h>

class QLocalServer;

namespace qs::service::notifications {

class LocalNotificationBridge: public QObject {
	Q_OBJECT;

public:
	explicit LocalNotificationBridge(QObject* parent);

	static QString socketPath();

private:
	void acceptConnection();

	QLocalServer* mServer;
};

int runNotifySend(int argc, char** argv);

}

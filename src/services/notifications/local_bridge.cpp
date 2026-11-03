#include "local_bridge.hpp"
#include <cstdio>
#include <optional>
#include <unistd.h>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qlocalserver.h>
#include <qlocalsocket.h>
#include <qlogging.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvariant.h>

#include "../../core/paths.hpp"
#include "notification.hpp"
#include "server.hpp"

namespace qs::service::notifications {

namespace {

constexpr int SOCKET_TIMEOUT_MILLISECONDS = 3000;

NotificationUrgency::Enum urgencyFromName(const QString& name) {
	if (name == QStringLiteral("low")) return NotificationUrgency::Low;
	if (name == QStringLiteral("critical")) return NotificationUrgency::Critical;
	return NotificationUrgency::Normal;
}

QVariant hintValue(const QString& type, const QString& value) {
	if (type == QStringLiteral("int")) return value.toInt();
	if (type == QStringLiteral("double")) return value.toDouble();
	if (type == QStringLiteral("boolean")) return value == QStringLiteral("true");
	if (type == QStringLiteral("byte")) return static_cast<uchar>(value.toUInt());
	return value;
}

void printUsage() {
	std::fputs(
	    "Usage: notify-send [OPTION...] SUMMARY [BODY]\n"
	    "  -u, --urgency=LEVEL        low, normal or critical\n"
	    "  -t, --expire-time=TIME     timeout in milliseconds\n"
	    "  -a, --app-name=APP_NAME    application name\n"
	    "  -i, --icon=ICON            icon name or path\n"
	    "  -c, --category=TYPE        notification category\n"
	    "  -h, --hint=TYPE:NAME:VALUE extra hint\n"
	    "  -r, --replace-id=ID        replace an existing notification\n"
	    "  -A, --action=[NAME=]Text   add an action\n"
	    "  -p, --print-id             print the notification id\n",
	    stderr
	);
}

}

LocalNotificationBridge::LocalNotificationBridge(QObject* parent)
    : QObject(parent)
    , mServer(new QLocalServer(this)) {
	auto path = LocalNotificationBridge::socketPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QLocalServer::removeServer(path);

	this->mServer->setSocketOptions(QLocalServer::UserAccessOption);
	QObject::connect(this->mServer, &QLocalServer::newConnection, this, &LocalNotificationBridge::acceptConnection);
	if (!this->mServer->listen(path)) {
		qWarning() << "Could not start the notify-send bridge at" << path;
	}
}

QString LocalNotificationBridge::socketPath() {
	return QDir(QsPaths::defaultRuntimeDirectory()).filePath(QStringLiteral("quickshell/notify.sock"));
}

void LocalNotificationBridge::acceptConnection() {
	while (auto* socket = this->mServer->nextPendingConnection()) {
		uid_t peerUserId = 0;
		gid_t peerGroupId = 0;
		if (getpeereid(static_cast<int>(socket->socketDescriptor()), &peerUserId, &peerGroupId) != 0
		    || peerUserId != getuid())
		{
			socket->abort();
			socket->deleteLater();
			continue;
		}

		QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
		QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket]() {
			if (!socket->canReadLine()) return;

			auto request = QJsonDocument::fromJson(socket->readLine()).object();

			QVariantMap hints = request.value(QStringLiteral("hints")).toObject().toVariantMap();
			hints.insert(
			    QStringLiteral("urgency"),
			    QVariant::fromValue(urgencyFromName(request.value(QStringLiteral("urgency")).toString()))
			);

			QStringList actions;
			for (const auto& action: request.value(QStringLiteral("actions")).toArray()) {
				actions.append(action.toString());
			}

			auto notificationId = NotificationServer::instance()->Notify(
			    request.value(QStringLiteral("appName")).toString(),
			    static_cast<uint>(request.value(QStringLiteral("replacesId")).toInt()),
			    request.value(QStringLiteral("appIcon")).toString(),
			    request.value(QStringLiteral("summary")).toString(),
			    request.value(QStringLiteral("body")).toString(),
			    actions,
			    hints,
			    request.value(QStringLiteral("expireTimeout")).toInt(-1)
			);

			socket->write(QByteArray::number(notificationId) + '\n');
			socket->flush();
			socket->disconnectFromServer();
		});
	}
}

int runNotifySend(int argc, char** argv) {
	QCoreApplication application(argc, argv);

	QJsonObject request {
	    {QStringLiteral("appName"), QStringLiteral("notify-send")},
	    {QStringLiteral("urgency"), QStringLiteral("normal")},
	    {QStringLiteral("expireTimeout"), -1},
	};
	QJsonObject hints;
	QJsonArray actions;
	QStringList positional;
	auto printId = false;

	auto takeValue = [&](int& index, const QString& argument, const QString& shortFlag, const QString& longFlag) -> std::optional<QString> {
		if (argument == shortFlag || argument == longFlag) {
			if (index + 1 >= argc) return std::nullopt;
			return QString::fromLocal8Bit(argv[++index]);
		}
		if (argument.startsWith(longFlag + u'=')) return argument.sliced(longFlag.size() + 1);
		return std::nullopt;
	};

	for (int index = 1; index < argc; ++index) {
		auto argument = QString::fromLocal8Bit(argv[index]);
		if (index == 1 && argument == QStringLiteral("notify-send")) continue;

		if (argument == QStringLiteral("--help") || argument == QStringLiteral("-?")) {
			printUsage();
			return 0;
		}
		if (argument == QStringLiteral("-p") || argument == QStringLiteral("--print-id")) {
			printId = true;
		} else if (auto value = takeValue(index, argument, QStringLiteral("-u"), QStringLiteral("--urgency"))) {
			request.insert(QStringLiteral("urgency"), *value);
		} else if (auto value = takeValue(index, argument, QStringLiteral("-t"), QStringLiteral("--expire-time"))) {
			request.insert(QStringLiteral("expireTimeout"), value->toInt());
		} else if (auto value = takeValue(index, argument, QStringLiteral("-a"), QStringLiteral("--app-name"))) {
			request.insert(QStringLiteral("appName"), *value);
		} else if (auto value = takeValue(index, argument, QStringLiteral("-i"), QStringLiteral("--icon"))) {
			request.insert(QStringLiteral("appIcon"), *value);
		} else if (auto value = takeValue(index, argument, QStringLiteral("-c"), QStringLiteral("--category"))) {
			hints.insert(QStringLiteral("category"), *value);
		} else if (auto value = takeValue(index, argument, QStringLiteral("-r"), QStringLiteral("--replace-id"))) {
			request.insert(QStringLiteral("replacesId"), value->toInt());
		} else if (auto value = takeValue(index, argument, QStringLiteral("-h"), QStringLiteral("--hint"))) {
			auto parts = value->split(u':');
			if (parts.size() >= 3) {
				hints.insert(parts.at(1), QJsonValue::fromVariant(hintValue(parts.at(0), parts.mid(2).join(u':'))));
			}
		} else if (auto value = takeValue(index, argument, QStringLiteral("-A"), QStringLiteral("--action"))) {
			auto separator = value->indexOf(u'=');
			auto identifier = separator == -1 ? QString::number(actions.size() / 2) : value->first(separator);
			auto label = separator == -1 ? *value : value->sliced(separator + 1);
			actions.append(identifier);
			actions.append(label);
		} else if (argument.startsWith(u'-') && argument.size() > 1) {
			std::fprintf(stderr, "notify-send: unknown option %s\n", argv[index]);
			printUsage();
			return 1;
		} else {
			positional.append(argument);
		}
	}

	if (positional.isEmpty()) {
		printUsage();
		return 1;
	}

	request.insert(QStringLiteral("summary"), positional.at(0));
	request.insert(QStringLiteral("body"), positional.mid(1).join(u' '));
	request.insert(QStringLiteral("hints"), hints);
	request.insert(QStringLiteral("actions"), actions);

	QLocalSocket socket;
	socket.connectToServer(LocalNotificationBridge::socketPath());
	if (!socket.waitForConnected(SOCKET_TIMEOUT_MILLISECONDS)) {
		std::fputs("notify-send: no MacShell notification server is running\n", stderr);
		return 1;
	}

	socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
	socket.flush();

	if (socket.waitForReadyRead(SOCKET_TIMEOUT_MILLISECONDS) && printId) {
		std::fputs(socket.readLine().constData(), stdout);
	}

	return 0;
}

}

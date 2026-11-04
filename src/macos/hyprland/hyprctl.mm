#include "hyprctl.hpp"
#include <cstdio>
#include <unistd.h>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qlocalserver.h>
#include <qlocalsocket.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvariant.h>

#include "../../core/paths.hpp"
#include "hyprland.hpp"

#import <AppKit/AppKit.h>

namespace qs::hyprland::ipc {

namespace {

constexpr int SOCKET_TIMEOUT_MILLISECONDS = 3000;

QByteArray toJson(const QVariant& value) {
	return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Indented);
}

QVariantList monitorObjects() {
	QVariantList monitors;
	for (auto* monitor: HyprlandState::instance()->monitors()->valueList()) {
		monitors.append(monitor->lastIpcObject());
	}
	return monitors;
}

QVariantList workspaceObjects() {
	QVariantList workspaces;
	for (auto* workspace: HyprlandState::instance()->workspaces()->valueList()) {
		workspaces.append(workspace->lastIpcObject());
	}
	return workspaces;
}

QVariantList clientObjects() {
	QVariantList clients;
	for (auto* toplevel: HyprlandState::instance()->toplevels()->valueList()) {
		clients.append(toplevel->bindableLastIpcObject().value());
	}
	return clients;
}

QVariantMap cursorPosition() {
	auto mouseLocation = NSEvent.mouseLocation;
	auto primaryHeight = NSScreen.screens.firstObject.frame.size.height;
	return {
	    {QStringLiteral("x"), static_cast<int>(mouseLocation.x)},
	    {QStringLiteral("y"), static_cast<int>(primaryHeight - mouseLocation.y)},
	};
}

QString describeObject(const QString& heading, const QVariantMap& object) {
	QString text = heading + u'\n';
	for (const auto& [key, value]: object.asKeyValueRange()) {
		auto rendered = value.canConvert<QString>() ? value.toString()
		                                            : QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
		text += u'\t' + key + QStringLiteral(": ") + rendered + u'\n';
	}
	return text;
}

QString describeList(const QString& kind, const QVariantList& objects) {
	QString text;
	for (const auto& object: objects) {
		auto map = object.toMap();
		auto label = map.value(QStringLiteral("name"), map.value(QStringLiteral("title"))).toString();
		text += describeObject(kind + u' ' + label + u':', map) + u'\n';
	}
	return text;
}

QByteArray respond(const QStringList& arguments) {
	auto json = arguments.contains(QStringLiteral("-j"));
	QStringList words;
	for (const auto& argument: arguments) {
		if (argument != QStringLiteral("-j")) words.append(argument);
	}
	if (words.isEmpty()) return "usage: hyprctl [-j] <command> [arguments]\n";

	auto command = words.takeFirst();
	auto* state = HyprlandState::instance();

	if (command == QStringLiteral("dispatch")) {
		state->dispatch(words.join(u' '));
		return "ok\n";
	}

	if (command == QStringLiteral("monitors")) {
		return json ? toJson(monitorObjects()) : describeList(QStringLiteral("Monitor"), monitorObjects()).toUtf8();
	}

	if (command == QStringLiteral("workspaces")) {
		return json ? toJson(workspaceObjects()) : describeList(QStringLiteral("workspace"), workspaceObjects()).toUtf8();
	}

	if (command == QStringLiteral("clients")) {
		return json ? toJson(clientObjects()) : describeList(QStringLiteral("Window"), clientObjects()).toUtf8();
	}

	if (command == QStringLiteral("activeworkspace")) {
		auto* workspace = state->bindableFocusedWorkspace().value();
		auto object = workspace ? workspace->lastIpcObject() : QVariantMap();
		return json ? toJson(object) : describeObject(QStringLiteral("workspace:"), object).toUtf8();
	}

	if (command == QStringLiteral("activewindow")) {
		auto* toplevel = state->bindableActiveToplevel().value();
		auto object = toplevel ? toplevel->bindableLastIpcObject().value() : QVariantMap();
		return json ? toJson(object) : describeObject(QStringLiteral("Window:"), object).toUtf8();
	}

	if (command == QStringLiteral("cursorpos")) {
		auto position = cursorPosition();
		if (json) return toJson(position);
		return QStringLiteral("%1, %2\n")
		    .arg(position.value(QStringLiteral("x")).toInt())
		    .arg(position.value(QStringLiteral("y")).toInt())
		    .toUtf8();
	}

	if (command == QStringLiteral("version")) {
		QVariantMap version {
		    {QStringLiteral("branch"), QStringLiteral("macshell")},
		    {QStringLiteral("tag"), QStringLiteral("macshell-compat")},
		    {QStringLiteral("flags"), QVariantList()},
		};
		return json ? toJson(version) : "Hyprland compatibility layer provided by MacShell\n";
	}

	if (command == QStringLiteral("getoption")) {
		QVariantMap option {
		    {QStringLiteral("option"), words.value(0)},
		    {QStringLiteral("int"), 0},
		    {QStringLiteral("float"), 0.0},
		    {QStringLiteral("str"), QString()},
		    {QStringLiteral("set"), false},
		};
		return json ? toJson(option) : "int: 0\nset: false\n";
	}

	if (command == QStringLiteral("reload") || command == QStringLiteral("keyword")
	    || command == QStringLiteral("hyprsunset") || command == QStringLiteral("setcursor"))
	{
		return "ok\n";
	}

	return QStringLiteral("unknown request: %1 is not supported by MacShell\n").arg(command).toUtf8();
}

}

QString hyprctlSocketPath() {
	return QDir(QsPaths::defaultRuntimeDirectory()).filePath(QStringLiteral("quickshell/hyprctl.sock"));
}

void startHyprctlServer() {
	static QLocalServer* server = nullptr;
	if (server) return;

	auto path = hyprctlSocketPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QLocalServer::removeServer(path);

	server = new QLocalServer(QCoreApplication::instance());
	server->setSocketOptions(QLocalServer::UserAccessOption);
	if (!server->listen(path)) {
		qWarning() << "Could not start the hyprctl bridge at" << path;
		return;
	}

	QObject::connect(server, &QLocalServer::newConnection, server, []() {
		while (auto* socket = server->nextPendingConnection()) {
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

				QStringList arguments;
				for (const auto& argument: QJsonDocument::fromJson(socket->readLine()).array()) {
					arguments.append(argument.toString());
				}

				socket->write(respond(arguments));
				socket->flush();
				socket->disconnectFromServer();
			});
		}
	});
}

int runHyprctl(int argc, char** argv) {
	QCoreApplication application(argc, argv);

	QJsonArray arguments;
	for (int index = 1; index < argc; ++index) {
		auto argument = QString::fromLocal8Bit(argv[index]);
		if (index == 1 && argument == QStringLiteral("hyprctl")) continue;
		arguments.append(argument);
	}

	QLocalSocket socket;
	socket.connectToServer(hyprctlSocketPath());
	if (!socket.waitForConnected(SOCKET_TIMEOUT_MILLISECONDS)) {
		std::fputs("hyprctl: MacShell is not running\n", stderr);
		return 1;
	}

	socket.write(QJsonDocument(arguments).toJson(QJsonDocument::Compact) + '\n');
	socket.flush();

	QByteArray response;
	while (socket.waitForReadyRead(SOCKET_TIMEOUT_MILLISECONDS)) response += socket.readAll();
	response += socket.readAll();

	std::fwrite(response.constData(), 1, static_cast<size_t>(response.size()), stdout);
	return response.startsWith("unknown request") ? 1 : 0;
}

}

#include "hyprctl.hpp"
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <unistd.h>

#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qjsondocument.h>
#include <qlist.h>
#include <qlocalserver.h>
#include <qlocalsocket.h>
#include <qpointer.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtenvironmentvariables.h>
#include <qvariant.h>

#include "../../core/paths.hpp"
#include "hyprland.hpp"
#include "keyboard_layouts.hpp"

#import <AppKit/AppKit.h>

namespace qs::hyprland::ipc {

namespace {

constexpr int SOCKET_TIMEOUT_MILLISECONDS = 3000;
const QString BATCH_PREFIX = QStringLiteral("[[BATCH]]");
const QString KEYBOARD_NAME = QStringLiteral("macos-keyboard");

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

QVariantMap keyboardObject() {
	QStringList layoutCodes;
	for (const auto& layout: macos::enabledKeyboardLayouts()) layoutCodes.append(layout.code);
	auto current = macos::currentKeyboardLayout();

	return {
	    {QStringLiteral("address"), QStringLiteral("0x1")},
	    {QStringLiteral("name"), KEYBOARD_NAME},
	    {QStringLiteral("rules"), QString()},
	    {QStringLiteral("model"), QString()},
	    {QStringLiteral("layout"), layoutCodes.join(u',')},
	    {QStringLiteral("variant"), QString()},
	    {QStringLiteral("options"), QString()},
	    {QStringLiteral("active_keymap"), current ? current->name : QStringLiteral("Unknown")},
	    {QStringLiteral("capsLock"), macos::capsLockEnabled()},
	    {QStringLiteral("numLock"), false},
	    {QStringLiteral("main"), true},
	};
}

QVariantMap devicesObject() {
	return {
	    {QStringLiteral("mice"), QVariantList()},
	    {QStringLiteral("keyboards"), QVariantList {keyboardObject()}},
	    {QStringLiteral("tablets"), QVariantList()},
	    {QStringLiteral("touch"), QVariantList()},
	    {QStringLiteral("switches"), QVariantList()},
	};
}

QString describeObject(const QString& heading, const QVariantMap& object) {
	QString text = heading + u'\n';
	for (const auto& [key, value]: object.asKeyValueRange()) {
		auto rendered = value.canConvert<QString>()
		                  ? value.toString()
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

QByteArray formatted(bool json, const QVariant& value, const QString& text) {
	return json ? toJson(value) : text.toUtf8();
}

QByteArray respondToCommand(const QString& commandLine, bool json) {
	auto words = commandLine.trimmed().split(u' ', Qt::SkipEmptyParts);
	if (words.isEmpty()) return "unknown request";

	auto command = words.takeFirst();
	auto argument = words.join(u' ');
	auto* state = HyprlandState::instance();

	if (command == QStringLiteral("dispatch")) {
		return state->dispatch(argument) ? "ok" : "Invalid dispatcher";
	}
	if (command == QStringLiteral("monitors")) {
		return formatted(json, monitorObjects(), describeList(QStringLiteral("Monitor"), monitorObjects()));
	}
	if (command == QStringLiteral("workspaces")) {
		return formatted(json, workspaceObjects(), describeList(QStringLiteral("workspace"), workspaceObjects()));
	}
	if (command == QStringLiteral("clients")) {
		return formatted(json, clientObjects(), describeList(QStringLiteral("Window"), clientObjects()));
	}
	if (command == QStringLiteral("activeworkspace")) {
		auto* workspace = state->bindableFocusedWorkspace().value();
		auto object = workspace ? workspace->lastIpcObject() : QVariantMap();
		return formatted(json, object, describeObject(QStringLiteral("workspace:"), object));
	}
	if (command == QStringLiteral("activewindow")) {
		auto* toplevel = state->bindableActiveToplevel().value();
		auto object = toplevel ? toplevel->bindableLastIpcObject().value() : QVariantMap();
		return formatted(json, object, describeObject(QStringLiteral("Window:"), object));
	}
	if (command == QStringLiteral("devices")) {
		return formatted(json, devicesObject(), describeObject(QStringLiteral("Keyboard:"), keyboardObject()));
	}
	if (command == QStringLiteral("cursorpos")) {
		auto position = cursorPosition();
		return formatted(
		    json,
		    position,
		    QStringLiteral("%1, %2")
		        .arg(position.value(QStringLiteral("x")).toInt())
		        .arg(position.value(QStringLiteral("y")).toInt())
		);
	}
	if (command == QStringLiteral("switchxkblayout")) {
		auto selector = words.value(1, words.value(0));
		return macos::selectKeyboardLayout(selector) ? "ok" : "invalid layout index";
	}
	if (command == QStringLiteral("version")) {
		QVariantMap version {
		    {QStringLiteral("branch"), QStringLiteral("macshell")},
		    {QStringLiteral("commit"), QString()},
		    {QStringLiteral("tag"), QStringLiteral("v0.50.0")},
		    {QStringLiteral("version"), QStringLiteral("0.50.0")},
		    {QStringLiteral("flags"), QVariantList()},
		};
		return formatted(json, version, QStringLiteral("Hyprland 0.50.0 compatibility layer provided by MacShell"));
	}
	if (command == QStringLiteral("instances")) {
		QVariantMap instance {
		    {QStringLiteral("instance"), qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE")},
		    {QStringLiteral("time"), QDateTime::currentSecsSinceEpoch()},
		    {QStringLiteral("pid"), QCoreApplication::applicationPid()},
		    {QStringLiteral("wl_socket"), QString()},
		};
		return formatted(json, QVariantList {instance}, describeObject(QStringLiteral("instance:"), instance));
	}
	if (command == QStringLiteral("getoption")) {
		QVariantMap option {
		    {QStringLiteral("option"), argument},
		    {QStringLiteral("int"), 0},
		    {QStringLiteral("float"), 0.0},
		    {QStringLiteral("str"), QString()},
		    {QStringLiteral("set"), false},
		};
		return formatted(json, option, QStringLiteral("int: 0\nset: false"));
	}
	if (command == QStringLiteral("layers")) {
		QVariantMap layersByMonitor;
		QVariantMap emptyLevels {
		    {QStringLiteral("0"), QVariantList()},
		    {QStringLiteral("1"), QVariantList()},
		    {QStringLiteral("2"), QVariantList()},
		    {QStringLiteral("3"), QVariantList()},
		};
		for (auto* monitor: state->monitors()->valueList()) {
			layersByMonitor.insert(monitor->bindableName().value(), QVariantMap {{QStringLiteral("levels"), emptyLevels}});
		}
		return formatted(json, layersByMonitor, QString());
	}
	if (command == QStringLiteral("splash")) return formatted(json, QVariantMap(), QString());
	if (command == QStringLiteral("binds") || command == QStringLiteral("globalshortcuts")
	    || command == QStringLiteral("configerrors") || command == QStringLiteral("workspacerules")
	    || command == QStringLiteral("layouts"))
	{
		return formatted(json, QVariantList(), QString());
	}
	if (command == QStringLiteral("reload") || command == QStringLiteral("keyword")
	    || command == QStringLiteral("hyprsunset") || command == QStringLiteral("setcursor")
	    || command == QStringLiteral("notify") || command == QStringLiteral("dismissnotify")
	    || command == QStringLiteral("seterror") || command == QStringLiteral("setprop")
	    || command == QStringLiteral("output") || command == QStringLiteral("plugin")
	    || command == QStringLiteral("kill") || command == QStringLiteral("eval"))
	{
		return "ok";
	}

	return QStringLiteral("unknown request: %1 is not supported by MacShell").arg(command).toUtf8();
}

QByteArray respondToRequest(const QByteArray& rawRequest) {
	auto request = QString::fromUtf8(rawRequest).trimmed();
	QString flags;

	auto slashIndex = request.indexOf(u'/');
	auto spaceIndex = request.indexOf(u' ');
	auto bracketIndex = request.indexOf(BATCH_PREFIX);
	if (slashIndex != -1 && (spaceIndex == -1 || slashIndex < spaceIndex)
	    && (bracketIndex == -1 || slashIndex < bracketIndex))
	{
		flags = request.first(slashIndex);
		request = request.sliced(slashIndex + 1);
	}

	auto json = flags.contains(u'j');
	if (!request.startsWith(BATCH_PREFIX)) return respondToCommand(request, json);

	QByteArray combined;
	for (const auto& batchedCommand: request.sliced(BATCH_PREFIX.size()).split(u';', Qt::SkipEmptyParts)) {
		auto commandText = batchedCommand.trimmed();
		auto commandJson = json;
		if (auto commandSlash = commandText.indexOf(u'/');
		    commandSlash != -1 && (commandText.indexOf(u' ') == -1 || commandSlash < commandText.indexOf(u' ')))
		{
			commandJson = commandJson || commandText.first(commandSlash).contains(u'j');
			commandText = commandText.sliced(commandSlash + 1);
		}
		if (!combined.isEmpty()) combined += "\n\n\n";
		combined += respondToCommand(commandText, commandJson);
	}
	return combined;
}

bool isSameUser(QLocalSocket* socket) {
	uid_t peerUserId = 0;
	gid_t peerGroupId = 0;
	return getpeereid(static_cast<int>(socket->socketDescriptor()), &peerUserId, &peerGroupId) == 0
	    && peerUserId == getuid();
}

QLocalServer* listenOn(const QString& path) {
	QLocalServer::removeServer(path);
	auto* server = new QLocalServer(QCoreApplication::instance());
	server->setSocketOptions(QLocalServer::UserAccessOption);
	if (!server->listen(path)) {
		std::fprintf(stderr, "MacShell: unable to listen on %s\n", qPrintable(path));
		delete server;
		return nullptr;
	}
	return server;
}

QString runtimeDirectory() {
	auto directory = qEnvironmentVariable("XDG_RUNTIME_DIR");
	return directory.isEmpty() ? QsPaths::defaultRuntimeDirectory() : directory;
}

QString newestInstanceDirectory() {
	QDir hyprDirectory(QDir(runtimeDirectory()).filePath(QStringLiteral("hypr")));
	auto instances = hyprDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
	for (const auto& instance: instances) {
		if (QFileInfo::exists(QDir(instance.filePath()).filePath(QStringLiteral(".socket.sock")))) {
			return instance.filePath();
		}
	}
	return {};
}

QString instanceDirectory(const QString& requestedInstance) {
	auto signature = requestedInstance.isEmpty() ? qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE")
	                                             : requestedInstance;
	if (signature.isEmpty()) return newestInstanceDirectory();
	return QDir(runtimeDirectory()).filePath(QStringLiteral("hypr/") + signature);
}

}

bool otherShellInstanceRunning() {
	QDir hyprDirectory(QDir(runtimeDirectory()).filePath(QStringLiteral("hypr")));
	auto ownProcessId = QCoreApplication::applicationPid();
	for (const auto& instance: hyprDirectory.entryList({QStringLiteral("macshell_*")}, QDir::Dirs)) {
		bool isNumber = false;
		auto processId = instance.section(u'_', -1).toLongLong(&isNumber);
		if (!isNumber || processId == ownProcessId) continue;
		if (kill(static_cast<pid_t>(processId), 0) == 0) return true;
		QDir(hyprDirectory.filePath(instance)).removeRecursively();
	}
	return false;
}

QString hyprctlSocketPath() {
	return QDir(instanceDirectory({})).filePath(QStringLiteral(".socket.sock"));
}

void startHyprctlServer() {
	static bool started = false;
	if (started) return;
	started = true;

	if (qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR")) {
		qputenv("XDG_RUNTIME_DIR", QsPaths::defaultRuntimeDirectory().toLocal8Bit());
	}
	auto signature = QStringLiteral("macshell_%1_%2")
	                     .arg(QDateTime::currentSecsSinceEpoch())
	                     .arg(QCoreApplication::applicationPid());
	qputenv("HYPRLAND_INSTANCE_SIGNATURE", signature.toLocal8Bit());

	auto directory = instanceDirectory(signature);
	QDir().mkpath(directory);
	QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

	auto* requestServer = listenOn(QDir(directory).filePath(QStringLiteral(".socket.sock")));
	auto* eventServer = listenOn(QDir(directory).filePath(QStringLiteral(".socket2.sock")));

	QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [directory] {
		QDir(directory).removeRecursively();
	});

	if (requestServer) {
		QObject::connect(requestServer, &QLocalServer::newConnection, requestServer, [requestServer] {
			while (auto* socket = requestServer->nextPendingConnection()) {
				if (!isSameUser(socket)) {
					socket->abort();
					socket->deleteLater();
					continue;
				}

				QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
				QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket] {
					socket->write(respondToRequest(socket->readAll()));
					socket->flush();
					socket->disconnectFromServer();
				});
			}
		});
	}

	if (eventServer) {
		static QList<QPointer<QLocalSocket>> eventListeners;
		QObject::connect(eventServer, &QLocalServer::newConnection, eventServer, [eventServer] {
			while (auto* socket = eventServer->nextPendingConnection()) {
				if (!isSameUser(socket)) {
					socket->abort();
					socket->deleteLater();
					continue;
				}
				QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
				eventListeners.append(socket);
			}
		});

		QObject::connect(HyprlandState::instance(), &HyprlandState::rawEvent, eventServer, [](HyprlandIpcEvent* event) {
			auto line = (event->name() + QStringLiteral(">>") + event->data() + u'\n').toUtf8();
			eventListeners.removeIf([](const QPointer<QLocalSocket>& listener) { return listener.isNull(); });
			for (const auto& listener: eventListeners) {
				if (listener->state() == QLocalSocket::ConnectedState) listener->write(line);
			}
		});
	}

	macos::watchKeyboardLayout([] {
		auto layout = macos::currentKeyboardLayout();
		HyprlandState::instance()->emitRawEvent(
		    QStringLiteral("activelayout"),
		    KEYBOARD_NAME + u',' + (layout ? layout->name : QStringLiteral("Unknown"))
		);
	});
}

int runHyprctl(int argc, char** argv) {
	QCoreApplication application(argc, argv);

	QString flags;
	QString requestedInstance;
	QString batch;
	QStringList commandWords;

	for (int index = 1; index < argc; ++index) {
		auto argument = QString::fromLocal8Bit(argv[index]);
		if (index == 1 && argument == QStringLiteral("hyprctl")) continue;

		auto passesArgumentsThrough = !commandWords.isEmpty()
		                           && (commandWords.first() == QStringLiteral("dispatch")
		                               || commandWords.first() == QStringLiteral("keyword")
		                               || commandWords.first() == QStringLiteral("eval"));
		auto isKnownFlag = argument == QStringLiteral("-j") || argument == QStringLiteral("-r")
		                || argument == QStringLiteral("-a");
		if ((commandWords.isEmpty() || (isKnownFlag && !passesArgumentsThrough)) && argument.startsWith(u'-')
		    && argument.size() > 1)
		{
			if (argument == QStringLiteral("-j")) flags += u'j';
			else if (argument == QStringLiteral("-r")) flags += u'r';
			else if (argument == QStringLiteral("-a")) flags += u'a';
			else if (argument == QStringLiteral("--batch") && index + 1 < argc) batch = QString::fromLocal8Bit(argv[++index]);
			else if ((argument == QStringLiteral("-i") || argument == QStringLiteral("--instance")) && index + 1 < argc) {
				requestedInstance = QString::fromLocal8Bit(argv[++index]);
			}
			continue;
		}
		commandWords.append(argument);
	}

	auto request = flags + u'/' + (batch.isEmpty() ? commandWords.join(u' ') : BATCH_PREFIX + batch);

	QLocalSocket socket;
	socket.connectToServer(QDir(instanceDirectory(requestedInstance)).filePath(QStringLiteral(".socket.sock")));
	if (!socket.waitForConnected(SOCKET_TIMEOUT_MILLISECONDS)) {
		std::fputs("HYPRLAND_INSTANCE_SIGNATURE was not set! (Is MacShell running?)\n", stderr);
		return 1;
	}

	socket.write(request.toUtf8());
	socket.flush();

	QByteArray response;
	while (socket.waitForReadyRead(SOCKET_TIMEOUT_MILLISECONDS)) response += socket.readAll();
	response += socket.readAll();

	std::fwrite(response.constData(), 1, static_cast<size_t>(response.size()), stdout);
	if (!response.endsWith('\n')) std::fputc('\n', stdout);
	return response.startsWith("unknown request") ? 1 : 0;
}

}

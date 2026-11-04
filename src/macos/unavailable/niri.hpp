#pragma once

#include <qjsonobject.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/model.hpp"

namespace qs::niri::ipc {

class NiriIpcEvent: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString type READ type CONSTANT);
	Q_PROPERTY(QJsonObject data READ data CONSTANT);
	QML_NAMED_ELEMENT(NiriEvent);
	QML_UNCREATABLE("NiriIpcEvents cannot be created.");

public:
	[[nodiscard]] static QString type() { return {}; }
	[[nodiscard]] static QJsonObject data() { return {}; }
};

class NiriIpcQml: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString socketPath READ socketPath CONSTANT);
	Q_PROPERTY(QObject* focusedWindow READ focusedWindow NOTIFY focusedWindowChanged);
	Q_PROPERTY(bool overviewActive READ overviewActive NOTIFY overviewActiveChanged);
	Q_PROPERTY(QStringList keyboardLayoutNames READ keyboardLayoutNames NOTIFY keyboardLayoutsChanged);
	Q_PROPERTY(qint32 currentKeyboardLayoutIndex READ currentKeyboardLayoutIndex NOTIFY keyboardLayoutSwitched);
	Q_PROPERTY(QString currentKeyboardLayoutName READ currentKeyboardLayoutName NOTIFY keyboardLayoutSwitched);
	Q_PROPERTY(UntypedObjectModel* outputs READ emptyModel CONSTANT);
	Q_PROPERTY(UntypedObjectModel* workspaces READ emptyModel CONSTANT);
	Q_PROPERTY(UntypedObjectModel* windows READ emptyModel CONSTANT);
	QML_NAMED_ELEMENT(Niri);
	QML_SINGLETON;

public:
	explicit NiriIpcQml(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static QString socketPath() { return {}; }
	[[nodiscard]] static QObject* focusedWindow() { return nullptr; }
	[[nodiscard]] static bool overviewActive() { return false; }
	[[nodiscard]] static QStringList keyboardLayoutNames() { return {}; }
	[[nodiscard]] static qint32 currentKeyboardLayoutIndex() { return -1; }
	[[nodiscard]] static QString currentKeyboardLayoutName() { return {}; }
	[[nodiscard]] static UntypedObjectModel* emptyModel() { return UntypedObjectModel::emptyInstance(); }

	Q_INVOKABLE static void dispatch([[maybe_unused]] const QStringList& args) {}
	Q_INVOKABLE static void refreshOutputs() {}
	Q_INVOKABLE static void refreshWorkspaces() {}
	Q_INVOKABLE static void refreshWindows() {}

signals:
	void rawEvent(qs::niri::ipc::NiriIpcEvent* event);
	void focusedWindowChanged();
	void overviewActiveChanged();
	void keyboardLayoutsChanged();
	void keyboardLayoutSwitched();
	void workspacesUpdated();
	void windowsUpdated();
	void outputsUpdated();
};

}

#pragma once

#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/model.hpp"

namespace qs::hyprland::ipc {
class HyprlandMonitor;
class HyprlandWorkspace;
}

namespace qs::i3::ipc {

class I3Monitor;

class I3IpcEvent: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString type READ type CONSTANT);
	Q_PROPERTY(QString data READ data CONSTANT);
	QML_NAMED_ELEMENT(I3Event);
	QML_UNCREATABLE("I3IpcEvents cannot be created.");

public:
	I3IpcEvent(QString type, QString data, QObject* parent);

	[[nodiscard]] QString type() const { return this->mType; }
	[[nodiscard]] QString data() const { return this->mData; }

private:
	QString mType;
	QString mData;
};

class I3Workspace: public QObject {
	Q_OBJECT;
	Q_PROPERTY(qint32 id READ default NOTIFY idChanged BINDABLE bindableId);
	Q_PROPERTY(QString name READ default NOTIFY nameChanged BINDABLE bindableName);
	Q_PROPERTY(qint32 number READ default NOTIFY numberChanged BINDABLE bindableNumber);
	Q_PROPERTY(qint32 num READ default NOTIFY numberChanged BINDABLE bindableNumber);
	Q_PROPERTY(bool urgent READ default NOTIFY urgentChanged BINDABLE bindableUrgent);
	Q_PROPERTY(bool active READ default NOTIFY activeChanged BINDABLE bindableActive);
	Q_PROPERTY(bool focused READ default NOTIFY focusedChanged BINDABLE bindableFocused);
	Q_PROPERTY(qs::i3::ipc::I3Monitor* monitor READ default NOTIFY monitorChanged BINDABLE bindableMonitor);
	Q_PROPERTY(QVariantMap lastIpcObject READ lastIpcObject NOTIFY lastIpcObjectChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("I3Workspaces must be retrieved from the I3 object.");

public:
	I3Workspace(hyprland::ipc::HyprlandWorkspace* source, QObject* parent);

	void synchronize(I3Monitor* monitor);
	Q_INVOKABLE void activate();

	[[nodiscard]] hyprland::ipc::HyprlandWorkspace* source() const { return this->mSource; }
	[[nodiscard]] QVariantMap lastIpcObject() const { return this->mLastIpcObject; }

	[[nodiscard]] QBindable<qint32> bindableId() { return &this->bId; }
	[[nodiscard]] QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QBindable<qint32> bindableNumber() { return &this->bNumber; }
	[[nodiscard]] QBindable<bool> bindableUrgent() { return &this->bUrgent; }
	[[nodiscard]] QBindable<bool> bindableActive() { return &this->bActive; }
	[[nodiscard]] QBindable<bool> bindableFocused() { return &this->bFocused; }
	[[nodiscard]] QBindable<I3Monitor*> bindableMonitor() { return &this->bMonitor; }

signals:
	void idChanged();
	void nameChanged();
	void numberChanged();
	void urgentChanged();
	void activeChanged();
	void focusedChanged();
	void monitorChanged();
	void lastIpcObjectChanged();

private:
	hyprland::ipc::HyprlandWorkspace* mSource;
	QVariantMap mLastIpcObject;

	Q_OBJECT_BINDABLE_PROPERTY(I3Workspace, qint32, bId, &I3Workspace::idChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Workspace, QString, bName, &I3Workspace::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Workspace, qint32, bNumber, &I3Workspace::numberChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Workspace, bool, bUrgent, &I3Workspace::urgentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Workspace, bool, bActive, &I3Workspace::activeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Workspace, bool, bFocused, &I3Workspace::focusedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Workspace, I3Monitor*, bMonitor, &I3Workspace::monitorChanged);
};

class I3Monitor: public QObject {
	Q_OBJECT;
	Q_PROPERTY(qint32 id READ default NOTIFY idChanged BINDABLE bindableId);
	Q_PROPERTY(QString name READ default NOTIFY nameChanged BINDABLE bindableName);
	Q_PROPERTY(bool power READ default NOTIFY powerChanged BINDABLE bindablePower);
	Q_PROPERTY(qs::i3::ipc::I3Workspace* activeWorkspace READ default NOTIFY activeWorkspaceChanged BINDABLE bindableActiveWorkspace);
	Q_PROPERTY(qs::i3::ipc::I3Workspace* focusedWorkspace READ default NOTIFY activeWorkspaceChanged BINDABLE bindableActiveWorkspace);
	Q_PROPERTY(qint32 x READ default NOTIFY xChanged BINDABLE bindableX);
	Q_PROPERTY(qint32 y READ default NOTIFY yChanged BINDABLE bindableY);
	Q_PROPERTY(qint32 width READ default NOTIFY widthChanged BINDABLE bindableWidth);
	Q_PROPERTY(qint32 height READ default NOTIFY heightChanged BINDABLE bindableHeight);
	Q_PROPERTY(qreal scale READ default NOTIFY scaleChanged BINDABLE bindableScale);
	Q_PROPERTY(bool focused READ default NOTIFY focusedChanged BINDABLE bindableFocused);
	Q_PROPERTY(QVariantMap lastIpcObject READ lastIpcObject NOTIFY lastIpcObjectChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("I3Monitors must be retrieved from the I3IpcController object.");

public:
	I3Monitor(hyprland::ipc::HyprlandMonitor* source, QObject* parent);

	void synchronize(I3Workspace* activeWorkspace);

	[[nodiscard]] hyprland::ipc::HyprlandMonitor* source() const { return this->mSource; }
	[[nodiscard]] QVariantMap lastIpcObject() const { return this->mLastIpcObject; }

	[[nodiscard]] QBindable<qint32> bindableId() { return &this->bId; }
	[[nodiscard]] QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QBindable<bool> bindablePower() { return &this->bPower; }
	[[nodiscard]] QBindable<I3Workspace*> bindableActiveWorkspace() { return &this->bActiveWorkspace; }
	[[nodiscard]] QBindable<qint32> bindableX() { return &this->bX; }
	[[nodiscard]] QBindable<qint32> bindableY() { return &this->bY; }
	[[nodiscard]] QBindable<qint32> bindableWidth() { return &this->bWidth; }
	[[nodiscard]] QBindable<qint32> bindableHeight() { return &this->bHeight; }
	[[nodiscard]] QBindable<qreal> bindableScale() { return &this->bScale; }
	[[nodiscard]] QBindable<bool> bindableFocused() { return &this->bFocused; }

signals:
	void idChanged();
	void nameChanged();
	void powerChanged();
	void activeWorkspaceChanged();
	void xChanged();
	void yChanged();
	void widthChanged();
	void heightChanged();
	void scaleChanged();
	void focusedChanged();
	void lastIpcObjectChanged();

private:
	hyprland::ipc::HyprlandMonitor* mSource;
	QVariantMap mLastIpcObject;

	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, qint32, bId, &I3Monitor::idChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, QString, bName, &I3Monitor::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(I3Monitor, bool, bPower, true, &I3Monitor::powerChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, I3Workspace*, bActiveWorkspace, &I3Monitor::activeWorkspaceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, qint32, bX, &I3Monitor::xChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, qint32, bY, &I3Monitor::yChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, qint32, bWidth, &I3Monitor::widthChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, qint32, bHeight, &I3Monitor::heightChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, qreal, bScale, &I3Monitor::scaleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3Monitor, bool, bFocused, &I3Monitor::focusedChanged);
};

class I3State: public QObject {
	Q_OBJECT;

public:
	static I3State* instance();

	[[nodiscard]] ObjectModel<I3Workspace>* workspaces() { return &this->mWorkspaces; }
	[[nodiscard]] ObjectModel<I3Monitor>* monitors() { return &this->mMonitors; }
	[[nodiscard]] QBindable<I3Workspace*> bindableFocusedWorkspace() { return &this->bFocusedWorkspace; }
	[[nodiscard]] QBindable<I3Monitor*> bindableFocusedMonitor() { return &this->bFocusedMonitor; }

	void dispatch(const QString& request);
	[[nodiscard]] I3Workspace* workspaceForName(const QString& name) const;
	[[nodiscard]] I3Monitor* monitorForName(const QString& name) const;

signals:
	void rawEvent(qs::i3::ipc::I3IpcEvent* event);
	void focusedWorkspaceChanged();
	void focusedMonitorChanged();

private:
	I3State();

	void synchronize();
	void emitRawEvent(const QString& type, const QString& data);

	ObjectModel<I3Workspace> mWorkspaces {this};
	ObjectModel<I3Monitor> mMonitors {this};
	QHash<hyprland::ipc::HyprlandWorkspace*, I3Workspace*> mWorkspacesBySource;
	QHash<hyprland::ipc::HyprlandMonitor*, I3Monitor*> mMonitorsBySource;

	Q_OBJECT_BINDABLE_PROPERTY(I3State, I3Workspace*, bFocusedWorkspace, &I3State::focusedWorkspaceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(I3State, I3Monitor*, bFocusedMonitor, &I3State::focusedMonitorChanged);
};

class I3IpcListener: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QList<QString> subscriptions READ subscriptions WRITE setSubscriptions NOTIFY subscriptionsChanged);
	QML_ELEMENT;

public:
	explicit I3IpcListener(QObject* parent = nullptr);

	[[nodiscard]] QList<QString> subscriptions() const { return this->mSubscriptions; }
	void setSubscriptions(QList<QString> subscriptions);

signals:
	void ipcEvent(qs::i3::ipc::I3IpcEvent* event);
	void subscriptionsChanged();

private:
	QList<QString> mSubscriptions;
};

class I3IpcQml: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString socketPath READ socketPath CONSTANT);
	Q_PROPERTY(qs::i3::ipc::I3Workspace* focusedWorkspace READ default NOTIFY focusedWorkspaceChanged BINDABLE bindableFocusedWorkspace);
	Q_PROPERTY(qs::i3::ipc::I3Monitor* focusedMonitor READ default NOTIFY focusedMonitorChanged BINDABLE bindableFocusedMonitor);
	Q_PROPERTY(UntypedObjectModel* monitors READ monitors CONSTANT);
	Q_PROPERTY(UntypedObjectModel* workspaces READ workspaces CONSTANT);
	QML_NAMED_ELEMENT(I3);
	QML_SINGLETON;

public:
	explicit I3IpcQml(QObject* parent = nullptr);

	Q_INVOKABLE static void dispatch(const QString& request);
	Q_INVOKABLE static void refreshMonitors();
	Q_INVOKABLE static void refreshWorkspaces();
	Q_INVOKABLE static qs::i3::ipc::I3Workspace* findWorkspaceByName(const QString& name);
	Q_INVOKABLE static qs::i3::ipc::I3Monitor* findMonitorByName(const QString& name);

	[[nodiscard]] static QString socketPath() { return {}; }
	[[nodiscard]] static ObjectModel<I3Monitor>* monitors() { return I3State::instance()->monitors(); }
	[[nodiscard]] static ObjectModel<I3Workspace>* workspaces() { return I3State::instance()->workspaces(); }
	[[nodiscard]] static QBindable<I3Workspace*> bindableFocusedWorkspace() {
		return I3State::instance()->bindableFocusedWorkspace();
	}
	[[nodiscard]] static QBindable<I3Monitor*> bindableFocusedMonitor() {
		return I3State::instance()->bindableFocusedMonitor();
	}

signals:
	void rawEvent(qs::i3::ipc::I3IpcEvent* event);
	void focusedWorkspaceChanged();
	void focusedMonitorChanged();
};

}

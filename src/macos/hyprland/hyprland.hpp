#pragma once

#include "../background_reader.hpp"
#include <memory>

#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/model.hpp"
#include "../../core/qmlscreen.hpp"
#include "spaces_backend.hpp"

namespace qs::hyprland::ipc {

class HyprlandWorkspace;
class HyprlandToplevel;

class HyprlandIpcEvent: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString name READ name CONSTANT);
	Q_PROPERTY(QString data READ data CONSTANT);
	QML_NAMED_ELEMENT(HyprlandEvent);
	QML_UNCREATABLE("HyprlandIpcEvents cannot be created.");

public:
	HyprlandIpcEvent(QString name, QString data, QObject* parent);

	[[nodiscard]] QString name() const { return this->mName; }
	[[nodiscard]] QString data() const { return this->mData; }

	Q_INVOKABLE [[nodiscard]] QVector<QString> parse(qint32 argumentCount) const;

private:
	QString mName;
	QString mData;
};

class HyprlandMonitor: public QObject {
	Q_OBJECT;
	Q_PROPERTY(qint32 id READ default NOTIFY idChanged BINDABLE bindableId);
	Q_PROPERTY(QString name READ default NOTIFY nameChanged BINDABLE bindableName);
	Q_PROPERTY(QString description READ default NOTIFY descriptionChanged BINDABLE bindableDescription);
	Q_PROPERTY(qint32 x READ default NOTIFY xChanged BINDABLE bindableX);
	Q_PROPERTY(qint32 y READ default NOTIFY yChanged BINDABLE bindableY);
	Q_PROPERTY(qint32 width READ default NOTIFY widthChanged BINDABLE bindableWidth);
	Q_PROPERTY(qint32 height READ default NOTIFY heightChanged BINDABLE bindableHeight);
	Q_PROPERTY(qreal scale READ default NOTIFY scaleChanged BINDABLE bindableScale);
	Q_PROPERTY(QVariantMap lastIpcObject READ lastIpcObject NOTIFY lastIpcObjectChanged);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandWorkspace* activeWorkspace READ default NOTIFY activeWorkspaceChanged BINDABLE bindableActiveWorkspace);
	Q_PROPERTY(bool focused READ default NOTIFY focusedChanged BINDABLE bindableFocused);
	QML_ELEMENT;
	QML_UNCREATABLE("HyprlandMonitors must be retrieved from the HyprlandIpc object.");

public:
	explicit HyprlandMonitor(QObject* parent): QObject(parent) {}

	void update(qint32 id, const macos::ScreenDescription& screen);

	[[nodiscard]] QString displayIdentifier() const { return this->mDisplayIdentifier; }
	[[nodiscard]] QVariantMap lastIpcObject() const { return this->mLastIpcObject; }
	void setActiveWorkspace(HyprlandWorkspace* workspace);
	void setFocused(bool focused) { this->bFocused = focused; }

	[[nodiscard]] QBindable<qint32> bindableId() { return &this->bId; }
	[[nodiscard]] QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QBindable<QString> bindableDescription() { return &this->bDescription; }
	[[nodiscard]] QBindable<qint32> bindableX() { return &this->bX; }
	[[nodiscard]] QBindable<qint32> bindableY() { return &this->bY; }
	[[nodiscard]] QBindable<qint32> bindableWidth() { return &this->bWidth; }
	[[nodiscard]] QBindable<qint32> bindableHeight() { return &this->bHeight; }
	[[nodiscard]] QBindable<qreal> bindableScale() { return &this->bScale; }
	[[nodiscard]] QBindable<HyprlandWorkspace*> bindableActiveWorkspace() {
		return &this->bActiveWorkspace;
	}
	[[nodiscard]] QBindable<bool> bindableFocused() { return &this->bFocused; }

signals:
	void idChanged();
	void nameChanged();
	void descriptionChanged();
	void xChanged();
	void yChanged();
	void widthChanged();
	void heightChanged();
	void scaleChanged();
	void lastIpcObjectChanged();
	void activeWorkspaceChanged();
	void focusedChanged();

private:
	void rebuildIpcObject();

	QString mDisplayIdentifier;
	QVariantMap mLastIpcObject;

	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, qint32, bId, &HyprlandMonitor::idChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, QString, bName, &HyprlandMonitor::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, QString, bDescription, &HyprlandMonitor::descriptionChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, qint32, bX, &HyprlandMonitor::xChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, qint32, bY, &HyprlandMonitor::yChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, qint32, bWidth, &HyprlandMonitor::widthChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, qint32, bHeight, &HyprlandMonitor::heightChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, qreal, bScale, &HyprlandMonitor::scaleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, HyprlandWorkspace*, bActiveWorkspace, &HyprlandMonitor::activeWorkspaceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandMonitor, bool, bFocused, &HyprlandMonitor::focusedChanged);
};

class HyprlandWorkspace: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString address READ default NOTIFY addressChanged BINDABLE bindableAddress);
	Q_PROPERTY(qint32 id READ default NOTIFY idChanged BINDABLE bindableId);
	Q_PROPERTY(QString name READ default NOTIFY nameChanged BINDABLE bindableName);
	Q_PROPERTY(bool active READ default NOTIFY activeChanged BINDABLE bindableActive);
	Q_PROPERTY(bool focused READ default NOTIFY focusedChanged BINDABLE bindableFocused);
	Q_PROPERTY(bool urgent READ default NOTIFY urgentChanged BINDABLE bindableUrgent);
	Q_PROPERTY(bool hasFullscreen READ default NOTIFY hasFullscreenChanged BINDABLE bindableHasFullscreen);
	Q_PROPERTY(QVariantMap lastIpcObject READ lastIpcObject NOTIFY lastIpcObjectChanged);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandMonitor* monitor READ default NOTIFY monitorChanged BINDABLE bindableMonitor);
	Q_PROPERTY(UntypedObjectModel* toplevels READ toplevels CONSTANT);
	QML_ELEMENT;
	QML_UNCREATABLE("HyprlandWorkspaces must be retrieved from the HyprlandIpc object.");

public:
	explicit HyprlandWorkspace(QObject* parent): QObject(parent) {}

	void update(qint32 id, quint64 spaceId, int positionOnDisplay, HyprlandMonitor* monitor);
	void setActive(bool active) { this->bActive = active; }
	void setFocused(bool focused) { this->bFocused = focused; }
	void rebuildIpcObject(int windowCount);

	[[nodiscard]] quint64 spaceId() const { return this->mSpaceId; }
	[[nodiscard]] int positionOnDisplay() const { return this->mPositionOnDisplay; }
	[[nodiscard]] QVariantMap lastIpcObject() const { return this->mLastIpcObject; }
	[[nodiscard]] ObjectModel<HyprlandToplevel>* toplevels() { return &this->mToplevels; }

	Q_INVOKABLE void activate();

	[[nodiscard]] QBindable<QString> bindableAddress() { return &this->bAddress; }
	[[nodiscard]] QBindable<qint32> bindableId() { return &this->bId; }
	[[nodiscard]] QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QBindable<bool> bindableActive() { return &this->bActive; }
	[[nodiscard]] QBindable<bool> bindableFocused() { return &this->bFocused; }
	[[nodiscard]] QBindable<bool> bindableUrgent() { return &this->bUrgent; }
	[[nodiscard]] QBindable<bool> bindableHasFullscreen() { return &this->bHasFullscreen; }
	[[nodiscard]] QBindable<HyprlandMonitor*> bindableMonitor() { return &this->bMonitor; }

signals:
	void addressChanged();
	void idChanged();
	void nameChanged();
	void activeChanged();
	void focusedChanged();
	void urgentChanged();
	void hasFullscreenChanged();
	void lastIpcObjectChanged();
	void monitorChanged();

private:
	quint64 mSpaceId = 0;
	int mPositionOnDisplay = 0;
	QVariantMap mLastIpcObject;
	ObjectModel<HyprlandToplevel> mToplevels {this};

	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, QString, bAddress, &HyprlandWorkspace::addressChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, qint32, bId, &HyprlandWorkspace::idChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, QString, bName, &HyprlandWorkspace::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, bool, bActive, &HyprlandWorkspace::activeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, bool, bFocused, &HyprlandWorkspace::focusedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, bool, bUrgent, &HyprlandWorkspace::urgentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, bool, bHasFullscreen, &HyprlandWorkspace::hasFullscreenChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, HyprlandMonitor*, bMonitor, &HyprlandWorkspace::monitorChanged);
};

class HyprlandToplevel: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString address READ address NOTIFY addressChanged);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandToplevel* handle READ handle CONSTANT);
	Q_PROPERTY(QString title READ default NOTIFY titleChanged BINDABLE bindableTitle);
	Q_PROPERTY(bool activated READ default NOTIFY activatedChanged BINDABLE bindableActivated);
	Q_PROPERTY(bool urgent READ default NOTIFY urgentChanged BINDABLE bindableUrgent);
	Q_PROPERTY(QVariantMap lastIpcObject READ default NOTIFY lastIpcObjectChanged BINDABLE bindableLastIpcObject);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandWorkspace* workspace READ default NOTIFY workspaceChanged BINDABLE bindableWorkspace);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandMonitor* monitor READ default NOTIFY monitorChanged BINDABLE bindableMonitor);
	QML_ELEMENT;
	QML_UNCREATABLE("HyprlandToplevels must be retrieved from the HyprlandIpc object.");
	QML_ATTACHED(HyprlandToplevel);

public:
	HyprlandToplevel(quint32 windowId, QObject* parent);

	static HyprlandToplevel* qmlAttachedProperties(QObject* object);

	void update(const macos::WindowDescription& window, HyprlandWorkspace* workspace, bool activated);

	[[nodiscard]] quint32 windowId() const { return this->mWindowId; }
	[[nodiscard]] QString address() const;
	[[nodiscard]] HyprlandToplevel* handle() { return this; }

	[[nodiscard]] QBindable<QString> bindableTitle() { return &this->bTitle; }
	[[nodiscard]] QBindable<bool> bindableActivated() { return &this->bActivated; }
	[[nodiscard]] QBindable<bool> bindableUrgent() { return &this->bUrgent; }
	[[nodiscard]] QBindable<QVariantMap> bindableLastIpcObject() { return &this->bLastIpcObject; }
	[[nodiscard]] QBindable<HyprlandWorkspace*> bindableWorkspace() { return &this->bWorkspace; }
	[[nodiscard]] QBindable<HyprlandMonitor*> bindableMonitor() { return &this->bMonitor; }

signals:
	void addressChanged();
	void titleChanged();
	void activatedChanged();
	void urgentChanged();
	void lastIpcObjectChanged();
	void workspaceChanged();
	void monitorChanged();

private:
	quint32 mWindowId;

	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, QString, bTitle, &HyprlandToplevel::titleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, bool, bActivated, &HyprlandToplevel::activatedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, bool, bUrgent, &HyprlandToplevel::urgentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, QVariantMap, bLastIpcObject, &HyprlandToplevel::lastIpcObjectChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, HyprlandWorkspace*, bWorkspace, &HyprlandToplevel::workspaceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, HyprlandMonitor*, bMonitor, &HyprlandToplevel::monitorChanged);
};

class HyprlandState: public QObject {
	Q_OBJECT;

public:
	static HyprlandState* instance();

	void refreshMonitors();
	void refreshWorkspaces();
	void refreshToplevels();
	void applyToplevels(const QList<macos::WindowDescription>& windows, quint32 frontWindowId);
	void dispatch(const QString& request);
	void activateWorkspace(HyprlandWorkspace* workspace);

	[[nodiscard]] ObjectModel<HyprlandMonitor>* monitors() { return &this->mMonitors; }
	[[nodiscard]] ObjectModel<HyprlandWorkspace>* workspaces() { return &this->mWorkspaces; }
	[[nodiscard]] ObjectModel<HyprlandToplevel>* toplevels() { return &this->mToplevels; }
	[[nodiscard]] HyprlandMonitor* monitorForName(const QString& name) const;
	[[nodiscard]] HyprlandToplevel* toplevelForWindow(quint32 windowId) const {
		return this->mToplevelsByWindow.value(windowId);
	}

	[[nodiscard]] QBindable<HyprlandMonitor*> bindableFocusedMonitor() { return &this->bFocusedMonitor; }
	[[nodiscard]] QBindable<HyprlandWorkspace*> bindableFocusedWorkspace() {
		return &this->bFocusedWorkspace;
	}
	[[nodiscard]] QBindable<HyprlandToplevel*> bindableActiveToplevel() { return &this->bActiveToplevel; }

signals:
	void rawEvent(qs::hyprland::ipc::HyprlandIpcEvent* event);
	void focusedMonitorChanged();
	void focusedWorkspaceChanged();
	void activeToplevelChanged();

private:
	HyprlandState();

	void refreshEverything();
	void emitRawEvent(const QString& name, const QString& data);
	void switchWorkspaceRelative(int offset);
	[[nodiscard]] HyprlandWorkspace* workspaceForSpace(quint64 spaceId) const;

	ObjectModel<HyprlandMonitor> mMonitors {this};
	ObjectModel<HyprlandWorkspace> mWorkspaces {this};
	ObjectModel<HyprlandToplevel> mToplevels {this};
	QHash<QString, HyprlandMonitor*> mMonitorsByDisplay;
	QHash<qint32, HyprlandWorkspace*> mWorkspacesById;
	QHash<quint32, HyprlandToplevel*> mToplevelsByWindow;
	QTimer mToplevelRefreshTimer;
	qs::macos::CoalescingBackgroundReader mToplevelReader {"MacShell.HyprlandToplevels"};
	std::unique_ptr<macos::SystemEventObserver> mSystemEvents;

	Q_OBJECT_BINDABLE_PROPERTY(HyprlandState, HyprlandMonitor*, bFocusedMonitor, &HyprlandState::focusedMonitorChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandState, HyprlandWorkspace*, bFocusedWorkspace, &HyprlandState::focusedWorkspaceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandState, HyprlandToplevel*, bActiveToplevel, &HyprlandState::activeToplevelChanged);
};

class HyprlandIpcQml: public QObject {
	Q_OBJECT;
	Q_PROPERTY(bool usingLua READ usingLua CONSTANT);
	Q_PROPERTY(QString requestSocketPath READ requestSocketPath CONSTANT);
	Q_PROPERTY(QString eventSocketPath READ eventSocketPath CONSTANT);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandMonitor* focusedMonitor READ default NOTIFY focusedMonitorChanged BINDABLE bindableFocusedMonitor);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandWorkspace* focusedWorkspace READ default NOTIFY focusedWorkspaceChanged BINDABLE bindableFocusedWorkspace);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandToplevel* activeToplevel READ default NOTIFY activeToplevelChanged BINDABLE bindableActiveToplevel);
	Q_PROPERTY(UntypedObjectModel* monitors READ monitors CONSTANT);
	Q_PROPERTY(UntypedObjectModel* workspaces READ workspaces CONSTANT);
	Q_PROPERTY(UntypedObjectModel* toplevels READ toplevels CONSTANT);
	QML_NAMED_ELEMENT(Hyprland);
	QML_SINGLETON;

public:
	explicit HyprlandIpcQml(QObject* parent = nullptr);

	Q_INVOKABLE static void dispatch(const QString& request);
	Q_INVOKABLE static qs::hyprland::ipc::HyprlandMonitor* monitorFor(QuickshellScreenInfo* screen);
	Q_INVOKABLE static void refreshMonitors();
	Q_INVOKABLE static void refreshWorkspaces();
	Q_INVOKABLE static void refreshToplevels();

	[[nodiscard]] static bool usingLua() { return false; }
	[[nodiscard]] static QString requestSocketPath() { return {}; }
	[[nodiscard]] static QString eventSocketPath() { return {}; }
	[[nodiscard]] static ObjectModel<HyprlandMonitor>* monitors();
	[[nodiscard]] static ObjectModel<HyprlandWorkspace>* workspaces();
	[[nodiscard]] static ObjectModel<HyprlandToplevel>* toplevels();
	[[nodiscard]] static QBindable<HyprlandMonitor*> bindableFocusedMonitor();
	[[nodiscard]] static QBindable<HyprlandWorkspace*> bindableFocusedWorkspace();
	[[nodiscard]] static QBindable<HyprlandToplevel*> bindableActiveToplevel();

signals:
	void rawEvent(qs::hyprland::ipc::HyprlandIpcEvent* event);
	void focusedMonitorChanged();
	void focusedWorkspaceChanged();
	void activeToplevelChanged();
};

}

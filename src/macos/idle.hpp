#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::wayland::idle_inhibit {

class IdleInhibitor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(bool enabled READ default WRITE default NOTIFY enabledChanged BINDABLE bindableEnabled);
	Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged);

public:
	explicit IdleInhibitor(QObject* parent = nullptr);
	~IdleInhibitor() override;
	Q_DISABLE_COPY_MOVE(IdleInhibitor);

	[[nodiscard]] QObject* window() const { return this->mWindow; }
	void setWindow(QObject* window);

	[[nodiscard]] QBindable<bool> bindableEnabled() { return &this->bEnabled; }

signals:
	void enabledChanged();
	void windowChanged();

private:
	void updateAssertion();

	QPointer<QObject> mWindow;
	quint32 assertionId = 0;

	Q_OBJECT_BINDABLE_PROPERTY(IdleInhibitor, bool, bEnabled, &IdleInhibitor::enabledChanged);
};

}

namespace qs::wayland::idle_notify {

class IdleMonitor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(qreal timeout READ default WRITE default NOTIFY timeoutChanged BINDABLE bindableTimeout);
	Q_PROPERTY(bool respectInhibitors READ default WRITE default NOTIFY respectInhibitorsChanged BINDABLE bindableRespectInhibitors);
	Q_PROPERTY(bool isIdle READ default NOTIFY isIdleChanged BINDABLE bindableIsIdle);

public:
	explicit IdleMonitor(QObject* parent = nullptr);

	[[nodiscard]] bool isEnabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);

	[[nodiscard]] QBindable<qreal> bindableTimeout() { return &this->bTimeout; }
	[[nodiscard]] QBindable<bool> bindableRespectInhibitors() { return &this->bRespectInhibitors; }
	[[nodiscard]] QBindable<bool> bindableIsIdle() { return &this->bIsIdle; }

signals:
	void enabledChanged();
	void timeoutChanged();
	void respectInhibitorsChanged();
	void isIdleChanged();

private:
	void evaluateIdleState();

	bool mEnabled = true;
	QTimer mPollTimer;

	Q_OBJECT_BINDABLE_PROPERTY(IdleMonitor, qreal, bTimeout, &IdleMonitor::timeoutChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(IdleMonitor, bool, bRespectInhibitors, true, &IdleMonitor::respectInhibitorsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(IdleMonitor, bool, bIsIdle, &IdleMonitor::isIdleChanged);
};

}

#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qwindow.h>

namespace qs::wayland::shortcuts_inhibit {

class ShortcutInhibitor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(bool enabled READ default WRITE default NOTIFY enabledChanged BINDABLE bindableEnabled);
	Q_PROPERTY(QObject* window READ default WRITE default NOTIFY windowChanged BINDABLE bindableWindow);
	Q_PROPERTY(bool active READ default NOTIFY activeChanged BINDABLE bindableActive);

public:
	explicit ShortcutInhibitor(QObject* parent = nullptr);

	[[nodiscard]] QBindable<bool> bindableEnabled() { return &this->bEnabled; }
	[[nodiscard]] QBindable<QObject*> bindableWindow() { return &this->bWindow; }
	[[nodiscard]] QBindable<bool> bindableActive() { return &this->bActive; }

signals:
	void enabledChanged();
	void windowChanged();
	void activeChanged();
	void cancelled();

private:
	void updateActive();
	void trackWindow();

	QPointer<QWindow> mTrackedWindow;

	Q_OBJECT_BINDABLE_PROPERTY(ShortcutInhibitor, bool, bEnabled, &ShortcutInhibitor::enabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(ShortcutInhibitor, QObject*, bWindow, &ShortcutInhibitor::windowChanged);
	Q_OBJECT_BINDABLE_PROPERTY(ShortcutInhibitor, bool, bActive, &ShortcutInhibitor::activeChanged);
};

}

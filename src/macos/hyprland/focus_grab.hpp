#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qwindow.h>

namespace qs::hyprland {

class HyprlandFocusGrab: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged);
	Q_PROPERTY(QList<QObject*> windows READ windows WRITE setWindows NOTIFY windowsChanged);

public:
	explicit HyprlandFocusGrab(QObject* parent = nullptr): QObject(parent) {}
	~HyprlandFocusGrab() override;
	Q_DISABLE_COPY_MOVE(HyprlandFocusGrab);

	[[nodiscard]] bool isActive() const { return this->mActive; }
	void setActive(bool active);

	[[nodiscard]] QList<QObject*> windows() const { return this->mWindows; }
	void setWindows(const QList<QObject*>& windows);

	[[nodiscard]] bool containsNativeWindow(qintptr windowNumber) const;

signals:
	void activeChanged();
	void windowsChanged();
	void cleared();

private:
	void startMonitoring();
	void stopMonitoring();
	void clearGrab();

	[[nodiscard]] QList<QWindow*> grabbedWindows() const;

	bool mActive = false;
	QList<QObject*> mWindows;
	void* globalMonitor = nullptr;
	void* localMonitor = nullptr;
};

}

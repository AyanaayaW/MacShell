#pragma once

#include <qcolor.h>
#include <qmap.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlcomponent.h>
#include <qqmlintegration.h>
#include <qqmllist.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qscreen.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/qmlscreen.hpp"
#include "../core/reload.hpp"

class WlSessionLockSurface;

class WlSessionLock: public Reloadable {
	Q_OBJECT;
	Q_PROPERTY(bool locked READ isLocked WRITE setLocked NOTIFY lockStateChanged);
	Q_PROPERTY(bool secure READ isSecure NOTIFY secureStateChanged);
	Q_PROPERTY(QQmlComponent* surface READ surfaceComponent WRITE setSurfaceComponent NOTIFY surfaceComponentChanged);
	QML_ELEMENT;
	Q_CLASSINFO("DefaultProperty", "surface");

public:
	explicit WlSessionLock(QObject* parent = nullptr): Reloadable(parent) {}
	~WlSessionLock() override;
	Q_DISABLE_COPY_MOVE(WlSessionLock);

	void onReload(QObject* oldInstance) override;

	[[nodiscard]] bool isLocked() const { return this->mLocked; }
	void setLocked(bool locked);

	[[nodiscard]] static bool isSecure();

	[[nodiscard]] QQmlComponent* surfaceComponent() const { return this->mSurfaceComponent; }
	void setSurfaceComponent(QQmlComponent* surfaceComponent);

signals:
	void lockStateChanged();
	void secureStateChanged();
	void surfaceComponentChanged();

private slots:
	void onScreensChanged();

private:
	void lock(WlSessionLock* oldInstance = nullptr);
	void unlock();
	bool updateSurfaces(WlSessionLock* oldInstance = nullptr);

	QQmlComponent* mSurfaceComponent = nullptr;
	QMap<QScreen*, WlSessionLockSurface*> surfaces;
	bool mLocked = false;
};

class WlSessionLockSurface: public Reloadable {
	Q_OBJECT;
	Q_PROPERTY(QQuickItem* contentItem READ contentItem);
	Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged);
	Q_PROPERTY(qint32 width READ width NOTIFY widthChanged);
	Q_PROPERTY(qint32 height READ height NOTIFY heightChanged);
	Q_PROPERTY(QuickshellScreenInfo* screen READ screen NOTIFY screenChanged);
	Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged);
	Q_PROPERTY(QQmlListProperty<QObject> data READ data);
	QML_NAMED_ELEMENT(WlSessionLockSurface);
	Q_CLASSINFO("DefaultProperty", "data");

public:
	explicit WlSessionLockSurface(QObject* parent = nullptr);
	~WlSessionLockSurface() override;
	Q_DISABLE_COPY_MOVE(WlSessionLockSurface);

	void onReload(QObject* oldInstance) override;
	QQuickWindow* disownWindow();

	void show();

	[[nodiscard]] QQuickItem* contentItem() const { return this->mContentItem; }
	[[nodiscard]] bool isVisible() const;
	[[nodiscard]] qint32 width() const;
	[[nodiscard]] qint32 height() const;
	[[nodiscard]] QuickshellScreenInfo* screen() const;
	void setScreen(QScreen* screen);
	[[nodiscard]] QColor color() const;
	void setColor(QColor color);
	[[nodiscard]] QQmlListProperty<QObject> data();

signals:
	void visibleChanged();
	void widthChanged();
	void heightChanged();
	void screenChanged();
	void colorChanged();

private:
	void fitToScreen();

	QQuickWindow* window = nullptr;
	QQuickItem* mContentItem;
	QPointer<QScreen> mScreen;
	QColor mColor = Qt::black;
};

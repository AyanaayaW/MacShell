#include "session_lock.hpp"

#include <qguiapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qscreen.h>

#include "../core/qmlglobal.hpp"
#include "../window/proxywindow.hpp"
#include "cocoa.hpp"

namespace {

Q_LOGGING_CATEGORY(logSessionLock, "quickshell.wayland.sessionlock.macos", QtWarningMsg);

}

WlSessionLock::~WlSessionLock() {
	if (this->mLocked) {
		qCCritical(logSessionLock) << "Session lock object was destroyed without unlocking. The lock stays "
		                              "active until MacShell exits.";
	}
}

void WlSessionLock::onReload(QObject* oldInstance) {
	auto* guiApplication = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
	if (guiApplication) {
		QObject::connect(guiApplication, &QGuiApplication::screenAdded, this, &WlSessionLock::onScreensChanged);
		QObject::connect(guiApplication, &QGuiApplication::screenRemoved, this, &WlSessionLock::onScreensChanged);
	}

	auto* old = qobject_cast<WlSessionLock*>(oldInstance);
	if (old && old->mLocked) {
		this->mLocked = true;
		this->lock(old);
		old->surfaces.clear();
		old->mLocked = false;
	} else if (this->mLocked) {
		this->lock();
	}
}

bool WlSessionLock::isSecure() { return qs::macos::isLockdownActive(); }

void WlSessionLock::setLocked(bool locked) {
	if (locked == this->mLocked) return;

	if (!this->reloadComplete) {
		this->mLocked = locked;
		return;
	}

	if (locked) {
		this->mLocked = true;
		this->lock();
	} else {
		this->unlock();
	}
}

void WlSessionLock::lock(WlSessionLock* oldInstance) {
	if (this->mSurfaceComponent == nullptr) {
		qCWarning(logSessionLock) << "WlSessionLock.surface is null. Aborting lock.";
		this->unlock();
		return;
	}

	qs::macos::beginLockdown();

	if (!this->updateSurfaces(oldInstance)) return;
	for (auto* surface: this->surfaces) surface->show();

	emit this->lockStateChanged();
	emit this->secureStateChanged();
}

void WlSessionLock::unlock() {
	for (auto* surface: this->surfaces) surface->deleteLater();
	this->surfaces.clear();

	auto wasLocked = this->mLocked;
	this->mLocked = false;
	qs::macos::endLockdown();

	if (wasLocked) {
		emit this->lockStateChanged();
		emit this->secureStateChanged();
	}
}

bool WlSessionLock::updateSurfaces(WlSessionLock* oldInstance) {
	auto screens = QGuiApplication::screens();

	for (auto iterator = this->surfaces.begin(); iterator != this->surfaces.end();) {
		if (screens.contains(iterator.key())) {
			++iterator;
			continue;
		}
		iterator.value()->deleteLater();
		iterator = this->surfaces.erase(iterator);
	}

	for (auto* screen: screens) {
		if (this->surfaces.contains(screen)) continue;

		auto* createdObject = this->mSurfaceComponent->create(QQmlEngine::contextForObject(this->mSurfaceComponent));
		auto* surface = qobject_cast<WlSessionLockSurface*>(createdObject);
		if (surface == nullptr) {
			qCWarning(logSessionLock) << "WlSessionLock.surface does not create a WlSessionLockSurface. Aborting lock.";
			if (createdObject != nullptr) createdObject->deleteLater();
			this->unlock();
			return false;
		}

		surface->setParent(this);
		surface->setScreen(screen);
		auto* oldSurface = oldInstance == nullptr ? nullptr : oldInstance->surfaces.value(screen, nullptr);
		surface->reload(oldSurface);
		this->surfaces.insert(screen, surface);
	}

	return true;
}

void WlSessionLock::onScreensChanged() {
	if (!this->mLocked) return;
	if (!this->updateSurfaces()) return;
	for (auto* surface: this->surfaces) surface->show();
}

void WlSessionLock::setSurfaceComponent(QQmlComponent* surfaceComponent) {
	if (this->mLocked) {
		qCCritical(logSessionLock) << "WlSessionLock.surface cannot be changed while the lock is active.";
		return;
	}

	if (this->mSurfaceComponent != nullptr) this->mSurfaceComponent->deleteLater();
	if (surfaceComponent != nullptr) surfaceComponent->setParent(this);
	this->mSurfaceComponent = surfaceComponent;
	emit this->surfaceComponentChanged();
}

WlSessionLockSurface::WlSessionLockSurface(QObject* parent)
    : Reloadable(parent)
    , mContentItem(new QQuickItem()) {
	QQmlEngine::setObjectOwnership(this->mContentItem, QQmlEngine::CppOwnership);
	this->mContentItem->setParent(this);
}

WlSessionLockSurface::~WlSessionLockSurface() {
	if (this->window != nullptr) {
		this->window->destroy();
		this->window->deleteLater();
	}
}

void WlSessionLockSurface::onReload(QObject* oldInstance) {
	if (auto* old = qobject_cast<WlSessionLockSurface*>(oldInstance)) this->window = old->disownWindow();
	if (this->window == nullptr) this->window = new QsQuickWindowBase();

	this->window->setFlag(Qt::FramelessWindowHint);
	this->mContentItem->setParentItem(this->window->contentItem());
	if (this->mScreen) this->window->setScreen(this->mScreen);
	this->window->setColor(this->mColor);
	this->fitToScreen();

	QObject::connect(this->window, &QWindow::visibilityChanged, this, &WlSessionLockSurface::visibleChanged);
	QObject::connect(this->window, &QWindow::widthChanged, this, &WlSessionLockSurface::widthChanged);
	QObject::connect(this->window, &QWindow::heightChanged, this, &WlSessionLockSurface::heightChanged);
	QObject::connect(this->window, &QWindow::screenChanged, this, &WlSessionLockSurface::screenChanged);
	QObject::connect(this->window, &QQuickWindow::colorChanged, this, &WlSessionLockSurface::colorChanged);
	QObject::connect(this->window, &QWindow::widthChanged, this, [this]() {
		this->mContentItem->setWidth(this->width());
	});
	QObject::connect(this->window, &QWindow::heightChanged, this, [this]() {
		this->mContentItem->setHeight(this->height());
	});
}

QQuickWindow* WlSessionLockSurface::disownWindow() {
	QObject::disconnect(this->window, nullptr, this, nullptr);
	this->mContentItem->setParentItem(nullptr);
	auto* window = this->window;
	this->window = nullptr;
	return window;
}

void WlSessionLockSurface::fitToScreen() {
	if (this->window == nullptr || !this->mScreen) return;
	this->window->setGeometry(this->mScreen->geometry());
	this->mContentItem->setWidth(this->mScreen->geometry().width());
	this->mContentItem->setHeight(this->mScreen->geometry().height());
}

void WlSessionLockSurface::show() {
	if (this->window == nullptr) return;

	this->fitToScreen();
	this->window->show();
	qs::macos::configureLockWindow(this->window->winId());
	this->window->requestActivate();
}

bool WlSessionLockSurface::isVisible() const { return this->window != nullptr && this->window->isVisible(); }

qint32 WlSessionLockSurface::width() const { return this->window == nullptr ? 0 : this->window->width(); }

qint32 WlSessionLockSurface::height() const { return this->window == nullptr ? 0 : this->window->height(); }

QuickshellScreenInfo* WlSessionLockSurface::screen() const {
	QScreen* screen = this->window != nullptr ? this->window->screen() : this->mScreen.data();
	return QuickshellTracked::instance()->screenInfo(screen);
}

void WlSessionLockSurface::setScreen(QScreen* screen) {
	this->mScreen = screen;
	if (this->window != nullptr) {
		this->window->setScreen(screen);
		this->fitToScreen();
	}
	emit this->screenChanged();
}

QColor WlSessionLockSurface::color() const {
	return this->window == nullptr ? this->mColor : this->window->color();
}

void WlSessionLockSurface::setColor(QColor color) {
	if (this->window == nullptr) {
		this->mColor = color;
		emit this->colorChanged();
	} else {
		this->window->setColor(color);
	}
}

QQmlListProperty<QObject> WlSessionLockSurface::data() {
	return this->mContentItem->property("data").value<QQmlListProperty<QObject>>();
}

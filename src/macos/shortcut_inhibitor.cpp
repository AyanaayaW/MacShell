#include "shortcut_inhibitor.hpp"

#include <qobject.h>
#include <qwindow.h>

#include "../window/proxywindow.hpp"

namespace qs::wayland::shortcuts_inhibit {

ShortcutInhibitor::ShortcutInhibitor(QObject* parent): QObject(parent) {
	QObject::connect(this, &ShortcutInhibitor::enabledChanged, this, &ShortcutInhibitor::updateActive);
	QObject::connect(this, &ShortcutInhibitor::windowChanged, this, &ShortcutInhibitor::trackWindow);
}

void ShortcutInhibitor::trackWindow() {
	if (this->mTrackedWindow) QObject::disconnect(this->mTrackedWindow, nullptr, this, nullptr);

	auto* proxyWindow = ProxyWindowBase::forObject(this->bWindow.value());
	this->mTrackedWindow = proxyWindow ? proxyWindow->backingWindow() : nullptr;

	if (this->mTrackedWindow) {
		QObject::connect(this->mTrackedWindow, &QWindow::activeChanged, this, &ShortcutInhibitor::updateActive);
	}

	this->updateActive();
}

void ShortcutInhibitor::updateActive() {
	auto wasActive = this->bActive.value();
	auto isActive = this->bEnabled.value() && this->mTrackedWindow && this->mTrackedWindow->isActive();
	this->bActive = isActive;

	if (wasActive && !isActive && this->bEnabled.value()) emit this->cancelled();
}

}

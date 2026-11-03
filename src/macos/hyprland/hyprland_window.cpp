#include "hyprland_window.hpp"
#include <algorithm>

#include <qobject.h>
#include <qquickwindow.h>
#include <qregion.h>

#include "../../core/region.hpp"
#include "../../window/proxywindow.hpp"

namespace qs::hyprland::surface {

HyprlandWindow::HyprlandWindow(ProxyWindowBase* window): QObject(window), mProxyWindow(window) {
	QObject::connect(window, &ProxyWindowBase::windowConnected, this, &HyprlandWindow::applyToWindow);
	QObject::connect(window, &ProxyWindowBase::polished, this, &HyprlandWindow::applyToWindow);
}

HyprlandWindow* HyprlandWindow::qmlAttachedProperties(QObject* object) {
	auto* proxyWindow = ProxyWindowBase::forObject(object);
	if (!proxyWindow) return nullptr;
	return new HyprlandWindow(proxyWindow);
}

void HyprlandWindow::setOpacity(qreal opacity) {
	opacity = std::clamp(opacity, 0.0, 1.0);
	if (qFuzzyCompare(opacity, this->mOpacity)) return;

	this->mOpacity = opacity;
	emit this->opacityChanged();
	this->applyToWindow();
}

void HyprlandWindow::setVisibleMask(PendingRegion* visibleMask) {
	if (visibleMask == this->mVisibleMask) return;

	if (this->mVisibleMask) QObject::disconnect(this->mVisibleMask, nullptr, this, nullptr);
	this->mVisibleMask = visibleMask;
	if (visibleMask) {
		QObject::connect(visibleMask, &PendingRegion::changed, this, &HyprlandWindow::applyToWindow);
	}

	emit this->visibleMaskChanged();
	this->applyToWindow();
}

void HyprlandWindow::applyToWindow() {
	if (!this->mProxyWindow) return;
	auto* window = this->mProxyWindow->backingWindow();
	if (!window) return;

	window->setOpacity(this->mOpacity);
	window->setMask(this->mVisibleMask ? this->mVisibleMask->build() : QRegion());
}

}

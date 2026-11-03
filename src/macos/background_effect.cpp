#include "background_effect.hpp"

#include <qimage.h>
#include <qobject.h>
#include <qpainter.h>
#include <qquickwindow.h>
#include <qregion.h>

#include "../core/region.hpp"
#include "../window/proxywindow.hpp"
#include "cocoa.hpp"

namespace qs::wayland::background_effect {

BackgroundEffect::BackgroundEffect(ProxyWindowBase* window): QObject(window), mProxyWindow(window) {
	QObject::connect(window, &ProxyWindowBase::windowConnected, this, &BackgroundEffect::applyToWindow);
	QObject::connect(window, &ProxyWindowBase::polished, this, &BackgroundEffect::applyToWindow);
}

BackgroundEffect* BackgroundEffect::qmlAttachedProperties(QObject* object) {
	auto* proxyWindow = ProxyWindowBase::forObject(object);
	if (!proxyWindow) return nullptr;
	return new BackgroundEffect(proxyWindow);
}

void BackgroundEffect::setBlurRegion(PendingRegion* blurRegion) {
	if (blurRegion == this->mBlurRegion) return;

	if (this->mBlurRegion) QObject::disconnect(this->mBlurRegion, nullptr, this, nullptr);
	this->mBlurRegion = blurRegion;
	if (blurRegion) QObject::connect(blurRegion, &PendingRegion::changed, this, &BackgroundEffect::applyToWindow);

	emit this->blurRegionChanged();
	this->applyToWindow();
}

void BackgroundEffect::applyToWindow() {
	if (!this->mProxyWindow) return;
	auto* window = this->mProxyWindow->backingWindow();
	if (!window || !window->handle()) return;

	if (!this->mBlurRegion) {
		qs::macos::removeBackgroundBlur(window->winId());
		return;
	}

	auto region = this->mBlurRegion->build();
	if (region.isEmpty()) {
		qs::macos::removeBackgroundBlur(window->winId());
		return;
	}

	QImage maskImage(window->size(), QImage::Format_ARGB32_Premultiplied);
	maskImage.fill(Qt::transparent);

	QPainter painter(&maskImage);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setClipRegion(region);
	painter.fillRect(maskImage.rect(), Qt::black);
	painter.end();

	qs::macos::setBackgroundBlur(window->winId(), maskImage);
}

}

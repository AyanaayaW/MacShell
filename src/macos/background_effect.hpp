#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../core/region.hpp"
#include "../window/proxywindow.hpp"

namespace qs::wayland::background_effect {

class BackgroundEffect: public QObject {
	Q_OBJECT;
	Q_PROPERTY(PendingRegion* blurRegion READ blurRegion WRITE setBlurRegion NOTIFY blurRegionChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("BackgroundEffect can only be used as an attached object.");
	QML_ATTACHED(BackgroundEffect);

public:
	explicit BackgroundEffect(ProxyWindowBase* window);

	static BackgroundEffect* qmlAttachedProperties(QObject* object);

	[[nodiscard]] PendingRegion* blurRegion() const { return this->mBlurRegion; }
	void setBlurRegion(PendingRegion* blurRegion);

signals:
	void blurRegionChanged();

private:
	void applyToWindow();

	QPointer<ProxyWindowBase> mProxyWindow;
	QPointer<PendingRegion> mBlurRegion;
};

}

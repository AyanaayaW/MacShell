#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/region.hpp"
#include "../../window/proxywindow.hpp"

namespace qs::hyprland::surface {

class HyprlandWindow: public QObject {
	Q_OBJECT;
	Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged);
	Q_PROPERTY(PendingRegion* visibleMask READ visibleMask WRITE setVisibleMask NOTIFY visibleMaskChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("HyprlandWindow can only be used as an attached object.");
	QML_ATTACHED(HyprlandWindow);

public:
	explicit HyprlandWindow(ProxyWindowBase* window);

	static HyprlandWindow* qmlAttachedProperties(QObject* object);

	[[nodiscard]] qreal opacity() const { return this->mOpacity; }
	void setOpacity(qreal opacity);

	[[nodiscard]] PendingRegion* visibleMask() const { return this->mVisibleMask; }
	void setVisibleMask(PendingRegion* visibleMask);

signals:
	void opacityChanged();
	void visibleMaskChanged();

private:
	void applyToWindow();

	QPointer<ProxyWindowBase> mProxyWindow;
	QPointer<PendingRegion> mVisibleMask;
	qreal mOpacity = 1;
};

}

#include "panel_window.hpp"

#include <qloggingcategory.h>
#include <map>

#include <qhash.h>
#include <qmargins.h>

#include <qevent.h>
#include <qlist.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qqmlengine.h>
#include <qquickwindow.h>
#include <qscreen.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/generation.hpp"
#include "../core/qmlscreen.hpp"
#include "../core/types.hpp"
#include "../window/panelinterface.hpp"
#include "../window/proxywindow.hpp"
#include "cocoa.hpp"
#include "exclusive_zones.hpp"

class MacPanelStack {
public:
	static MacPanelStack* instance() {
		static auto* stack = new MacPanelStack();
		return stack;
	}

	[[nodiscard]] const QList<MacPanelWindow*>& panels(MacPanelWindow* panel) {
		return this->mPanels[EngineGeneration::findObjectGeneration(panel)];
	}

	void addPanel(MacPanelWindow* panel) {
		panel->engineGeneration = EngineGeneration::findObjectGeneration(panel);
		auto& panels = this->mPanels[panel->engineGeneration];
		if (!panels.contains(panel)) panels.push_back(panel);
		this->publishReservations();
	}

	void publishReservations() {
		QHash<QScreen*, QMargins> insetsByScreen;

		for (const auto& [generation, panels]: this->mPanels) {
			for (auto* panel: panels) {
				if (!panel->mTrackedScreen || panel->bExclusionMode == ExclusionMode::Ignore) continue;

				auto zone = panel->bcExclusiveZone.value();
				if (zone <= 0) continue;

				auto& insets = insetsByScreen[panel->mTrackedScreen];
				switch (panel->bcExclusionEdge.value()) {
				case Qt::TopEdge: insets.setTop(insets.top() + zone); break;
				case Qt::BottomEdge: insets.setBottom(insets.bottom() + zone); break;
				case Qt::LeftEdge: insets.setLeft(insets.left() + zone); break;
				case Qt::RightEdge: insets.setRight(insets.right() + zone); break;
				default: break;
				}
			}
		}

		QList<qs::macos::ScreenReservation> reservations;
		for (const auto& [screen, insets]: insetsByScreen.asKeyValueRange()) {
			reservations.append({.screenGeometry = screen->geometry(), .reservedInsets = insets});
		}
		qs::macos::ExclusiveZoneEnforcer::instance()->setReservations(reservations);
	}

	void removePanel(MacPanelWindow* panel) {
		if (!panel->engineGeneration) return;

		auto& panels = this->mPanels[panel->engineGeneration];
		if (panels.removeOne(panel)) {
			if (panels.isEmpty()) this->mPanels.erase(panel->engineGeneration);

			for (auto* panel: this->mPanels[panel->engineGeneration]) {
				panel->updateDimensions();
			}
			this->publishReservations();
		}
	}

	void updateLowerDimensions(MacPanelWindow* exclude) {
		if (!exclude->engineGeneration) return;
		auto& panels = this->mPanels[exclude->engineGeneration];

		auto found = false;
		for (auto* panel: panels) {
			if (panel == exclude) found = true;
			else if (found) panel->updateDimensions(false);
		}
	}

private:
	std::map<EngineGeneration*, QList<MacPanelWindow*>> mPanels;
};

namespace {
Q_LOGGING_CATEGORY(logPanelGeometry, "quickshell.macos.panel.geometry", QtWarningMsg);
}

bool MacPanelEventFilter::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
		if (auto* window = qobject_cast<QWindow*>(watched)) {
			qCDebug(logPanelGeometry) << "window" << window << "now at" << window->geometry();
		}
		emit this->geometryMoved();
	}

	if (event->type() == QEvent::PlatformSurface) {
		auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);

		if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
			emit this->surfaceCreated();
		}
	} else if (event->type() == QEvent::Show) {
		emit this->surfaceCreated();
	}

	return this->QObject::eventFilter(watched, event);
}

MacPanelWindow::MacPanelWindow(QObject* parent): ProxyWindowBase(parent) {
	QObject::connect(
	    &this->eventFilter,
	    &MacPanelEventFilter::surfaceCreated,
	    this,
	    &MacPanelWindow::macInit
	);
	QObject::connect(
	    &this->eventFilter,
	    &MacPanelEventFilter::geometryMoved,
	    this,
	    &MacPanelWindow::restoreAnchoredGeometry,
	    Qt::QueuedConnection
	);

	this->bcExclusiveZone.setBinding([this]() -> qint32 {
		switch (this->bExclusionMode.value()) {
		case ExclusionMode::Ignore: return 0;
		case ExclusionMode::Normal: return this->bExclusiveZone;
		case ExclusionMode::Auto:
			auto edge = this->bcExclusionEdge.value();
			auto margins = this->bMargins.value();

			if (edge == Qt::TopEdge || edge == Qt::BottomEdge) {
				return this->bImplicitHeight + margins.top + margins.bottom;
			} else if (edge == Qt::LeftEdge || edge == Qt::RightEdge) {
				return this->bImplicitWidth + margins.left + margins.right;
			} else {
				return 0;
			}
		}

		return 0;
	});

	this->bcExclusionEdge.setBinding([this] { return this->bAnchors.value().exclusionEdge(); });
}

MacPanelWindow::~MacPanelWindow() { MacPanelStack::instance()->removePanel(this); }

void MacPanelWindow::connectWindow() {
	this->ProxyWindowBase::connectWindow();

	this->window->installEventFilter(&this->eventFilter);
	this->updateScreen();

	QObject::connect(
	    this->window,
	    &QQuickWindow::visibleChanged,
	    this,
	    &MacPanelWindow::updatePanelStack
	);

	this->window->setFlag(Qt::FramelessWindowHint);
	this->window->setFlag(Qt::Tool);
	this->updateLayer();
	this->updateKeyboardFocus();

	if (this->window->handle() != nullptr) {
		this->macInit();
		this->updatePanelStack();
	}
}

void MacPanelWindow::trySetWidth(qint32 implicitWidth) {
	if (!this->bAnchors.value().horizontalConstraint()) {
		this->ProxyWindowBase::trySetWidth(implicitWidth);
		this->updateDimensions();
	}
}

void MacPanelWindow::trySetHeight(qint32 implicitHeight) {
	if (!this->bAnchors.value().verticalConstraint()) {
		this->ProxyWindowBase::trySetHeight(implicitHeight);
		this->updateDimensions();
	}
}

void MacPanelWindow::setScreen(QuickshellScreenInfo* screen) {
	this->ProxyWindowBase::setScreen(screen);
	this->updateScreen();
}

void MacPanelWindow::macInit() {
	if (this->window == nullptr || this->window->handle() == nullptr) return;
	this->applyCocoa();
	this->updateDimensions();
}

void MacPanelWindow::applyCocoa() {
	if (this->window == nullptr || this->window->handle() == nullptr) return;
	qs::macos::configurePanel(this->window->winId(), static_cast<int>(this->bLayer.value()));
}

void MacPanelWindow::updateScreen() {
	auto* newScreen =
	    this->mScreen ? this->mScreen : (this->window ? this->window->screen() : nullptr);

	if (newScreen == this->mTrackedScreen) return;

	if (this->mTrackedScreen != nullptr) {
		QObject::disconnect(this->mTrackedScreen, nullptr, this, nullptr);
	}

	this->mTrackedScreen = newScreen;

	if (this->mTrackedScreen != nullptr) {
		QObject::connect(
		    this->mTrackedScreen,
		    &QScreen::geometryChanged,
		    this,
		    &MacPanelWindow::updateDimensionsSlot
		);
	}

	this->updateDimensions();
}

void MacPanelWindow::updateDimensionsSlot() { this->updateDimensions(); }

void MacPanelWindow::updateDimensions(bool propagate) {
	if (this->window == nullptr || this->window->handle() == nullptr
	    || this->mTrackedScreen == nullptr)
		return;

	auto screenGeometry = this->mTrackedScreen->geometry();

	if (this->bExclusionMode != ExclusionMode::Ignore) {
		for (auto* panel: MacPanelStack::instance()->panels(this)) {
			if (panel == this) break;
			if (panel->bLayer != this->bLayer) continue;
			if (panel->mTrackedScreen != this->mTrackedScreen) continue;

			auto edge = panel->bcExclusionEdge.value();
			auto exclusiveZone = panel->bcExclusiveZone.value();

			screenGeometry.adjust(
			    edge == Qt::LeftEdge ? exclusiveZone : 0,
			    edge == Qt::TopEdge ? exclusiveZone : 0,
			    edge == Qt::RightEdge ? -exclusiveZone : 0,
			    edge == Qt::BottomEdge ? -exclusiveZone : 0
			);
		}
	}

	auto geometry = QRect();
	auto anchors = this->bAnchors.value();
	auto margins = this->bMargins.value();

	if (anchors.horizontalConstraint()) {
		geometry.setX(screenGeometry.x() + margins.left);
		geometry.setWidth(screenGeometry.width() - margins.left - margins.right);
	} else {
		if (anchors.mLeft) {
			geometry.setX(screenGeometry.x() + margins.left);
		} else if (anchors.mRight) {
			geometry.setX(
			    screenGeometry.x() + screenGeometry.width() - this->implicitWidth() - margins.right
			);
		} else {
			geometry.setX(screenGeometry.x() + screenGeometry.width() / 2 - this->implicitWidth() / 2);
		}

		geometry.setWidth(this->implicitWidth());
	}

	if (anchors.verticalConstraint()) {
		geometry.setY(screenGeometry.y() + margins.top);
		geometry.setHeight(screenGeometry.height() - margins.top - margins.bottom);
	} else {
		if (anchors.mTop) {
			geometry.setY(screenGeometry.y() + margins.top);
		} else if (anchors.mBottom) {
			geometry.setY(
			    screenGeometry.y() + screenGeometry.height() - this->implicitHeight() - margins.bottom
			);
		} else {
			geometry.setY(
			    screenGeometry.y() + screenGeometry.height() / 2 - this->implicitHeight() / 2
			);
		}

		geometry.setHeight(this->implicitHeight());
	}

	qCDebug(logPanelGeometry) << "panel" << this->window << "set to" << geometry << "anchors top" << anchors.mTop
	                          << "bottom" << anchors.mBottom << "screen" << screenGeometry;
	this->mAnchoredGeometry = geometry;
	this->window->setMinimumSize(geometry.size());
	this->window->setMaximumSize(geometry.size());
	this->window->setGeometry(geometry);
	this->applyCocoa();
	this->updateStrut(propagate);
}

void MacPanelWindow::restoreAnchoredGeometry() {
	if (this->window == nullptr || !this->mAnchoredGeometry.isValid()) return;
	if (this->window->geometry() == this->mAnchoredGeometry) return;
	qCDebug(logPanelGeometry) << "panel" << this->window << "moved by the system to" << this->window->geometry()
	                          << "; restoring" << this->mAnchoredGeometry;
	this->window->setMinimumSize(this->mAnchoredGeometry.size());
	this->window->setMaximumSize(this->mAnchoredGeometry.size());
	this->window->setGeometry(this->mAnchoredGeometry);
	this->applyCocoa();
}

void MacPanelWindow::updatePanelStack() {
	if (this->window->isVisible()) {
		if (this->focusable()) {
			qs::macos::focusPanel(this->window->winId());
		}
		MacPanelStack::instance()->addPanel(this);
	} else {
		qs::macos::unfocusPanel(this->window->winId());
		MacPanelStack::instance()->removePanel(this);
	}
}

void MacPanelWindow::updateStrut(bool propagate) {
	if (propagate) MacPanelStack::instance()->updateLowerDimensions(this);
	MacPanelStack::instance()->publishReservations();
}

void MacPanelWindow::updateLayer() {
	emit this->aboveWindowsChanged();
	if (this->window == nullptr) return;
	this->window->setFlag(Qt::WindowStaysOnTopHint, this->aboveWindows());
	this->applyCocoa();
}

void MacPanelWindow::updateKeyboardFocus() {
	emit this->focusableChanged();
	if (this->window == nullptr) return;
	this->window->setFlag(Qt::WindowDoesNotAcceptFocus, !this->focusable());
	this->applyCocoa();

	if (!this->window->isVisible()) return;
	if (this->focusable()) {
		qs::macos::focusPanel(this->window->winId());
	} else {
		qs::macos::unfocusPanel(this->window->winId());
	}
}

void MacPanelWindow::setLayerNamespace(const QString& layerNamespace) {
	if (layerNamespace == this->mLayerNamespace) return;
	this->mLayerNamespace = layerNamespace;
	emit this->namespaceChanged();
}

MacPanelWindow* MacPanelWindow::qmlAttachedProperties(QObject* object) {
	if (auto* panelInterface = qobject_cast<MacPanelInterface*>(object)) return panelInterface->panel;
	return qobject_cast<MacPanelWindow*>(object);
}

MacPanelInterface::MacPanelInterface(QObject* parent)
    : PanelWindowInterface(parent)
    , panel(new MacPanelWindow(this)) {
	this->connectSignals();

	QObject::connect(this->panel, &MacPanelWindow::anchorsChanged, this, &MacPanelInterface::anchorsChanged);
	QObject::connect(this->panel, &MacPanelWindow::marginsChanged, this, &MacPanelInterface::marginsChanged);
	QObject::connect(this->panel, &MacPanelWindow::exclusiveZoneChanged, this, &MacPanelInterface::exclusiveZoneChanged);
	QObject::connect(this->panel, &MacPanelWindow::exclusionModeChanged, this, &MacPanelInterface::exclusionModeChanged);
	QObject::connect(this->panel, &MacPanelWindow::aboveWindowsChanged, this, &MacPanelInterface::aboveWindowsChanged);
	QObject::connect(this->panel, &MacPanelWindow::focusableChanged, this, &MacPanelInterface::focusableChanged);
}

void MacPanelInterface::onReload(QObject* oldInstance) {
	QQmlEngine::setContextForObject(this->panel, QQmlEngine::contextForObject(this));

	auto* old = qobject_cast<MacPanelInterface*>(oldInstance);
	this->panel->reload(old != nullptr ? old->panel : nullptr);
}

ProxyWindowBase* MacPanelInterface::proxyWindow() const { return this->panel; }

#define proxyPair(type, get, set)                                                                  \
	type MacPanelInterface::get() const { return this->panel->get(); }                               \
	void MacPanelInterface::set(type value) { this->panel->set(value); }

proxyPair(Anchors, anchors, setAnchors);
proxyPair(Margins, margins, setMargins);
proxyPair(qint32, exclusiveZone, setExclusiveZone);
proxyPair(ExclusionMode::Enum, exclusionMode, setExclusionMode);
proxyPair(bool, focusable, setFocusable);
proxyPair(bool, aboveWindows, setAboveWindows);

#undef proxyPair

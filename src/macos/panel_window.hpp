#pragma once

#include <qrect.h>

#include <qnamespace.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qquickwindow.h>
#include <qscreen.h>
#include <qtclasshelpermacros.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include "../core/doc.hpp"
#include "../core/util.hpp"
#include "../window/panelinterface.hpp"
#include "../window/proxywindow.hpp"

namespace qs::wayland::layershell {

class WlrLayer: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Background = 0,
		Bottom = 1,
		Top = 2,
		Overlay = 3,
	};
	Q_ENUM(Enum);
};

class WlrKeyboardFocus: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		Exclusive = 1,
		OnDemand = 2,
	};
	Q_ENUM(Enum);
};

}

class MacPanelStack;

class MacPanelEventFilter: public QObject {
	Q_OBJECT;

public:
	explicit MacPanelEventFilter(QObject* parent = nullptr): QObject(parent) {}

signals:
	void surfaceCreated();
	void geometryMoved();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
};

class MacPanelWindow: public ProxyWindowBase {
	QSDOC_BASECLASS(PanelWindowInterface);
	Q_OBJECT;
	QSDOC_HIDE Q_PROPERTY(Anchors anchors READ anchors WRITE setAnchors NOTIFY anchorsChanged);
	QSDOC_HIDE Q_PROPERTY(qint32 exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged);
	QSDOC_HIDE Q_PROPERTY(ExclusionMode::Enum exclusionMode READ exclusionMode WRITE setExclusionMode NOTIFY exclusionModeChanged);
	QSDOC_HIDE Q_PROPERTY(Margins margins READ margins WRITE setMargins NOTIFY marginsChanged);
	QSDOC_HIDE Q_PROPERTY(bool aboveWindows READ aboveWindows WRITE setAboveWindows NOTIFY aboveWindowsChanged);
	QSDOC_HIDE Q_PROPERTY(bool focusable READ focusable WRITE setFocusable NOTIFY focusableChanged);
	Q_PROPERTY(qs::wayland::layershell::WlrLayer::Enum layer READ layer WRITE setLayer NOTIFY layerChanged);
	Q_PROPERTY(QString namespace READ layerNamespace WRITE setLayerNamespace NOTIFY namespaceChanged);
	Q_PROPERTY(qs::wayland::layershell::WlrKeyboardFocus::Enum keyboardFocus READ keyboardFocus WRITE setKeyboardFocus NOTIFY keyboardFocusChanged);
	QML_NAMED_ELEMENT(WlrLayershell);
	QML_ATTACHED(MacPanelWindow);

public:
	explicit MacPanelWindow(QObject* parent = nullptr);
	~MacPanelWindow() override;
	Q_DISABLE_COPY_MOVE(MacPanelWindow);

	void connectWindow() override;

	void trySetWidth(qint32 implicitWidth) override;
	void trySetHeight(qint32 implicitHeight) override;

	void setScreen(QuickshellScreenInfo* screen) override;

	static MacPanelWindow* qmlAttachedProperties(QObject* object);

	[[nodiscard]] qs::wayland::layershell::WlrLayer::Enum layer() const { return this->bLayer; }
	void setLayer(qs::wayland::layershell::WlrLayer::Enum layer) { this->bLayer = layer; }

	[[nodiscard]] QString layerNamespace() const { return this->mLayerNamespace; }
	void setLayerNamespace(const QString& layerNamespace);

	[[nodiscard]] qs::wayland::layershell::WlrKeyboardFocus::Enum keyboardFocus() const {
		return this->bKeyboardFocus;
	}
	void setKeyboardFocus(qs::wayland::layershell::WlrKeyboardFocus::Enum keyboardFocus) {
		this->bKeyboardFocus = keyboardFocus;
	}

	[[nodiscard]] bool aboveWindows() const {
		return this->bLayer.value() >= qs::wayland::layershell::WlrLayer::Top;
	}
	void setAboveWindows(bool aboveWindows) {
		this->bLayer = aboveWindows ? qs::wayland::layershell::WlrLayer::Top
		                            : qs::wayland::layershell::WlrLayer::Bottom;
	}

	[[nodiscard]] Anchors anchors() const { return this->bAnchors; }
	void setAnchors(Anchors anchors) { this->bAnchors = anchors; }

	[[nodiscard]] qint32 exclusiveZone() const { return this->bExclusiveZone; }
	void setExclusiveZone(qint32 exclusiveZone) {
		Qt::beginPropertyUpdateGroup();
		this->bExclusiveZone = exclusiveZone;
		this->bExclusionMode = ExclusionMode::Normal;
		Qt::endPropertyUpdateGroup();
	}

	[[nodiscard]] ExclusionMode::Enum exclusionMode() const { return this->bExclusionMode; }
	void setExclusionMode(ExclusionMode::Enum exclusionMode) { this->bExclusionMode = exclusionMode; }

	[[nodiscard]] Margins margins() const { return this->bMargins; }
	void setMargins(Margins margins) { this->bMargins = margins; }

	[[nodiscard]] bool focusable() const {
		return this->bKeyboardFocus.value() != qs::wayland::layershell::WlrKeyboardFocus::None;
	}
	void setFocusable(bool focusable) {
		this->bKeyboardFocus = focusable ? qs::wayland::layershell::WlrKeyboardFocus::OnDemand
		                                 : qs::wayland::layershell::WlrKeyboardFocus::None;
	}

signals:
	QSDOC_HIDE void anchorsChanged();
	QSDOC_HIDE void exclusiveZoneChanged();
	QSDOC_HIDE void exclusionModeChanged();
	QSDOC_HIDE void marginsChanged();
	QSDOC_HIDE void aboveWindowsChanged();
	QSDOC_HIDE void focusableChanged();
	void layerChanged();
	void namespaceChanged();
	void keyboardFocusChanged();

private slots:
	void macInit();
	void updatePanelStack();
	void updateDimensionsSlot();
	void restoreAnchoredGeometry();

private:
	void updateScreen();
	void updateStrut(bool propagate = true);
	void updateStrutCb() { this->updateStrut(); }
	void updateLayer();
	void applyCocoa();
	void updateKeyboardFocus();
	void updateDimensions(bool propagate = true);
	void updateDimensionsCb() { this->updateDimensions(); }

	QPointer<QScreen> mTrackedScreen = nullptr;
	EngineGeneration* knownGeneration = nullptr;

	MacPanelEventFilter eventFilter;
	QString mLayerNamespace;
	QRect mAnchoredGeometry;

	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MacPanelWindow, qs::wayland::layershell::WlrLayer::Enum, bLayer, qs::wayland::layershell::WlrLayer::Top, &MacPanelWindow::layerChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MacPanelWindow, qs::wayland::layershell::WlrKeyboardFocus::Enum, bKeyboardFocus, qs::wayland::layershell::WlrKeyboardFocus::None, &MacPanelWindow::keyboardFocusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MacPanelWindow, Anchors, bAnchors, &MacPanelWindow::anchorsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MacPanelWindow, Margins, bMargins, &MacPanelWindow::marginsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MacPanelWindow, qint32, bExclusiveZone, &MacPanelWindow::exclusiveZoneChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MacPanelWindow, ExclusionMode::Enum, bExclusionMode, ExclusionMode::Auto, &MacPanelWindow::exclusionModeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MacPanelWindow, qint32, bcExclusiveZone);
	Q_OBJECT_BINDABLE_PROPERTY(MacPanelWindow, Qt::Edge, bcExclusionEdge);

	QS_BINDING_SUBSCRIBE_METHOD(MacPanelWindow, bLayer, updateLayer, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(MacPanelWindow, bAnchors, updateDimensionsCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(MacPanelWindow, bMargins, updateDimensionsCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(MacPanelWindow, bcExclusiveZone, updateStrutCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(MacPanelWindow, bKeyboardFocus, updateKeyboardFocus, onValueChanged);

	friend class MacPanelStack;
};

class MacPanelInterface: public PanelWindowInterface {
	Q_OBJECT;

public:
	explicit MacPanelInterface(QObject* parent = nullptr);

	void onReload(QObject* oldInstance) override;

	[[nodiscard]] ProxyWindowBase* proxyWindow() const override;

	[[nodiscard]] Anchors anchors() const override;
	void setAnchors(Anchors anchors) override;

	[[nodiscard]] Margins margins() const override;
	void setMargins(Margins margins) override;

	[[nodiscard]] qint32 exclusiveZone() const override;
	void setExclusiveZone(qint32 exclusiveZone) override;

	[[nodiscard]] ExclusionMode::Enum exclusionMode() const override;
	void setExclusionMode(ExclusionMode::Enum exclusionMode) override;

	[[nodiscard]] bool aboveWindows() const override;
	void setAboveWindows(bool aboveWindows) override;

	[[nodiscard]] bool focusable() const override;
	void setFocusable(bool focusable) override;

private:
	MacPanelWindow* panel;

	friend class MacPanelWindow;
};

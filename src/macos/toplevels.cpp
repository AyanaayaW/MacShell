#include "toplevels.hpp"
#include <cmath>

#include <qguiapplication.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qrect.h>
#include <qscreen.h>
#include <qset.h>

#include "../core/qmlglobal.hpp"
#include "../core/qmlscreen.hpp"
#include "exclusive_zones.hpp"
#include "toplevels_backend.hpp"

namespace qs::wayland::toplevel {

namespace {

Q_LOGGING_CATEGORY(logToplevel, "quickshell.wayland.toplevel.macos", QtWarningMsg);

constexpr auto POLL_INTERVAL_MILLISECONDS = 1000;
constexpr auto REFRESH_DEBOUNCE_MILLISECONDS = 200;
constexpr int MAXIMIZED_EDGE_TOLERANCE = 12;

bool isFrameMaximized(const QRect& frame) {
	auto visibleFrame = macos::visibleFrameForWindow(frame);
	return std::abs(frame.left() - visibleFrame.left()) <= MAXIMIZED_EDGE_TOLERANCE
	    && std::abs(frame.top() - visibleFrame.top()) <= MAXIMIZED_EDGE_TOLERANCE
	    && std::abs(frame.right() - visibleFrame.right()) <= MAXIMIZED_EDGE_TOLERANCE
	    && std::abs(frame.bottom() - visibleFrame.bottom()) <= MAXIMIZED_EDGE_TOLERANCE;
}

void warnIfNoAccessibility() {
	if (!macos::hasAccessibilityAccess()) {
		qCWarning(logToplevel) << "Window actions need Accessibility permission for MacShell.";
	}
}

}

Toplevel::Toplevel(quint32 windowId, qint32 processId, QObject* parent)
    : QObject(parent)
    , mWindowId(windowId)
    , mProcessId(processId) {}

void Toplevel::applyNativeWindow(const macos::NativeWindow& window, bool activated) {
	auto updateField = [this](auto& field, const auto& value, void (Toplevel::*signal)()) {
		if (field == value) return;
		field = value;
		emit (this->*signal)();
	};

	auto frameChanged = window.frame != this->mFrame;
	this->mFrame = window.frame;

	updateField(this->mAppId, window.appId, &Toplevel::appIdChanged);
	updateField(this->mTitle, window.title, &Toplevel::titleChanged);
	updateField(this->mActivated, activated, &Toplevel::activatedChanged);
	updateField(this->mMinimized, window.minimized, &Toplevel::minimizedChanged);
	updateField(this->mFullscreen, window.fullscreen, &Toplevel::fullscreenChanged);
	updateField(this->mMaximized, !window.fullscreen && isFrameMaximized(window.frame), &Toplevel::maximizedChanged);

	if (frameChanged) emit this->screensChanged();
}

QList<QuickshellScreenInfo*> Toplevel::screens() const {
	QList<QuickshellScreenInfo*> screenInfos;
	auto* tracked = QuickshellTracked::instance();

	for (auto* screen: QGuiApplication::screens()) {
		if (!screen->geometry().intersects(this->mFrame)) continue;
		if (auto* screenInfo = tracked->screenInfo(screen)) screenInfos.append(screenInfo);
	}

	return screenInfos;
}

void Toplevel::activate() {
	macos::activateWindow(this->mProcessId, this->mWindowId);
	ToplevelManager::instance()->scheduleRefresh();
}

void Toplevel::close() {
	warnIfNoAccessibility();
	macos::closeWindow(this->mProcessId, this->mWindowId);
	ToplevelManager::instance()->scheduleRefresh();
}

void Toplevel::setMinimized(bool minimized) {
	warnIfNoAccessibility();
	macos::setWindowMinimized(this->mProcessId, this->mWindowId, minimized);
	ToplevelManager::instance()->scheduleRefresh();
}

void Toplevel::setFullscreen(bool fullscreen) {
	warnIfNoAccessibility();
	macos::setWindowFullscreen(this->mProcessId, this->mWindowId, fullscreen);
	ToplevelManager::instance()->scheduleRefresh();
}

void Toplevel::fullscreenOn([[maybe_unused]] QuickshellScreenInfo* screen) { this->setFullscreen(true); }

void Toplevel::setMaximized(bool maximized) {
	warnIfNoAccessibility();

	if (maximized && !this->mMaximized) {
		this->mFrameBeforeMaximize = this->mFrame;
		auto targetFrame = macos::visibleFrameForWindow(this->mFrame);
		if (auto* screen = QGuiApplication::screenAt(this->mFrame.center())) {
			targetFrame = targetFrame.intersected(
			    macos::ExclusiveZoneEnforcer::instance()->usableGeometry(screen->geometry())
			);
		}
		macos::setWindowFrame(this->mProcessId, this->mWindowId, targetFrame);
	} else if (!maximized && this->mMaximized && this->mFrameBeforeMaximize) {
		macos::setWindowFrame(this->mProcessId, this->mWindowId, *this->mFrameBeforeMaximize);
		this->mFrameBeforeMaximize.reset();
	}

	ToplevelManager::instance()->scheduleRefresh();
}

void Toplevel::setRectangle([[maybe_unused]] QObject* window, [[maybe_unused]] QRect rect) {}

void Toplevel::unsetRectangle() {}

ToplevelManager* ToplevelManager::instance() {
	static auto* sharedInstance = new ToplevelManager();
	return sharedInstance;
}

ToplevelManager::ToplevelManager() {
	this->mRefreshDebounce.setSingleShot(true);
	this->mRefreshDebounce.setInterval(REFRESH_DEBOUNCE_MILLISECONDS);
	QObject::connect(&this->mRefreshDebounce, &QTimer::timeout, this, &ToplevelManager::refresh);

	QObject::connect(&this->mPollTimer, &QTimer::timeout, this, &ToplevelManager::refresh);
	this->mPollTimer.start(POLL_INTERVAL_MILLISECONDS);

	this->refresh();
}

void ToplevelManager::scheduleRefresh() { this->mRefreshDebounce.start(); }

void ToplevelManager::refresh() {
	auto windows = macos::readNativeWindows();
	auto focusedWindowId = macos::focusedWindowId();

	QSet<quint32> presentWindows;
	Toplevel* activeToplevel = nullptr;

	for (const auto& window: windows) {
		presentWindows.insert(window.windowId);

		auto* toplevel = this->mToplevelsByWindow.value(window.windowId);
		auto isNew = toplevel == nullptr;
		if (isNew) {
			toplevel = new Toplevel(window.windowId, window.processId, this);
			this->mToplevelsByWindow.insert(window.windowId, toplevel);
		}

		auto isActivated = window.windowId == focusedWindowId;
		toplevel->applyNativeWindow(window, isActivated);
		if (isActivated) activeToplevel = toplevel;
		if (isNew) this->mToplevels.insertObject(toplevel);
	}

	for (auto iterator = this->mToplevelsByWindow.begin(); iterator != this->mToplevelsByWindow.end();) {
		if (presentWindows.contains(iterator.key())) {
			++iterator;
			continue;
		}

		auto* closedToplevel = iterator.value();
		iterator = this->mToplevelsByWindow.erase(iterator);
		this->mToplevels.removeObject(closedToplevel);
		emit closedToplevel->closed();
		closedToplevel->deleteLater();
	}

	if (activeToplevel != this->mActiveToplevel) {
		this->mActiveToplevel = activeToplevel;
		emit this->activeToplevelChanged();
	}
}

ToplevelManagerQml::ToplevelManagerQml(QObject* parent): QObject(parent) {
	QObject::connect(
	    ToplevelManager::instance(),
	    &ToplevelManager::activeToplevelChanged,
	    this,
	    &ToplevelManagerQml::activeToplevelChanged
	);
}

}

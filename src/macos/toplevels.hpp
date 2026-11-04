#pragma once

#include <optional>

#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qrect.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/model.hpp"
#include "../core/qmlscreen.hpp"
#include "background_reader.hpp"
#include "toplevels_backend.hpp"

namespace qs::wayland::toplevel {

class Toplevel: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString appId READ appId NOTIFY appIdChanged);
	Q_PROPERTY(QString title READ title NOTIFY titleChanged);
	Q_PROPERTY(qs::wayland::toplevel::Toplevel* parent READ parent NOTIFY parentChanged);
	Q_PROPERTY(bool activated READ activated NOTIFY activatedChanged);
	Q_PROPERTY(QList<QuickshellScreenInfo*> screens READ screens NOTIFY screensChanged);
	Q_PROPERTY(bool maximized READ maximized WRITE setMaximized NOTIFY maximizedChanged);
	Q_PROPERTY(bool minimized READ minimized WRITE setMinimized NOTIFY minimizedChanged);
	Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("Toplevels must be acquired from the ToplevelManager.");

public:
	Toplevel(quint32 windowId, qint32 processId, QObject* parent);

	void applyNativeWindow(const macos::NativeWindow& window, bool activated);

	Q_INVOKABLE void activate();
	Q_INVOKABLE void close();
	Q_INVOKABLE void fullscreenOn(QuickshellScreenInfo* screen);
	Q_INVOKABLE static void setRectangle(QObject* window, QRect rect);
	Q_INVOKABLE static void unsetRectangle();

	[[nodiscard]] quint32 windowId() const { return this->mWindowId; }
	[[nodiscard]] QString appId() const { return this->mAppId; }
	[[nodiscard]] QString title() const { return this->mTitle; }
	[[nodiscard]] static Toplevel* parent() { return nullptr; }
	[[nodiscard]] bool activated() const { return this->mActivated; }
	[[nodiscard]] QList<QuickshellScreenInfo*> screens() const;
	[[nodiscard]] bool maximized() const { return this->mMaximized; }
	void setMaximized(bool maximized);
	[[nodiscard]] bool minimized() const { return this->mMinimized; }
	void setMinimized(bool minimized);
	[[nodiscard]] bool fullscreen() const { return this->mFullscreen; }
	void setFullscreen(bool fullscreen);

signals:
	void closed();
	void appIdChanged();
	void titleChanged();
	void parentChanged();
	void activatedChanged();
	void screensChanged();
	void maximizedChanged();
	void minimizedChanged();
	void fullscreenChanged();

private:
	quint32 mWindowId;
	qint32 mProcessId;
	QString mAppId;
	QString mTitle;
	QRect mFrame;
	std::optional<QRect> mFrameBeforeMaximize;
	bool mActivated = false;
	bool mMaximized = false;
	bool mMinimized = false;
	bool mFullscreen = false;
};

class ToplevelManager: public QObject {
	Q_OBJECT;

public:
	static ToplevelManager* instance();

	[[nodiscard]] ObjectModel<Toplevel>* toplevels() { return &this->mToplevels; }
	[[nodiscard]] Toplevel* activeToplevel() const { return this->mActiveToplevel; }
	void scheduleRefresh();

signals:
	void activeToplevelChanged();

private:
	ToplevelManager();

	void refresh();
	void applyNativeWindows(const QList<qs::macos::NativeWindow>& windows, quint32 focusedWindowId);

	ObjectModel<Toplevel> mToplevels {this};
	QHash<quint32, Toplevel*> mToplevelsByWindow;
	QPointer<Toplevel> mActiveToplevel;
	QTimer mPollTimer;
	qs::macos::CoalescingBackgroundReader mReader {"MacShell.Toplevels"};
	QTimer mRefreshDebounce;
};

class ToplevelManagerQml: public QObject {
	Q_OBJECT;
	Q_PROPERTY(UntypedObjectModel* toplevels READ toplevels CONSTANT);
	Q_PROPERTY(qs::wayland::toplevel::Toplevel* activeToplevel READ activeToplevel NOTIFY activeToplevelChanged);
	QML_NAMED_ELEMENT(ToplevelManager);
	QML_SINGLETON;

public:
	explicit ToplevelManagerQml(QObject* parent = nullptr);

	[[nodiscard]] static ObjectModel<Toplevel>* toplevels() { return ToplevelManager::instance()->toplevels(); }
	[[nodiscard]] static Toplevel* activeToplevel() { return ToplevelManager::instance()->activeToplevel(); }

signals:
	void activeToplevelChanged();
};

}

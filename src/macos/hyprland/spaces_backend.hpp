#pragma once

#include <functional>

#include <qlist.h>
#include <qrect.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::hyprland::macos {

struct DisplaySpaces {
	QString displayIdentifier;
	QList<quint64> desktopSpaceIds;
	QList<quint64> fullscreenSpaceIds;
	quint64 currentSpaceId = 0;
};

struct ScreenDescription {
	QString name;
	QString displayIdentifier;
	QRect frame;
	qreal scale = 1;
	quint32 displayId = 0;
};

struct WindowDescription {
	quint32 windowId = 0;
	qint32 processId = 0;
	QString applicationName;
	QString bundleIdentifier;
	QString title;
	QRect frame;
	QList<quint64> spaceIds;
};

QList<DisplaySpaces> readDisplaySpaces();
QList<ScreenDescription> readScreens();
QList<WindowDescription> readWindows();
quint32 frontmostWindowId();
QString focusedDisplayIdentifier();

bool switchToDesktop(int globalDesktopNumber, int stepsOnCurrentDisplay);
void launchDetached(const QString& command);

class SystemEventObserver {
public:
	SystemEventObserver(std::function<void()> onSpacesChanged, std::function<void()> onFocusChanged);
	~SystemEventObserver();
	SystemEventObserver(const SystemEventObserver&) = delete;
	SystemEventObserver& operator=(const SystemEventObserver&) = delete;
	SystemEventObserver(SystemEventObserver&&) = delete;
	SystemEventObserver& operator=(SystemEventObserver&&) = delete;

private:
	struct Registration {
		void* notificationCenter = nullptr;
		void* observerToken = nullptr;
	};

	QList<Registration> registrations;
};

}

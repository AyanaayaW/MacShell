#pragma once

#include <qlist.h>
#include <qrect.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::macos {

struct NativeWindow {
	quint32 windowId = 0;
	qint32 processId = 0;
	QString appId;
	QString applicationName;
	QString title;
	QRect frame;
	bool minimized = false;
	bool fullscreen = false;
};

bool hasAccessibilityAccess();
QList<NativeWindow> readNativeWindows();
quint32 focusedWindowId();
QRect visibleFrameForWindow(const QRect& frame);

void activateWindow(qint32 processId, quint32 windowId);
void closeWindow(qint32 processId, quint32 windowId);
void setWindowMinimized(qint32 processId, quint32 windowId, bool minimized);
void setWindowFullscreen(qint32 processId, quint32 windowId, bool fullscreen);
void setWindowFrame(qint32 processId, quint32 windowId, const QRect& frame);

}

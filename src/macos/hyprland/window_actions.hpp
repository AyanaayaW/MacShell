#pragma once

#include <qlist.h>
#include <qtypes.h>

#include "spaces_backend.hpp"

namespace qs::hyprland::macos {

enum class Direction : quint8 {
	Left,
	Right,
	Up,
	Down,
};

bool closeFocusedWindow();
bool closeWindow(qint32 processId, quint32 windowId);
bool toggleFocusedFullscreen();
bool maximizeFocusedWindow();
bool centerFocusedWindow();
bool resizeFocusedWindow(int widthDelta, int heightDelta);
bool snapFocusedWindow(Direction direction);
bool focusWindow(qint32 processId, quint32 windowId);
bool focusWindowInDirection(Direction direction, const QList<WindowDescription>& windows);

}

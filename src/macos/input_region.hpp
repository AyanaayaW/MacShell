#pragma once

#include <optional>

#include <qregion.h>
#include <qwindow.h>

namespace qs::macos {

void applyInputRegion(QWindow* window, const std::optional<QRegion>& inputRegion);

}

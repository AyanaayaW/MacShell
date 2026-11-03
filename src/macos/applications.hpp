#pragma once

#include <qlist.h>

#include "../core/desktopentry.hpp"

namespace qs::macos {

QList<ParsedDesktopEntryData> scanInstalledApplications();
void watchApplicationDirectories();

}

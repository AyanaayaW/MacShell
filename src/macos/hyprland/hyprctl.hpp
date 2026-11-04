#pragma once

#include <qstring.h>

namespace qs::hyprland::ipc {

QString hyprctlSocketPath();
void startHyprctlServer();
int runHyprctl(int argc, char** argv);

}

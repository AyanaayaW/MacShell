#pragma once

#include <optional>

namespace qs::macos::compat {

std::optional<int> runCompatCommand(int argc, char** argv);
void exposeCompatCommandsOnPath();

int runLoginctl(int argc, char** argv);
int runSystemctl(int argc, char** argv);
int runBrightnessctl(int argc, char** argv);
int runWpctl(int argc, char** argv);
int runPactl(int argc, char** argv);
int runNmcli(int argc, char** argv);
int runWlCopy(int argc, char** argv);
int runWlPaste(int argc, char** argv);
int runXdgOpen(int argc, char** argv);
int runCliphist(int argc, char** argv);
int runSwww(int argc, char** argv);
int runHyprpicker(int argc, char** argv);
int runGrim(int argc, char** argv);
int runNproc(int argc, char** argv);
int runLinuxInstall(int argc, char** argv);

}

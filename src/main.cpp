#include <array>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "launch/main.hpp"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <unistd.h>

#include "macos/compat/commands.hpp"
#include "macos/hyprland/hyprctl.hpp"
#include "services/notifications/local_bridge.hpp"
#include "services/pam/subprocess.hpp"

namespace {

void relaunchFromRealPathIfLinked(char** argv) {
	std::array<char, PATH_MAX> executablePath {};
	auto pathSize = static_cast<uint32_t>(executablePath.size());
	if (_NSGetExecutablePath(executablePath.data(), &pathSize) != 0) return;

	std::array<char, PATH_MAX> resolvedPath {};
	if (!realpath(executablePath.data(), resolvedPath.data())) return;
	if (std::strcmp(executablePath.data(), resolvedPath.data()) == 0) return;
	if (!std::strstr(resolvedPath.data(), ".app/Contents/MacOS/")) return;

	execv(resolvedPath.data(), argv);
}

bool isInvokedAs(const char* commandName, int argc, char** argv) {
	const char* programName = std::strrchr(argv[0], '/');
	programName = programName ? programName + 1 : argv[0];
	return std::strcmp(programName, commandName) == 0
	    || (argc > 1 && std::strcmp(argv[1], commandName) == 0);
}

}
#endif

int main(int argc, char** argv) {
#ifdef __APPLE__
	relaunchFromRealPathIfLinked(argv);
	setenv("QML_DISABLE_DISTANCEFIELD", "1", 0);
	setenv("QSG_PREFER_FULLSIZE_GLYPHCACHE_TEXTURES", "1", 0);
	setenv("QML_USE_GLYPHCACHE_WORKAROUND", "1", 0);
	setenv("QT_ENABLE_GLYPH_CACHE_WORKAROUND", "1", 0);
	if (auto helperExitCode = runPamHelperIfRequested(argc, argv)) return *helperExitCode;
	if (isInvokedAs("notify-send", argc, argv)) return qs::service::notifications::runNotifySend(argc, argv);
	if (isInvokedAs("hyprctl", argc, argv)) return qs::hyprland::ipc::runHyprctl(argc, argv);
	if (argc > 1 && std::strcmp(argv[1], "--install") == 0) return qs::macos::compat::runLinuxInstall(argc, argv);
	if (auto compatExitCode = qs::macos::compat::runCompatCommand(argc, argv)) return *compatExitCode;
	qs::macos::compat::exposeCompatCommandsOnPath();
#endif
	return qs::launch::main(argc, argv);
}

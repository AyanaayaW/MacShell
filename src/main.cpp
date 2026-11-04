#include <cstring>

#include "launch/main.hpp"

#ifdef __APPLE__
#include "macos/hyprland/hyprctl.hpp"
#include "services/notifications/local_bridge.hpp"
#include "services/pam/subprocess.hpp"

namespace {

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
	if (auto helperExitCode = runPamHelperIfRequested(argc, argv)) return *helperExitCode;
	if (isInvokedAs("notify-send", argc, argv)) return qs::service::notifications::runNotifySend(argc, argv);
	if (isInvokedAs("hyprctl", argc, argv)) return qs::hyprland::ipc::runHyprctl(argc, argv);
#endif
	return qs::launch::main(argc, argv);
}

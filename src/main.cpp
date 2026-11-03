#include <cstring>

#include "launch/main.hpp"

#ifdef __APPLE__
#include "services/notifications/local_bridge.hpp"
#include "services/pam/subprocess.hpp"

namespace {

bool isNotifySendInvocation(int argc, char** argv) {
	const char* programName = std::strrchr(argv[0], '/');
	programName = programName ? programName + 1 : argv[0];
	return std::strcmp(programName, "notify-send") == 0
	    || (argc > 1 && std::strcmp(argv[1], "notify-send") == 0);
}

}
#endif

int main(int argc, char** argv) {
#ifdef __APPLE__
	if (auto helperExitCode = runPamHelperIfRequested(argc, argv)) return *helperExitCode;
	if (isNotifySendInvocation(argc, argv)) return qs::service::notifications::runNotifySend(argc, argv);
#endif
	return qs::launch::main(argc, argv);
}

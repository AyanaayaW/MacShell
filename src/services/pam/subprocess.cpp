#include "subprocess.hpp"
#include <array>
#include <ostream>
#include <string>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qstring.h>
#include <sched.h>
#if defined(__FreeBSD__) || defined(__APPLE__)
#include <security/pam_types.h>
#else
#include <security/_pam_types.h>
#endif
#include <security/pam_appl.h>
#include <unistd.h>

#include "conversation.hpp"
#include "ipc.hpp"

#ifdef __APPLE__
#include <crt_externs.h>
#include <cstdlib>
#include <cstring>
#include <mach-o/dyld.h>
#include <spawn.h>

#include <string>
#include <vector>

namespace {

constexpr auto PAM_HELPER_FLAG = "--macshell-pam-helper";
constexpr int PAM_HELPER_ARGUMENT_COUNT = 8;

std::string currentExecutablePath() {
	uint32_t pathLength = 0;
	_NSGetExecutablePath(nullptr, &pathLength);
	std::string path(pathLength, '\0');
	if (_NSGetExecutablePath(path.data(), &pathLength) != 0) return {};
	path.resize(std::strlen(path.c_str()));
	return path;
}

}

pid_t spawnPamHelper(
    int helperInput,
    int helperOutput,
    bool log,
    const char* configDir,
    const char* config,
    const char* user
) {
	auto executablePath = currentExecutablePath();
	auto inputArgument = std::to_string(helperInput);
	auto outputArgument = std::to_string(helperOutput);
	std::string logArgument = log ? "1" : "0";

	std::vector<char*> arguments {
	    executablePath.data(),
	    const_cast<char*>(PAM_HELPER_FLAG),
	    inputArgument.data(),
	    outputArgument.data(),
	    logArgument.data(),
	    const_cast<char*>(configDir),
	    const_cast<char*>(config),
	    const_cast<char*>(user),
	    nullptr,
	};

	posix_spawn_file_actions_t fileActions;
	posix_spawn_file_actions_init(&fileActions);
	posix_spawn_file_actions_addinherit_np(&fileActions, helperInput);
	posix_spawn_file_actions_addinherit_np(&fileActions, helperOutput);
	posix_spawn_file_actions_addinherit_np(&fileActions, STDOUT_FILENO);
	posix_spawn_file_actions_addinherit_np(&fileActions, STDERR_FILENO);

	posix_spawnattr_t attributes;
	posix_spawnattr_init(&attributes);
	posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);

	pid_t pid = -1;
	auto result = posix_spawn(&pid, executablePath.c_str(), &fileActions, &attributes, arguments.data(), *_NSGetEnviron());

	posix_spawnattr_destroy(&attributes);
	posix_spawn_file_actions_destroy(&fileActions);

	if (result != 0) {
		qCDebug(logPam) << "Failed to spawn PAM helper process.";
		return -1;
	}
	return pid;
}

std::optional<int> runPamHelperIfRequested(int argc, char** argv) {
	if (argc != PAM_HELPER_ARGUMENT_COUNT || std::strcmp(argv[1], PAM_HELPER_FLAG) != 0) return std::nullopt;

	auto helperInput = std::atoi(argv[2]);
	auto helperOutput = std::atoi(argv[3]);
	auto log = std::strcmp(argv[4], "1") == 0;

	auto subprocess = PamSubprocess(log, helperInput, helperOutput);
	auto code = subprocess.exec(argv[5], argv[6], argv[7]);
	subprocess.sendCode(code);
	return 0;
}
#endif

pid_t PamConversation::createSubprocess(
    PamIpcPipes* pipes,
    const QString& configDir,
    const QString& config,
    const QString& user
) {
	auto toSubprocess = std::array<int, 2>();
	auto fromSubprocess = std::array<int, 2>();

	if (pipe(toSubprocess.data()) == -1 || pipe(fromSubprocess.data()) == -1) {
		qCDebug(logPam) << "Failed to create pipes for subprocess.";
		return 0;
	}

	auto* configDirF = strdup(configDir.toStdString().c_str()); // NOLINT (include)
	auto* configF = strdup(config.toStdString().c_str());       // NOLINT (include)
	auto* userF = strdup(user.toStdString().c_str());           // NOLINT (include)
	auto log = logPam().isDebugEnabled();

#ifdef __APPLE__
	auto pid = spawnPamHelper(toSubprocess[0], fromSubprocess[1], log, configDirF, configF, userF);
	if (pid > 0) {
#else
	auto pid = fork();

	if (pid < 0) {
		qCDebug(logPam) << "Failed to fork for subprocess.";
	} else if (pid == 0) {
		close(toSubprocess[1]);
		close(fromSubprocess[0]);

		{
			auto subprocess = PamSubprocess(log, toSubprocess[0], fromSubprocess[1]);
			auto code = subprocess.exec(configDirF, configF, userF);
			subprocess.sendCode(code);
		}

		free(configDirF); // NOLINT
		free(configF);    // NOLINT
		free(userF);      // NOLINT

		_exit(0);
	} else {
#endif
		close(toSubprocess[0]);   // close r
		close(fromSubprocess[1]); // close w

		pipes->fdIn = fromSubprocess[0];
		pipes->fdOut = toSubprocess[1];

		free(configDirF); // NOLINT
		free(configF);    // NOLINT
		free(userF);      // NOLINT

		return pid;
	}

#ifdef __APPLE__
	close(toSubprocess[0]);
	close(toSubprocess[1]);
	close(fromSubprocess[0]);
	close(fromSubprocess[1]);
	free(configDirF);
	free(configF);
	free(userF);
#endif

	return -1; // should never happen but lint
}

PamIpcExitCode PamSubprocess::exec(const char* configDir, const char* config, const char* user) {
	logIf(this->log) << "Waiting for parent confirmation..." << std::endl;

	auto conv = pam_conv {
	    .conv = &PamSubprocess::conversation,
	    .appdata_ptr = this,
	};

	pam_handle_t* handle = nullptr;

	logIf(this->log) << "Starting pam session for user \"" << user << "\" with config \"" << config
	                 << "\" in dir \"" << configDir << "\"" << std::endl;

#if defined(__FreeBSD__) || defined(__APPLE__)
	auto result = pam_start(config, user, &conv, &handle);
#else
	auto result = pam_start_confdir(config, user, &conv, configDir, &handle);
#endif

	if (result != PAM_SUCCESS) {
		logIf(true) << "Unable to start pam conversation with error \"" << pam_strerror(handle, result)
		            << "\" (code " << result << ")" << std::endl;
		return PamIpcExitCode::StartFailed;
	}

	result = pam_authenticate(handle, 0);
	PamIpcExitCode code = PamIpcExitCode::OtherError;

	switch (result) {
	case PAM_SUCCESS:
		logIf(this->log) << "Authenticated successfully." << std::endl;
		code = PamIpcExitCode::Success;
		break;
	case PAM_AUTH_ERR:
		logIf(this->log) << "Failed to authenticate." << std::endl;
		code = PamIpcExitCode::AuthFailed;
		break;
	case PAM_MAXTRIES:
		logIf(this->log) << "Failed to authenticate due to hitting max tries." << std::endl;
		code = PamIpcExitCode::MaxTries;
		break;
	default:
		logIf(true) << "Error while authenticating: \"" << pam_strerror(handle, result) << "\" (code "
		            << result << ")" << std::endl;
		code = PamIpcExitCode::PamError;
		break;
	}

	result = pam_end(handle, result);
	if (result != PAM_SUCCESS) {
		logIf(true) << "Error in pam_end: \"" << pam_strerror(handle, result) << "\" (code " << result
		            << ")" << std::endl;
	}

	return code;
}

void PamSubprocess::sendCode(PamIpcExitCode code) {
	{
		auto eventType = PamIpcEvent::Exit;
		auto ok = this->pipes.writeBytes(reinterpret_cast<char*>(&eventType), sizeof(PamIpcEvent));

		if (!ok) goto fail;

		ok = this->pipes.writeBytes(reinterpret_cast<char*>(&code), sizeof(PamIpcExitCode));

		if (!ok) goto fail;

		return;
	}

fail:
	_exit(1);
}

int PamSubprocess::conversation(
    int msgCount,
    const pam_message** msgArray,
    pam_response** responseArray,
    void* appdata
) {
	auto* delegate = static_cast<PamSubprocess*>(appdata);

	// freed by libc so must be alloc'd by it.
	auto* responses = static_cast<pam_response*>(calloc(msgCount, sizeof(pam_response))); // NOLINT

	for (auto i = 0; i < msgCount; i++) {
		const auto* message = msgArray[i]; // NOLINT
		auto& response = responses[i];     // NOLINT

		auto msgString = std::string(message->msg);
		auto req = PamIpcRequestFlags {
		    .echo = message->msg_style != PAM_PROMPT_ECHO_OFF,
		    .error = message->msg_style == PAM_ERROR_MSG,
		    .responseRequired =
		        message->msg_style == PAM_PROMPT_ECHO_OFF || message->msg_style == PAM_PROMPT_ECHO_ON,
		};

		logIf(delegate->log) << "Relaying pam message: \"" << msgString << "\" echo: " << req.echo
		                     << " error: " << req.error << " responseRequired: " << req.responseRequired
		                     << std::endl;

		auto eventType = PamIpcEvent::Request;
		auto ok = delegate->pipes.writeBytes(reinterpret_cast<char*>(&eventType), sizeof(PamIpcEvent));

		if (!ok) goto fail;

		ok =
		    delegate->pipes.writeBytes(reinterpret_cast<const char*>(&req), sizeof(PamIpcRequestFlags));

		if (!ok) goto fail;
		if (!delegate->pipes.writeString(msgString)) goto fail;

		if (req.responseRequired) {
			auto ok = false;
			auto resp = delegate->pipes.readString(&ok);
			if (!ok) _exit(static_cast<int>(PamIpcExitCode::OtherError));
			logIf(delegate->log) << "Got response for request.";

			response.resp = strdup(resp.c_str()); // NOLINT (include)
		}
	}

	*responseArray = responses;
	return PAM_SUCCESS;

fail:
	free(responses); // NOLINT
	_exit(1);
}

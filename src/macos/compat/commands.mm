#include "commands.hpp"
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <unistd.h>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace qs::macos::compat {

namespace {

using CommandEntry = int (*)(int, char**);

struct NamedCommand {
	std::string_view name;
	CommandEntry entry;
};

constexpr std::array COMPAT_COMMANDS {
	NamedCommand {"loginctl", runLoginctl},
	NamedCommand {"systemctl", runSystemctl},
	NamedCommand {"brightnessctl", runBrightnessctl},
	NamedCommand {"wpctl", runWpctl},
	NamedCommand {"wl-copy", runWlCopy},
	NamedCommand {"wl-paste", runWlPaste},
	NamedCommand {"xdg-open", runXdgOpen},
};

std::string_view programName(const char* path) {
	const char* lastSlash = std::strrchr(path, '/');
	return lastSlash ? lastSlash + 1 : path;
}

bool sendLoginWindowEvent(AEEventID eventId) {
	const char* loginWindowBundleId = "com.apple.loginwindow";
	AEAddressDesc loginWindowAddress {};
	if (AECreateDesc(
	        typeApplicationBundleID,
	        loginWindowBundleId,
	        static_cast<Size>(std::strlen(loginWindowBundleId)),
	        &loginWindowAddress
	    )
	    != noErr)
	{
		return false;
	}

	AppleEvent event {};
	AppleEvent reply {};
	OSErr createStatus = AECreateAppleEvent(
	    kCoreEventClass,
	    eventId,
	    &loginWindowAddress,
	    kAutoGenerateReturnID,
	    kAnyTransactionID,
	    &event
	);
	AEDisposeDesc(&loginWindowAddress);
	if (createStatus != noErr) return false;

	OSStatus sendStatus = AESendMessage(&event, &reply, kAENoReply, kAEDefaultTimeout);
	AEDisposeDesc(&event);
	AEDisposeDesc(&reply);
	return sendStatus == noErr;
}

bool lockScreen() {
	using LockScreenFunction = int (*)();
	void* loginFramework = dlopen("/System/Library/PrivateFrameworks/login.framework/login", RTLD_LAZY);
	auto lockScreenNow = loginFramework
	                       ? reinterpret_cast<LockScreenFunction>(dlsym(loginFramework, "SACLockScreenImmediate"))
	                       : nullptr;
	if (!lockScreenNow) return false;
	lockScreenNow();
	return true;
}

std::optional<int> runPowerAction(std::string_view action) {
	auto statusFor = [](bool succeeded) { return succeeded ? 0 : 1; };
	if (action == "suspend" || action == "hibernate" || action == "hybrid-sleep"
	    || action == "suspend-then-hibernate")
	{
		return statusFor(sendLoginWindowEvent(kAESleep));
	}
	if (action == "poweroff" || action == "halt") return statusFor(sendLoginWindowEvent(kAEShutDown));
	if (action == "reboot" || action == "soft-reboot") return statusFor(sendLoginWindowEvent(kAERestart));
	return std::nullopt;
}

std::optional<std::string_view> firstPositional(int argc, char** argv) {
	for (int index = 1; index < argc; index++) {
		if (argv[index][0] != '-') return argv[index];
	}
	return std::nullopt;
}

int reportUnsupported(std::string_view command, std::string_view verb) {
	std::fprintf(
	    stderr,
	    "%.*s: '%.*s' is not available on macOS\n",
	    static_cast<int>(command.size()),
	    command.data(),
	    static_cast<int>(verb.size()),
	    verb.data()
	);
	return 1;
}

NSData* readAllStandardInput() {
	return [NSFileHandle.fileHandleWithStandardInput readDataToEndOfFile];
}

void writeToStandardOutput(NSData* data) {
	std::fwrite(data.bytes, 1, data.length, stdout);
}

bool isTextMimeType(NSString* mimeType) {
	return mimeType.length == 0 || [mimeType hasPrefix:@"text/plain"] || [mimeType isEqualToString:@"TEXT"]
	    || [mimeType isEqualToString:@"STRING"] || [mimeType isEqualToString:@"UTF8_STRING"];
}

NSPasteboardType pasteboardTypeForMime(NSString* mimeType) {
	if (isTextMimeType(mimeType)) return NSPasteboardTypeString;
	if ([mimeType isEqualToString:@"text/uri-list"]) return NSPasteboardTypeFileURL;
	UTType* type = [UTType typeWithMIMEType:mimeType];
	return type ? type.identifier : mimeType;
}

NSString* mimeForPasteboardType(NSPasteboardType pasteboardType) {
	if ([pasteboardType isEqualToString:NSPasteboardTypeString]) return @"text/plain;charset=utf-8";
	if ([pasteboardType isEqualToString:NSPasteboardTypeFileURL]) return @"text/uri-list";
	UTType* type = [UTType typeWithIdentifier:pasteboardType];
	return type.preferredMIMEType;
}

}

std::optional<int> runCompatCommand(int argc, char** argv) {
	auto invokedName = programName(argv[0]);
	for (const auto& command: COMPAT_COMMANDS) {
		if (invokedName == command.name) return command.entry(argc, argv);
	}
	if (argc > 1) {
		for (const auto& command: COMPAT_COMMANDS) {
			if (command.name == argv[1]) return command.entry(argc - 1, argv + 1);
		}
	}
	return std::nullopt;
}

void exposeCompatCommandsOnPath() {
	std::array<char, PATH_MAX> executablePath {};
	auto pathSize = static_cast<uint32_t>(executablePath.size());
	if (_NSGetExecutablePath(executablePath.data(), &pathSize) != 0) return;

	std::array<char, PATH_MAX> resolvedPath {};
	if (!realpath(executablePath.data(), resolvedPath.data())) return;

	std::string executableDirectory = resolvedPath.data();
	executableDirectory.erase(executableDirectory.rfind('/'));

	const char* currentPath = std::getenv("PATH");
	std::string searchPath = currentPath ? currentPath : "/usr/bin:/bin:/usr/sbin:/sbin";
	auto containsEntry = [&searchPath](const std::string& entry) {
		return (":" + searchPath + ":").find(":" + entry + ":") != std::string::npos;
	};

	if (!containsEntry(executableDirectory)) searchPath = executableDirectory + ":" + searchPath;
	for (const std::string userToolDirectory: {"/opt/homebrew/bin", "/usr/local/bin"}) {
		if (!containsEntry(userToolDirectory) && access(userToolDirectory.c_str(), X_OK) == 0) {
			searchPath += ":" + userToolDirectory;
		}
	}
	setenv("PATH", searchPath.c_str(), 1);
}

int runLoginctl(int argc, char** argv) {
	auto verb = firstPositional(argc, argv);
	if (!verb) return reportUnsupported("loginctl", "(no command)");
	if (*verb == "lock-session" || *verb == "lock-sessions") return lockScreen() ? 0 : 1;
	if (*verb == "terminate-user" || *verb == "terminate-session" || *verb == "kill-session"
	    || *verb == "kill-user")
	{
		return sendLoginWindowEvent(kAEReallyLogOut) ? 0 : 1;
	}
	if (auto powerStatus = runPowerAction(*verb)) return *powerStatus;
	return reportUnsupported("loginctl", *verb);
}

int runSystemctl(int argc, char** argv) {
	bool userScope = false;
	for (int index = 1; index < argc; index++) {
		if (std::strcmp(argv[index], "--user") == 0) userScope = true;
	}

	auto verb = firstPositional(argc, argv);
	if (!verb) return reportUnsupported("systemctl", "(no command)");
	if (*verb == "is-active") {
		std::puts("inactive");
		return 3;
	}
	if (*verb == "is-enabled") {
		std::puts("disabled");
		return 1;
	}
	if (!userScope) {
		if (auto powerStatus = runPowerAction(*verb)) return *powerStatus;
	}
	return reportUnsupported("systemctl", *verb);
}

int runWlCopy(int argc, char** argv) {
	@autoreleasepool {
		NSString* mimeType = @"";
		bool trimNewline = false;
		bool clearOnly = false;
		NSMutableArray<NSString*>* words = [NSMutableArray array];

		for (int index = 1; index < argc; index++) {
			std::string_view argument = argv[index];
			if ((argument == "-t" || argument == "--type") && index + 1 < argc) {
				mimeType = @(argv[++index]);
			} else if (argument.starts_with("--type=")) {
				mimeType = @(argv[index] + std::strlen("--type="));
			} else if (argument == "-n" || argument == "--trim-newline") {
				trimNewline = true;
			} else if (argument == "-c" || argument == "--clear") {
				clearOnly = true;
			} else if (argument == "--") {
				for (index++; index < argc; index++) [words addObject:@(argv[index])];
			} else if (!argument.starts_with("-")) {
				[words addObject:@(argv[index])];
			}
		}

		NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
		[pasteboard clearContents];
		if (clearOnly) return 0;

		NSData* content = words.count > 0 ? [[words componentsJoinedByString:@" "] dataUsingEncoding:NSUTF8StringEncoding]
		                                  : readAllStandardInput();
		NSPasteboardType pasteboardType = pasteboardTypeForMime(mimeType);

		if ([pasteboardType isEqualToString:NSPasteboardTypeString]) {
			NSString* text = [[NSString alloc] initWithData:content encoding:NSUTF8StringEncoding];
			if (!text) return 1;
			if (trimNewline && [text hasSuffix:@"\n"]) text = [text substringToIndex:text.length - 1];
			return [pasteboard setString:text forType:NSPasteboardTypeString] ? 0 : 1;
		}
		return [pasteboard setData:content forType:pasteboardType] ? 0 : 1;
	}
}

int runWlPaste(int argc, char** argv) {
	@autoreleasepool {
		NSString* mimeType = @"";
		bool appendNewline = true;
		bool listTypes = false;

		for (int index = 1; index < argc; index++) {
			std::string_view argument = argv[index];
			if ((argument == "-t" || argument == "--type") && index + 1 < argc) {
				mimeType = @(argv[++index]);
			} else if (argument.starts_with("--type=")) {
				mimeType = @(argv[index] + std::strlen("--type="));
			} else if (argument == "-n" || argument == "--no-newline") {
				appendNewline = false;
			} else if (argument == "-l" || argument == "--list-types") {
				listTypes = true;
			}
		}

		NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
		if (listTypes) {
			NSMutableOrderedSet<NSString*>* mimeTypes = [NSMutableOrderedSet orderedSet];
			for (NSPasteboardType pasteboardType in pasteboard.types) {
				if (NSString* mime = mimeForPasteboardType(pasteboardType)) [mimeTypes addObject:mime];
			}
			for (NSString* mime in mimeTypes) std::puts(mime.UTF8String);
			return mimeTypes.count > 0 ? 0 : 1;
		}

		NSPasteboardType pasteboardType = pasteboardTypeForMime(mimeType);
		if ([pasteboardType isEqualToString:NSPasteboardTypeString]) {
			NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
			if (!text) {
				std::fputs("No selection\n", stderr);
				return 1;
			}
			std::fputs(text.UTF8String, stdout);
			if (appendNewline && ![text hasSuffix:@"\n"]) std::fputc('\n', stdout);
			return 0;
		}

		NSData* data = [pasteboard dataForType:pasteboardType];
		if (!data) {
			std::fputs("No suitable type of content copied\n", stderr);
			return 1;
		}
		writeToStandardOutput(data);
		return 0;
	}
}

int runXdgOpen(int argc, char** argv) {
	@autoreleasepool {
		auto target = firstPositional(argc, argv);
		if (!target) {
			std::fputs("xdg-open: missing target\n", stderr);
			return 1;
		}

		NSString* targetString = @(target->data());
		NSURL* url = [NSURL URLWithString:targetString];
		if (!url || url.scheme.length < 2) {
			url = [NSURL fileURLWithPath:targetString.stringByExpandingTildeInPath];
		}
		if (url.isFileURL && ![NSFileManager.defaultManager fileExistsAtPath:url.path]) {
			std::fprintf(stderr, "xdg-open: file '%s' does not exist\n", target->data());
			return 2;
		}
		return [NSWorkspace.sharedWorkspace openURL:url] ? 0 : 4;
	}
}

}

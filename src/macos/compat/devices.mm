#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include <dlfcn.h>

#include "../services/pipewire/core_audio.hpp"
#include "commands.hpp"

#import <CoreGraphics/CoreGraphics.h>

namespace qs::macos::compat {

namespace {

namespace coreaudio = qs::service::pipewire::coreaudio;

constexpr int BRIGHTNESS_MAX = 100;
constexpr const char* BACKLIGHT_DEVICE_NAME = "apple_backlight";
constexpr const char* BACKLIGHT_CLASS_NAME = "backlight";

struct DisplayServicesApi {
	int (*getBrightness)(CGDirectDisplayID, float*) = nullptr;
	int (*setBrightness)(CGDirectDisplayID, float) = nullptr;
};

const DisplayServicesApi& displayServices() {
	static const DisplayServicesApi api = [] {
		DisplayServicesApi loaded;
		void* framework =
		    dlopen("/System/Library/PrivateFrameworks/DisplayServices.framework/DisplayServices", RTLD_LAZY);
		if (!framework) return loaded;
		loaded.getBrightness =
		    reinterpret_cast<int (*)(CGDirectDisplayID, float*)>(dlsym(framework, "DisplayServicesGetBrightness"));
		loaded.setBrightness =
		    reinterpret_cast<int (*)(CGDirectDisplayID, float)>(dlsym(framework, "DisplayServicesSetBrightness"));
		return loaded;
	}();
	return api;
}

std::optional<CGDirectDisplayID> backlitDisplay() {
	const auto& api = displayServices();
	if (!api.getBrightness) return std::nullopt;

	std::array<CGDirectDisplayID, 16> displays {};
	uint32_t displayCount = 0;
	if (CGGetActiveDisplayList(displays.size(), displays.data(), &displayCount) != kCGErrorSuccess) {
		return std::nullopt;
	}

	std::sort(displays.begin(), displays.begin() + displayCount, [](CGDirectDisplayID left, CGDirectDisplayID right) {
		return CGDisplayIsBuiltin(left) > CGDisplayIsBuiltin(right);
	});
	for (uint32_t index = 0; index < displayCount; index++) {
		float brightness = 0;
		if (api.getBrightness(displays[index], &brightness) == 0) return displays[index];
	}
	return std::nullopt;
}

struct ValueChange {
	double amount = 0;
	int direction = 0;
	bool isPercent = false;
};

std::optional<ValueChange> parseValueChange(std::string_view text) {
	ValueChange change;
	if (text.starts_with('+')) {
		change.direction = 1;
		text.remove_prefix(1);
	} else if (text.starts_with('-')) {
		change.direction = -1;
		text.remove_prefix(1);
	}
	if (text.ends_with('+')) {
		change.direction = 1;
		text.remove_suffix(1);
	} else if (text.ends_with('-')) {
		change.direction = -1;
		text.remove_suffix(1);
	}
	if (text.ends_with('%')) {
		change.isPercent = true;
		text.remove_suffix(1);
	}
	if (text.empty()) return std::nullopt;

	std::string number(text);
	char* parseEnd = nullptr;
	change.amount = std::strtod(number.c_str(), &parseEnd);
	if (parseEnd != number.c_str() + number.size() || change.amount < 0) return std::nullopt;
	return change;
}

double applyValueChange(double current, const ValueChange& change, double scale) {
	double amount = change.isPercent ? change.amount / 100.0 * scale : change.amount;
	if (change.direction == 0) return amount;
	return current + change.direction * amount;
}

void printBrightness(int current, bool machineReadable) {
	int percent = static_cast<int>(std::lround(100.0 * current / BRIGHTNESS_MAX));
	if (machineReadable) {
		std::printf("%s,%s,%d,%d%%,%d\n", BACKLIGHT_DEVICE_NAME, BACKLIGHT_CLASS_NAME, current, percent, BRIGHTNESS_MAX);
		return;
	}
	std::printf(
	    "Device '%s' of class '%s':\n\tCurrent brightness: %d (%d%%)\n\tMax brightness: %d\n",
	    BACKLIGHT_DEVICE_NAME,
	    BACKLIGHT_CLASS_NAME,
	    current,
	    percent,
	    BRIGHTNESS_MAX
	);
}

std::optional<coreaudio::AudioDirection> directionForTarget(std::string_view target) {
	if (target == "@DEFAULT_AUDIO_SINK@" || target == "@DEFAULT_SINK@") return coreaudio::AudioDirection::Output;
	if (target == "@DEFAULT_AUDIO_SOURCE@" || target == "@DEFAULT_SOURCE@") return coreaudio::AudioDirection::Input;
	return std::nullopt;
}

struct AudioTarget {
	AudioObjectID deviceId = kAudioObjectUnknown;
	coreaudio::AudioDirection direction = coreaudio::AudioDirection::Output;
};

std::optional<AudioTarget> resolveAudioTarget(std::string_view target) {
	if (auto direction = directionForTarget(target)) {
		auto deviceId = coreaudio::defaultDevice(*direction);
		if (deviceId == kAudioObjectUnknown) return std::nullopt;
		return AudioTarget {deviceId, *direction};
	}

	std::string number(target);
	char* parseEnd = nullptr;
	auto deviceId = static_cast<AudioObjectID>(std::strtoul(number.c_str(), &parseEnd, 10));
	if (number.empty() || parseEnd != number.c_str() + number.size()) return std::nullopt;
	for (auto knownDevice: coreaudio::deviceIdentifiers()) {
		if (knownDevice != deviceId) continue;
		auto direction = coreaudio::deviceHasStreams(deviceId, coreaudio::AudioDirection::Output)
		                   ? coreaudio::AudioDirection::Output
		                   : coreaudio::AudioDirection::Input;
		return AudioTarget {deviceId, direction};
	}
	return std::nullopt;
}

int printWpctlUsage() {
	std::fputs(
	    "Usage: wpctl get-volume ID | set-volume [-l LIMIT] ID VALUE[%][+/-] | set-mute ID 1|0|toggle\n",
	    stderr
	);
	return 1;
}

}

int runBrightnessctl(int argc, char** argv) {
	bool machineReadable = false;
	bool quiet = false;
	std::array<std::string_view, 2> positionals {};
	size_t positionalCount = 0;

	for (int index = 1; index < argc; index++) {
		std::string_view argument = argv[index];
		if (argument == "-m" || argument == "--machine-readable") {
			machineReadable = true;
		} else if (argument == "-q" || argument == "--quiet") {
			quiet = true;
		} else if ((argument == "-d" || argument == "--device" || argument == "-c" || argument == "--class")
		           && index + 1 < argc)
		{
			index++;
		} else if (argument.starts_with("--device=") || argument.starts_with("--class=")) {
			continue;
		} else if (argument.size() > 1 && argument[0] == '-' && !std::isdigit(argument[1])) {
			continue;
		} else if (positionalCount < positionals.size()) {
			positionals[positionalCount++] = argument;
		}
	}

	auto display = backlitDisplay();
	if (!display) {
		std::fputs("Failed to find a display with adjustable brightness\n", stderr);
		return 1;
	}

	float brightness = 0;
	displayServices().getBrightness(*display, &brightness);
	int current = static_cast<int>(std::lround(brightness * BRIGHTNESS_MAX));
	std::string_view operation = positionalCount > 0 ? positionals[0] : "info";

	if (operation == "g" || operation == "get") {
		std::printf("%d\n", current);
		return 0;
	}
	if (operation == "m" || operation == "max") {
		std::printf("%d\n", BRIGHTNESS_MAX);
		return 0;
	}
	if (operation == "i" || operation == "info" || operation == "l" || operation == "-l" || operation == "list") {
		printBrightness(current, machineReadable);
		return 0;
	}
	if (operation != "s" && operation != "set") {
		std::fprintf(stderr, "Invalid operation '%.*s'\n", static_cast<int>(operation.size()), operation.data());
		return 1;
	}

	auto change = positionalCount > 1 ? parseValueChange(positionals[1]) : std::nullopt;
	if (!change) {
		std::fputs("Invalid value given\n", stderr);
		return 1;
	}

	int target = std::clamp(
	    static_cast<int>(std::lround(applyValueChange(current, *change, BRIGHTNESS_MAX))),
	    0,
	    BRIGHTNESS_MAX
	);
	if (displayServices().setBrightness(*display, static_cast<float>(target) / BRIGHTNESS_MAX) != 0) {
		std::fputs("Failed to set brightness\n", stderr);
		return 1;
	}
	if (!quiet) printBrightness(target, machineReadable);
	return 0;
}

int runWpctl(int argc, char** argv) {
	if (argc < 3) return printWpctlUsage();
	std::string_view command = argv[1];

	if (command == "get-volume") {
		auto target = resolveAudioTarget(argv[2]);
		if (!target) return printWpctlUsage();
		auto volume = coreaudio::mainVolume(target->deviceId, target->direction).value_or(0);
		bool muted = coreaudio::isMuted(target->deviceId, target->direction).value_or(false);
		std::printf("Volume: %.2f%s\n", volume, muted ? " [MUTED]" : "");
		return 0;
	}

	if (command == "set-mute") {
		if (argc < 4) return printWpctlUsage();
		auto target = resolveAudioTarget(argv[2]);
		if (!target) return printWpctlUsage();
		std::string_view state = argv[3];
		bool muted = state == "toggle" ? !coreaudio::isMuted(target->deviceId, target->direction).value_or(false)
		                               : state == "1";
		return coreaudio::setMuted(target->deviceId, target->direction, muted) ? 0 : 1;
	}

	if (command == "set-volume") {
		double limit = -1;
		int argumentIndex = 2;
		if (argumentIndex + 1 < argc
		    && (std::strcmp(argv[argumentIndex], "-l") == 0 || std::strcmp(argv[argumentIndex], "--limit") == 0))
		{
			limit = std::strtod(argv[argumentIndex + 1], nullptr);
			argumentIndex += 2;
		}
		if (argumentIndex + 1 >= argc) return printWpctlUsage();

		auto target = resolveAudioTarget(argv[argumentIndex]);
		auto change = parseValueChange(argv[argumentIndex + 1]);
		if (!target || !change) return printWpctlUsage();

		double current = coreaudio::mainVolume(target->deviceId, target->direction).value_or(0);
		double volume = std::max(0.0, applyValueChange(current, *change, 1.0));
		if (limit >= 0) volume = std::min(volume, limit);
		volume = std::min(volume, 1.0);
		return coreaudio::setMainVolume(target->deviceId, target->direction, static_cast<float>(volume)) ? 0 : 1;
	}

	return printWpctlUsage();
}

}

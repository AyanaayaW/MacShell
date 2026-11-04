#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <dlfcn.h>
#include <unistd.h>

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

constexpr double PULSE_NORMAL_VOLUME = 65536.0;

std::string deviceNameFor(AudioObjectID deviceId) {
	return coreaudio::deviceUniqueIdentifier(deviceId).toStdString();
}

std::optional<AudioTarget> resolvePulseTarget(std::string_view target, coreaudio::AudioDirection direction) {
	if (target == "@DEFAULT_SINK@" || target == "@DEFAULT_SOURCE@") {
		auto deviceId = coreaudio::defaultDevice(direction);
		if (deviceId == kAudioObjectUnknown) return std::nullopt;
		return AudioTarget {deviceId, direction};
	}
	for (auto deviceId: coreaudio::deviceIdentifiers()) {
		if (!coreaudio::deviceHasStreams(deviceId, direction)) continue;
		if (deviceNameFor(deviceId) == target || std::to_string(deviceId) == target) {
			return AudioTarget {deviceId, direction};
		}
	}
	return std::nullopt;
}

std::string pulseVolumeDescription(double volume) {
	auto rawVolume = static_cast<long>(std::lround(volume * PULSE_NORMAL_VOLUME));
	auto percent = static_cast<int>(std::lround(volume * 100));
	std::array<char, 48> decibels {};
	if (volume <= 0) {
		std::snprintf(decibels.data(), decibels.size(), "-inf dB");
	} else {
		std::snprintf(decibels.data(), decibels.size(), "%.2f dB", 60.0 * std::log10(volume));
	}
	std::array<char, 96> channel {};
	std::snprintf(channel.data(), channel.size(), "%ld / %3d%% / %s", rawVolume, percent, decibels.data());
	return std::string("front-left: ") + channel.data() + ",   front-right: " + channel.data();
}

std::optional<bool> parseMuteState(std::string_view text, bool currentlyMuted) {
	if (text == "toggle") return !currentlyMuted;
	if (text == "1" || text == "yes" || text == "true" || text == "on") return true;
	if (text == "0" || text == "no" || text == "false" || text == "off") return false;
	return std::nullopt;
}

std::optional<coreaudio::AudioDirection> pulseDirectionFor(std::string_view noun) {
	if (noun == "sink" || noun == "sinks") return coreaudio::AudioDirection::Output;
	if (noun == "source" || noun == "sources") return coreaudio::AudioDirection::Input;
	return std::nullopt;
}

void printPulseDevices(coreaudio::AudioDirection direction, bool shortForm) {
	auto defaultDeviceId = coreaudio::defaultDevice(direction);
	const char* kind = direction == coreaudio::AudioDirection::Output ? "Sink" : "Source";
	for (auto deviceId: coreaudio::deviceIdentifiers()) {
		if (!coreaudio::deviceHasStreams(deviceId, direction) || coreaudio::isDeviceHidden(deviceId)) continue;
		auto name = deviceNameFor(deviceId);
		const char* state = deviceId == defaultDeviceId ? "RUNNING" : "SUSPENDED";
		if (shortForm) {
			std::printf("%u\t%s\tCoreAudio\tfloat32le 2ch 48000Hz\t%s\n", deviceId, name.c_str(), state);
			continue;
		}
		auto volume = coreaudio::mainVolume(deviceId, direction).value_or(1);
		bool muted = coreaudio::isMuted(deviceId, direction).value_or(false);
		std::printf(
		    "%s #%u\n\tState: %s\n\tName: %s\n\tDescription: %s\n\tDriver: CoreAudio\n\tMute: %s\n\tVolume: %s\n\n",
		    kind,
		    deviceId,
		    state,
		    name.c_str(),
		    coreaudio::deviceName(deviceId).toStdString().c_str(),
		    muted ? "yes" : "no",
		    pulseVolumeDescription(volume).c_str()
		);
	}
}

int runPulseSubscribe() {
	auto reportChange = [](const char* facility, AudioObjectID objectId) {
		std::printf("Event 'change' on %s #%u\n", facility, objectId);
		std::fflush(stdout);
	};

	std::vector<std::unique_ptr<coreaudio::PropertyListener>> listeners;
	listeners.push_back(std::make_unique<coreaudio::PropertyListener>(
	    kAudioObjectSystemObject,
	    kAudioHardwarePropertyDefaultOutputDevice,
	    kAudioObjectPropertyScopeGlobal,
	    kAudioObjectPropertyElementMain,
	    [reportChange] { reportChange("server", 0); }
	));
	listeners.push_back(std::make_unique<coreaudio::PropertyListener>(
	    kAudioObjectSystemObject,
	    kAudioHardwarePropertyDefaultInputDevice,
	    kAudioObjectPropertyScopeGlobal,
	    kAudioObjectPropertyElementMain,
	    [reportChange] { reportChange("server", 0); }
	));
	for (auto deviceId: coreaudio::deviceIdentifiers()) {
		for (auto direction: {coreaudio::AudioDirection::Output, coreaudio::AudioDirection::Input}) {
			if (!coreaudio::deviceHasStreams(deviceId, direction)) continue;
			const char* facility = direction == coreaudio::AudioDirection::Output ? "sink" : "source";
			for (auto selector: {coreaudio::virtualMainVolumeSelector(), AudioObjectPropertySelector {kAudioDevicePropertyMute}}) {
				listeners.push_back(std::make_unique<coreaudio::PropertyListener>(
				    deviceId,
				    selector,
				    coreaudio::scopeFor(direction),
				    kAudioObjectPropertyElementMain,
				    [reportChange, facility, deviceId] { reportChange(facility, deviceId); }
				));
			}
		}
	}
	while (true) pause();
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
		auto volume = coreaudio::mainVolume(target->deviceId, target->direction).value_or(1);
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

		double current = coreaudio::mainVolume(target->deviceId, target->direction).value_or(1);
		double volume = std::max(0.0, applyValueChange(current, *change, 1.0));
		if (limit >= 0) volume = std::min(volume, limit);
		volume = std::min(volume, 1.0);
		return coreaudio::setMainVolume(target->deviceId, target->direction, static_cast<float>(volume)) ? 0 : 1;
	}

	return printWpctlUsage();
}

int runPactl(int argc, char** argv) {
	std::vector<std::string_view> arguments;
	for (int index = 1; index < argc; index++) {
		std::string_view argument = argv[index];
		if (argument.starts_with("--") || (argument.size() == 2 && argument[0] == '-' && std::isalpha(argument[1]))) {
			continue;
		}
		arguments.push_back(argument);
	}
	if (arguments.empty()) {
		std::fputs("pactl: no command specified\n", stderr);
		return 1;
	}

	auto command = arguments[0];
	auto argumentAt = [&arguments](size_t index) {
		return index < arguments.size() ? arguments[index] : std::string_view();
	};

	if (command == "info") {
		auto defaultSink = coreaudio::defaultDevice(coreaudio::AudioDirection::Output);
		auto defaultSource = coreaudio::defaultDevice(coreaudio::AudioDirection::Input);
		std::printf(
		    "Server Name: MacShell CoreAudio\nDefault Sample Specification: float32le 2ch 48000Hz\nDefault Sink: %s\nDefault Source: %s\n",
		    deviceNameFor(defaultSink).c_str(),
		    deviceNameFor(defaultSource).c_str()
		);
		return 0;
	}
	if (command == "get-default-sink" || command == "get-default-source") {
		auto direction = command == "get-default-sink" ? coreaudio::AudioDirection::Output : coreaudio::AudioDirection::Input;
		std::puts(deviceNameFor(coreaudio::defaultDevice(direction)).c_str());
		return 0;
	}
	if (command == "set-default-sink" || command == "set-default-source") {
		auto direction = command == "set-default-sink" ? coreaudio::AudioDirection::Output : coreaudio::AudioDirection::Input;
		auto target = resolvePulseTarget(argumentAt(1), direction);
		if (!target) {
			std::fputs("Failure: No such entity\n", stderr);
			return 1;
		}
		return coreaudio::setDefaultDevice(direction, target->deviceId) ? 0 : 1;
	}
	if (command == "list") {
		bool shortForm = argumentAt(1) == "short";
		auto noun = shortForm ? argumentAt(2) : argumentAt(1);
		auto direction = pulseDirectionFor(noun);
		if (noun.empty()) {
			printPulseDevices(coreaudio::AudioDirection::Output, shortForm);
			printPulseDevices(coreaudio::AudioDirection::Input, shortForm);
			return 0;
		}
		if (direction) printPulseDevices(*direction, shortForm);
		return 0;
	}
	if (command == "subscribe") return runPulseSubscribe();

	constexpr std::string_view volumeCommandSuffix = "-volume";
	constexpr std::string_view muteCommandSuffix = "-mute";
	for (std::string_view noun: {"sink", "source"}) {
		auto direction = *pulseDirectionFor(noun);
		std::string getVolume = "get-" + std::string(noun) + std::string(volumeCommandSuffix);
		std::string setVolume = "set-" + std::string(noun) + std::string(volumeCommandSuffix);
		std::string getMute = "get-" + std::string(noun) + std::string(muteCommandSuffix);
		std::string setMute = "set-" + std::string(noun) + std::string(muteCommandSuffix);
		if (command != getVolume && command != setVolume && command != getMute && command != setMute) continue;

		auto target = resolvePulseTarget(argumentAt(1), direction);
		if (!target) {
			std::fputs("Failure: No such entity\n", stderr);
			return 1;
		}
		auto volume = coreaudio::mainVolume(target->deviceId, direction).value_or(1);
		bool muted = coreaudio::isMuted(target->deviceId, direction).value_or(false);

		if (command == getVolume) {
			std::printf("Volume: %s\n        balance 0.00\n", pulseVolumeDescription(volume).c_str());
			return 0;
		}
		if (command == getMute) {
			std::printf("Mute: %s\n", muted ? "yes" : "no");
			return 0;
		}
		if (command == setMute) {
			auto requested = parseMuteState(argumentAt(2), muted);
			if (!requested) return 1;
			return coreaudio::setMuted(target->deviceId, direction, *requested) ? 0 : 1;
		}

		auto change = parseValueChange(argumentAt(2));
		if (!change) {
			std::fputs("Failed to parse volume\n", stderr);
			return 1;
		}
		double scale = change->isPercent ? 1.0 : PULSE_NORMAL_VOLUME;
		double requested = change->isPercent ? applyValueChange(volume, *change, 1.0)
		                                     : applyValueChange(volume * scale, *change, scale) / scale;
		requested = std::clamp(requested, 0.0, 1.0);
		return coreaudio::setMainVolume(target->deviceId, direction, static_cast<float>(requested)) ? 0 : 1;
	}

	std::fprintf(stderr, "pactl: '%.*s' is not available on macOS\n", static_cast<int>(command.size()), command.data());
	return 1;
}

}

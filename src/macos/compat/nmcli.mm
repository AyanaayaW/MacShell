#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <dispatch/dispatch.h>
#include <Network/Network.h>

#include "commands.hpp"

extern "C" {
#import <CoreWLAN/CoreWLAN.h>
}
#import <Foundation/Foundation.h>

@interface MacShellNmcliMonitor: NSObject <CWEventDelegate>
@property(nonatomic, copy) NSString* interfaceName;
@end

@implementation MacShellNmcliMonitor

- (void)report:(NSString*)line {
	std::printf("%s\n", line.UTF8String);
	std::fflush(stdout);
}

- (void)linkDidChangeForWiFiInterfaceWithName:(NSString*)interfaceName {
	CWInterface* interface = [CWWiFiClient.sharedWiFiClient interfaceWithName:interfaceName];
	[self report:[NSString stringWithFormat:@"%@: %@", interfaceName, interface.ssid || interface.transmitRate > 0 ? @"connected" : @"disconnected"]];
}

- (void)powerStateDidChangeForWiFiInterfaceWithName:(NSString*)interfaceName {
	CWInterface* interface = [CWWiFiClient.sharedWiFiClient interfaceWithName:interfaceName];
	[self report:[NSString stringWithFormat:@"%@: %@", interfaceName, interface.powerOn ? @"disconnected" : @"unavailable"]];
}

- (void)ssidDidChangeForWiFiInterfaceWithName:(NSString*)interfaceName {
	[self report:[NSString stringWithFormat:@"%@: using connection '%@'", interfaceName, [CWWiFiClient.sharedWiFiClient interfaceWithName:interfaceName].ssid ?: @""]];
}

- (void)scanCacheUpdatedForWiFiInterfaceWithName:(NSString*)interfaceName {
	[self report:[NSString stringWithFormat:@"%@: access points changed", interfaceName]];
}

@end

namespace qs::macos::compat {

namespace {

constexpr int EXIT_INVALID_ARGUMENTS = 2;
constexpr int EXIT_OPERATION_FAILED = 4;
constexpr int EXIT_NOT_FOUND = 10;
constexpr int64_t CONNECTIVITY_PROBE_NANOSECONDS = 1'500'000'000;

using Row = std::map<std::string, std::string>;

struct OutputOptions {
	bool terse = false;
	bool valuesOnly = false;
	std::vector<std::string> fields;
};

struct Table {
	std::vector<std::string> defaultFields;
	std::vector<std::string> availableFields;
	std::vector<Row> rows;
};

bool startsWithAbbreviation(std::string_view argument, std::string_view fullWord) {
	return !argument.empty() && argument.size() <= fullWord.size() && fullWord.starts_with(argument);
}

std::vector<std::string> splitFields(std::string_view text) {
	std::vector<std::string> fields;
	while (!text.empty()) {
		auto separator = text.find(',');
		std::string field(text.substr(0, separator));
		std::transform(field.begin(), field.end(), field.begin(), [](unsigned char character) {
			return static_cast<char>(std::toupper(character));
		});
		if (!field.empty()) fields.push_back(field);
		if (separator == std::string_view::npos) break;
		text.remove_prefix(separator + 1);
	}
	return fields;
}

std::string escapeTerseValue(const std::string& value) {
	std::string escaped;
	for (char character: value) {
		if (character == ':' || character == '\\') escaped += '\\';
		escaped += character;
	}
	return escaped;
}

int printTable(const Table& table, const OutputOptions& options) {
	std::vector<std::string> fields = options.fields;
	if (fields.empty() || (fields.size() == 1 && (fields[0] == "COMMON" || fields[0] == "ALL"))) {
		fields = !fields.empty() && fields[0] == "ALL" ? table.availableFields : table.defaultFields;
	}
	for (const auto& field: fields) {
		if (std::find(table.availableFields.begin(), table.availableFields.end(), field) == table.availableFields.end()) {
			std::fprintf(stderr, "Error: invalid field '%s'; allowed fields:", field.c_str());
			for (const auto& available: table.availableFields) std::fprintf(stderr, " %s", available.c_str());
			std::fputc('\n', stderr);
			return EXIT_INVALID_ARGUMENTS;
		}
	}

	auto valueOf = [](const Row& row, const std::string& field) {
		auto found = row.find(field);
		return found == row.end() ? std::string("--") : found->second;
	};

	if (options.terse || options.valuesOnly) {
		for (const auto& row: table.rows) {
			for (size_t index = 0; index < fields.size(); index++) {
				if (index > 0) std::fputc(':', stdout);
				auto value = valueOf(row, fields[index]);
				std::fputs((fields.size() > 1 ? escapeTerseValue(value) : value).c_str(), stdout);
			}
			std::fputc('\n', stdout);
		}
		return 0;
	}

	std::vector<size_t> widths;
	for (const auto& field: fields) {
		size_t width = field.size();
		for (const auto& row: table.rows) width = std::max(width, valueOf(row, field).size());
		widths.push_back(width);
	}
	auto printLine = [&](const std::function<std::string(size_t)>& cellAt) {
		for (size_t index = 0; index < fields.size(); index++) {
			auto cell = cellAt(index);
			std::fputs(cell.c_str(), stdout);
			if (index + 1 < fields.size()) std::fputs(std::string(widths[index] - cell.size() + 2, ' ').c_str(), stdout);
		}
		std::fputc('\n', stdout);
	};
	printLine([&](size_t index) { return fields[index]; });
	for (const auto& row: table.rows) printLine([&](size_t index) { return valueOf(row, fields[index]); });
	return 0;
}

std::string utf8(NSString* text) { return text ? std::string(text.UTF8String) : std::string(); }

CWInterface* wifiInterface() { return CWWiFiClient.sharedWiFiClient.interface; }

bool isAssociated(CWInterface* interface) {
	return interface.powerOn && (interface.ssid != nil || interface.transmitRate > 0);
}

std::string stableUuidFor(const std::string& name) {
	uint64_t firstHalf = 0xcbf29ce484222325ULL;
	uint64_t secondHalf = 0x84222325cbf29ce4ULL;
	for (unsigned char character: name) {
		firstHalf = (firstHalf ^ character) * 0x100000001b3ULL;
		secondHalf = (secondHalf ^ character) * 0x100000001b3ULL + 0x9e3779b97f4a7c15ULL;
	}
	char formatted[37];
	std::snprintf(
	    formatted,
	    sizeof(formatted),
	    "%08x-%04x-%04x-%04x-%012llx",
	    static_cast<unsigned>(firstHalf >> 32),
	    static_cast<unsigned>((firstHalf >> 16) & 0xffff),
	    static_cast<unsigned>(0x5000 | (firstHalf & 0x0fff)),
	    static_cast<unsigned>(0x8000 | ((secondHalf >> 48) & 0x3fff)),
	    static_cast<unsigned long long>(secondHalf & 0xffffffffffffULL)
	);
	return formatted;
}

std::string securityLabel(CWNetwork* network) {
	if ([network supportsSecurity:kCWSecurityWPA3Enterprise] || [network supportsSecurity:kCWSecurityWPA2Enterprise]
	    || [network supportsSecurity:kCWSecurityWPAEnterprise])
	{
		return "WPA2 802.1X";
	}
	if ([network supportsSecurity:kCWSecurityWPA3Personal]) return "WPA3";
	if ([network supportsSecurity:kCWSecurityWPA3Transition]) return "WPA2 WPA3";
	if ([network supportsSecurity:kCWSecurityWPA2Personal]) return "WPA2";
	if ([network supportsSecurity:kCWSecurityWPAPersonal]) return "WPA1";
	if ([network supportsSecurity:kCWSecurityWEP] || [network supportsSecurity:kCWSecurityDynamicWEP]) return "WEP";
	if ([network supportsSecurity:kCWSecurityOWE] || [network supportsSecurity:kCWSecurityOWETransition]) return "OWE";
	return "";
}

int frequencyForChannel(CWChannel* channel) {
	auto number = static_cast<int>(channel.channelNumber);
	switch (channel.channelBand) {
	case kCWChannelBand2GHz: return number == 14 ? 2484 : 2407 + number * 5;
	case kCWChannelBand5GHz: return 5000 + number * 5;
	case kCWChannelBand6GHz: return 5950 + number * 5;
	default: return 0;
	}
}

int signalPercent(NSInteger rssi) {
	return std::clamp(static_cast<int>(2 * (rssi + 100)), 0, 100);
}

std::string signalBars(int percent) {
	if (percent > 80) return "▂▄▆█";
	if (percent > 55) return "▂▄▆_";
	if (percent > 30) return "▂▄__";
	if (percent > 5) return "▂___";
	return "____";
}

std::string connectivityState() {
	__block std::string connectivity = "unknown";
	dispatch_semaphore_t resolved = dispatch_semaphore_create(0);
	nw_path_monitor_t monitor = nw_path_monitor_create();
	nw_path_monitor_set_queue(monitor, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
	nw_path_monitor_set_update_handler(monitor, ^(nw_path_t path) {
		switch (nw_path_get_status(path)) {
		case nw_path_status_satisfied: connectivity = "full"; break;
		case nw_path_status_satisfiable: connectivity = "limited"; break;
		default: connectivity = "none"; break;
		}
		dispatch_semaphore_signal(resolved);
	});
	nw_path_monitor_start(monitor);
	dispatch_semaphore_wait(resolved, dispatch_time(DISPATCH_TIME_NOW, CONNECTIVITY_PROBE_NANOSECONDS));
	nw_path_monitor_cancel(monitor);
	return connectivity;
}

std::string deviceState(CWInterface* interface) {
	if (!interface.powerOn) return "unavailable";
	return isAssociated(interface) ? "connected" : "disconnected";
}

std::vector<CWNetwork*> scannedNetworks(CWInterface* interface, bool rescan) {
	NSSet<CWNetwork*>* networks = interface.cachedScanResults;
	if (rescan || networks.count == 0) {
		NSError* error = nil;
		NSSet<CWNetwork*>* fresh = [interface scanForNetworksWithName:nil error:&error];
		if (fresh) networks = fresh;
	}

	std::map<std::string, CWNetwork*> strongestBySsid;
	for (CWNetwork* network in networks) {
		auto ssid = utf8(network.ssid);
		auto& strongest = strongestBySsid[ssid.empty() ? utf8(network.bssid) : ssid];
		if (!strongest || network.rssiValue > strongest.rssiValue) strongest = network;
	}

	std::vector<CWNetwork*> sorted;
	for (const auto& [key, network]: strongestBySsid) sorted.push_back(network);
	std::sort(sorted.begin(), sorted.end(), [](CWNetwork* left, CWNetwork* right) {
		return left.rssiValue > right.rssiValue;
	});
	return sorted;
}

std::optional<std::string> keychainPassword(const std::string& ssid) {
	NSString* password = nil;
	NSData* ssidData = [@(ssid.c_str()) dataUsingEncoding:NSUTF8StringEncoding];
	if (CWKeychainFindWiFiPassword(kCWKeychainDomainSystem, ssidData, &password) != errSecSuccess || !password) {
		return std::nullopt;
	}
	return utf8(password);
}

int connectToSsid(const std::string& ssid, std::optional<std::string> password) {
	CWInterface* interface = wifiInterface();
	if (!interface) {
		std::fputs("Error: No Wi-Fi device found.\n", stderr);
		return EXIT_NOT_FOUND;
	}

	NSError* error = nil;
	NSSet<CWNetwork*>* matches = [interface scanForNetworksWithName:@(ssid.c_str()) error:&error];
	CWNetwork* target = matches.anyObject;
	if (!target) {
		std::fprintf(stderr, "Error: No network with SSID '%s' found.\n", ssid.c_str());
		return EXIT_NOT_FOUND;
	}

	bool isOpen = [target supportsSecurity:kCWSecurityNone];
	if (!password && !isOpen) password = keychainPassword(ssid);
	if (!password && !isOpen) {
		std::fputs("Error: Connection activation failed: Secrets were required, but not provided.\n", stderr);
		return EXIT_OPERATION_FAILED;
	}

	NSString* nativePassword = password ? @(password->c_str()) : nil;
	if (![interface associateToNetwork:target password:nativePassword error:&error]) {
		std::fprintf(stderr, "Error: Connection activation failed: %s\n", utf8(error.localizedDescription).c_str());
		return EXIT_OPERATION_FAILED;
	}
	std::printf("Device '%s' successfully activated.\n", utf8(interface.interfaceName).c_str());
	return 0;
}

std::optional<std::string> ssidForConnectionId(CWInterface* interface, const std::string& identifier) {
	for (CWNetworkProfile* profile in interface.configuration.networkProfiles) {
		auto ssid = utf8(profile.ssid);
		if (ssid == identifier || stableUuidFor(ssid) == identifier) return ssid;
	}
	return std::nullopt;
}

int runGeneral(const std::vector<std::string_view>& arguments, const OutputOptions& options) {
	if (!arguments.empty() && !startsWithAbbreviation(arguments[0], "status")) {
		std::fprintf(stderr, "Error: 'general %.*s' is not available on macOS.\n", static_cast<int>(arguments[0].size()), arguments[0].data());
		return EXIT_INVALID_ARGUMENTS;
	}
	CWInterface* interface = wifiInterface();
	auto connectivity = connectivityState();
	Row row {
	    {"RUNNING", "running"},
	    {"STATE", connectivity == "full" || isAssociated(interface) ? "connected" : "disconnected"},
	    {"CONNECTIVITY", connectivity},
	    {"NETWORKING", "enabled"},
	    {"WIFI-HW", interface ? "enabled" : "missing"},
	    {"WIFI", interface.powerOn ? "enabled" : "disabled"},
	    {"WWAN-HW", "missing"},
	    {"WWAN", "disabled"},
	};
	return printTable(
	    {{"STATE", "CONNECTIVITY", "WIFI-HW", "WIFI", "WWAN-HW", "WWAN"},
	     {"RUNNING", "STATE", "CONNECTIVITY", "NETWORKING", "WIFI-HW", "WIFI", "WWAN-HW", "WWAN"},
	     {row}},
	    options
	);
}

int runRadio(const std::vector<std::string_view>& arguments, const OutputOptions& options) {
	CWInterface* interface = wifiInterface();
	if (arguments.empty() || startsWithAbbreviation(arguments[0], "all")) {
		Row row {
		    {"WIFI-HW", interface ? "enabled" : "missing"},
		    {"WIFI", interface.powerOn ? "enabled" : "disabled"},
		    {"WWAN-HW", "missing"},
		    {"WWAN", "disabled"},
		};
		if (arguments.size() < 2) {
			return printTable({{"WIFI-HW", "WIFI", "WWAN-HW", "WWAN"}, {"WIFI-HW", "WIFI", "WWAN-HW", "WWAN"}, {row}}, options);
		}
	} else if (!startsWithAbbreviation(arguments[0], "wifi")) {
		std::fprintf(stderr, "Error: radio '%.*s' is not available on macOS.\n", static_cast<int>(arguments[0].size()), arguments[0].data());
		return EXIT_INVALID_ARGUMENTS;
	}

	if (arguments.size() < 2) {
		std::puts(interface.powerOn ? "enabled" : "disabled");
		return 0;
	}
	if (!interface) return EXIT_NOT_FOUND;

	auto requested = arguments[1];
	if (requested != "on" && requested != "off") {
		std::fputs("Error: invalid radio switch state; use 'on' or 'off'.\n", stderr);
		return EXIT_INVALID_ARGUMENTS;
	}
	NSError* error = nil;
	if (![interface setPower:requested == "on" error:&error]) {
		std::fprintf(stderr, "Error: %s\n", utf8(error.localizedDescription).c_str());
		return EXIT_OPERATION_FAILED;
	}
	return 0;
}

int runDeviceWifi(const std::vector<std::string_view>& arguments, const OutputOptions& options) {
	CWInterface* interface = wifiInterface();
	if (!interface) {
		std::fputs("Error: No Wi-Fi device found.\n", stderr);
		return EXIT_NOT_FOUND;
	}

	if (!arguments.empty() && startsWithAbbreviation(arguments[0], "connect")) {
		if (arguments.size() < 2) {
			std::fputs("Error: SSID or BSSID are missing.\n", stderr);
			return EXIT_INVALID_ARGUMENTS;
		}
		std::optional<std::string> password;
		for (size_t index = 2; index + 1 < arguments.size(); index++) {
			if (arguments[index] == "password") password = std::string(arguments[index + 1]);
		}
		return connectToSsid(std::string(arguments[1]), password);
	}

	if (!arguments.empty() && startsWithAbbreviation(arguments[0], "rescan")) {
		scannedNetworks(interface, true);
		return 0;
	}

	if (!arguments.empty() && !startsWithAbbreviation(arguments[0], "list")) {
		std::fprintf(stderr, "Error: 'device wifi %.*s' is not available on macOS.\n", static_cast<int>(arguments[0].size()), arguments[0].data());
		return EXIT_INVALID_ARGUMENTS;
	}

	bool rescan = false;
	for (size_t index = 0; index + 1 < arguments.size(); index++) {
		if (arguments[index] == "--rescan") rescan = arguments[index + 1] == "yes";
	}

	auto activeSsid = utf8(interface.ssid);
	auto activeBssid = utf8(interface.bssid);
	auto interfaceName = utf8(interface.interfaceName);
	Table table {
	    {"IN-USE", "BSSID", "SSID", "MODE", "CHAN", "RATE", "SIGNAL", "BARS", "SECURITY"},
	    {"NAME", "SSID", "SSID-HEX", "BSSID", "MODE", "CHAN", "FREQ", "RATE", "SIGNAL", "BARS", "SECURITY", "DEVICE", "ACTIVE", "IN-USE"},
	    {}};

	for (CWNetwork* network: scannedNetworks(interface, rescan)) {
		auto ssid = utf8(network.ssid);
		bool active = !ssid.empty() && ssid == activeSsid;
		int signal = signalPercent(network.rssiValue);
		std::string ssidHex;
		for (unsigned char character: ssid) {
			char pair[3];
			std::snprintf(pair, sizeof(pair), "%02X", character);
			ssidHex += pair;
		}
		table.rows.push_back({
		    {"NAME", "AP"},
		    {"SSID", ssid.empty() ? "--" : ssid},
		    {"SSID-HEX", ssidHex.empty() ? "--" : ssidHex},
		    {"BSSID", active && network.bssid == nil ? activeBssid : utf8(network.bssid)},
		    {"MODE", network.ibss ? "Ad-Hoc" : "Infra"},
		    {"CHAN", std::to_string(network.wlanChannel.channelNumber)},
		    {"FREQ", std::to_string(frequencyForChannel(network.wlanChannel)) + " MHz"},
		    {"RATE", active ? std::to_string(static_cast<int>(interface.transmitRate)) + " Mbit/s" : "--"},
		    {"SIGNAL", std::to_string(signal)},
		    {"BARS", signalBars(signal)},
		    {"SECURITY", securityLabel(network)},
		    {"DEVICE", interfaceName},
		    {"ACTIVE", active ? "yes" : "no"},
		    {"IN-USE", active ? "*" : " "},
		});
	}
	return printTable(table, options);
}

int runDevice(const std::vector<std::string_view>& arguments, const OutputOptions& options) {
	if (!arguments.empty() && startsWithAbbreviation(arguments[0], "wifi")) {
		return runDeviceWifi({arguments.begin() + 1, arguments.end()}, options);
	}

	CWInterface* interface = wifiInterface();
	if (!arguments.empty() && startsWithAbbreviation(arguments[0], "disconnect")) {
		if (!interface) return EXIT_NOT_FOUND;
		[interface disassociate];
		std::printf("Device '%s' successfully disconnected.\n", utf8(interface.interfaceName).c_str());
		return 0;
	}
	if (!arguments.empty() && !startsWithAbbreviation(arguments[0], "status")) {
		std::fprintf(stderr, "Error: 'device %.*s' is not available on macOS.\n", static_cast<int>(arguments[0].size()), arguments[0].data());
		return EXIT_INVALID_ARGUMENTS;
	}

	Table table {
	    {"DEVICE", "TYPE", "STATE", "CONNECTION"},
	    {"DEVICE", "TYPE", "STATE", "IP4-CONNECTIVITY", "IP6-CONNECTIVITY", "DBUS-PATH", "CONNECTION", "CON-UUID", "CON-PATH"},
	    {}};
	if (interface) {
		auto ssid = utf8(interface.ssid);
		bool associated = isAssociated(interface);
		auto connectivity = associated ? connectivityState() : std::string("none");
		table.rows.push_back({
		    {"DEVICE", utf8(interface.interfaceName)},
		    {"TYPE", "wifi"},
		    {"STATE", deviceState(interface)},
		    {"IP4-CONNECTIVITY", connectivity},
		    {"IP6-CONNECTIVITY", connectivity},
		    {"CONNECTION", associated && !ssid.empty() ? ssid : "--"},
		    {"CON-UUID", associated && !ssid.empty() ? stableUuidFor(ssid) : "--"},
		});
	}
	table.rows.push_back({{"DEVICE", "lo0"}, {"TYPE", "loopback"}, {"STATE", "connected (externally)"}, {"CONNECTION", "lo0"}});
	return printTable(table, options);
}

int runConnection(const std::vector<std::string_view>& arguments, const OutputOptions& options) {
	CWInterface* interface = wifiInterface();
	std::string_view verb = arguments.empty() ? "show" : arguments[0];
	std::string identifier;
	for (size_t index = 1; index < arguments.size(); index++) {
		auto argument = arguments[index];
		if (argument == "id" || argument == "uuid" || argument.starts_with("--")) continue;
		identifier = argument;
		break;
	}

	if (startsWithAbbreviation(verb, "show")) {
		bool activeOnly = std::find(arguments.begin(), arguments.end(), "--active") != arguments.end();
		auto activeSsid = isAssociated(interface) ? utf8(interface.ssid) : std::string();
		Table table {
		    {"NAME", "UUID", "TYPE", "DEVICE"},
		    {"NAME", "UUID", "TYPE", "TIMESTAMP", "AUTOCONNECT", "READONLY", "DEVICE", "ACTIVE", "STATE"},
		    {}};
		for (CWNetworkProfile* profile in interface.configuration.networkProfiles) {
			auto ssid = utf8(profile.ssid);
			bool active = !ssid.empty() && ssid == activeSsid;
			if (ssid.empty() || (activeOnly && !active)) continue;
			if (!identifier.empty() && identifier != ssid && identifier != stableUuidFor(ssid)) continue;
			table.rows.push_back({
			    {"NAME", ssid},
			    {"UUID", stableUuidFor(ssid)},
			    {"TYPE", "802-11-wireless"},
			    {"TIMESTAMP", "0"},
			    {"AUTOCONNECT", "yes"},
			    {"READONLY", "no"},
			    {"DEVICE", active ? utf8(interface.interfaceName) : "--"},
			    {"ACTIVE", active ? "yes" : "no"},
			    {"STATE", active ? "activated" : "--"},
			});
		}
		std::stable_sort(table.rows.begin(), table.rows.end(), [](const Row& left, const Row& right) {
			return left.at("ACTIVE") > right.at("ACTIVE");
		});
		return printTable(table, options);
	}

	if (identifier.empty()) {
		std::fputs("Error: connection identifier is missing.\n", stderr);
		return EXIT_INVALID_ARGUMENTS;
	}
	auto ssid = ssidForConnectionId(interface, identifier).value_or(identifier);

	if (startsWithAbbreviation(verb, "up")) return connectToSsid(ssid, std::nullopt);
	if (startsWithAbbreviation(verb, "down")) {
		if (!isAssociated(interface) || utf8(interface.ssid) != ssid) {
			std::fprintf(stderr, "Error: '%s' is not an active connection.\n", identifier.c_str());
			return EXIT_NOT_FOUND;
		}
		[interface disassociate];
		std::printf("Connection '%s' successfully deactivated.\n", ssid.c_str());
		return 0;
	}
	if (startsWithAbbreviation(verb, "delete")) {
		NSMutableOrderedSet<CWNetworkProfile*>* profiles = [interface.configuration.networkProfiles mutableCopy];
		NSIndexSet* matching = [profiles indexesOfObjectsPassingTest:^BOOL(CWNetworkProfile* profile, NSUInteger, BOOL*) {
			return utf8(profile.ssid) == ssid;
		}];
		if (matching.count == 0) {
			std::fprintf(stderr, "Error: unknown connection '%s'.\n", identifier.c_str());
			return EXIT_NOT_FOUND;
		}
		[profiles removeObjectsAtIndexes:matching];
		CWMutableConfiguration* configuration = [CWMutableConfiguration configurationWithConfiguration:interface.configuration];
		configuration.networkProfiles = profiles;
		NSError* error = nil;
		if (![interface commitConfiguration:configuration authorization:nil error:&error]) {
			std::fprintf(stderr, "Error: %s\n", utf8(error.localizedDescription).c_str());
			return EXIT_OPERATION_FAILED;
		}
		std::printf("Connection '%s' (%s) successfully deleted.\n", ssid.c_str(), stableUuidFor(ssid).c_str());
		return 0;
	}

	std::fprintf(stderr, "Error: 'connection %.*s' is not available on macOS.\n", static_cast<int>(verb.size()), verb.data());
	return EXIT_INVALID_ARGUMENTS;
}

int runMonitor() {
	MacShellNmcliMonitor* monitor = [MacShellNmcliMonitor new];
	CWWiFiClient* client = CWWiFiClient.sharedWiFiClient;
	client.delegate = monitor;
	NSError* error = nil;
	for (auto eventType: {CWEventTypePowerDidChange, CWEventTypeSSIDDidChange, CWEventTypeLinkDidChange, CWEventTypeScanCacheUpdated}) {
		[client startMonitoringEventWithType:eventType error:&error];
	}
	[NSRunLoop.currentRunLoop run];
	return 0;
}

}

int runNmcli(int argc, char** argv) {
	@autoreleasepool {
		OutputOptions options;
		std::vector<std::string_view> arguments;
		for (int index = 1; index < argc; index++) {
			std::string_view argument = argv[index];
			if (argument == "-t" || argument == "--terse") {
				options.terse = true;
			} else if ((argument == "-f" || argument == "--fields") && index + 1 < argc) {
				options.fields = splitFields(argv[++index]);
			} else if ((argument == "-g" || argument == "--get-values") && index + 1 < argc) {
				options.fields = splitFields(argv[++index]);
				options.valuesOnly = true;
			} else if ((argument == "-c" || argument == "--colors" || argument == "-e" || argument == "--escape"
			            || argument == "-w" || argument == "--wait")
			           && index + 1 < argc)
			{
				index++;
			} else if (arguments.empty() && argument.starts_with('-') && argument != "--rescan") {
				continue;
			} else {
				arguments.push_back(argument);
			}
		}

		std::string_view object = arguments.empty() ? "general" : arguments[0];
		std::vector<std::string_view> rest = arguments.empty() ? std::vector<std::string_view> {}
		                                                       : std::vector<std::string_view>(arguments.begin() + 1, arguments.end());

		if (startsWithAbbreviation(object, "general")) return runGeneral(rest, options);
		if (startsWithAbbreviation(object, "radio")) return runRadio(rest, options);
		if (startsWithAbbreviation(object, "device")) return runDevice(rest, options);
		if (startsWithAbbreviation(object, "connection")) return runConnection(rest, options);
		if (startsWithAbbreviation(object, "monitor")) return runMonitor();
		if (startsWithAbbreviation(object, "networking")) {
			if (!rest.empty() && startsWithAbbreviation(rest[0], "connectivity")) {
				std::puts(connectivityState().c_str());
			} else {
				std::puts("enabled");
			}
			return 0;
		}

		std::fprintf(stderr, "Error: argument '%.*s' not understood.\n", static_cast<int>(object.size()), object.data());
		return EXIT_INVALID_ARGUMENTS;
	}
}

}

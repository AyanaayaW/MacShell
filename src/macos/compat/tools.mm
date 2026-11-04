#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "commands.hpp"

#import <AppKit/AppKit.h>

extern char** environ;

namespace qs::macos::compat {

namespace {

constexpr useconds_t CLIPBOARD_POLL_MICROSECONDS = 250000;
constexpr NSUInteger CLIPHIST_MAXIMUM_ENTRIES = 750;
constexpr NSUInteger CLIPHIST_PREVIEW_CHARACTERS = 100;
constexpr NSUInteger CLIPHIST_MAXIMUM_ENTRY_BYTES = 5 * 1024 * 1024;

NSString* cliphistDirectory() {
	NSString* cacheHome = NSProcessInfo.processInfo.environment[@"XDG_CACHE_HOME"];
	if (cacheHome.length == 0) cacheHome = [NSHomeDirectory() stringByAppendingPathComponent:@".cache"];
	NSString* directory = [cacheHome stringByAppendingPathComponent:@"cliphist/macshell"];
	[NSFileManager.defaultManager createDirectoryAtPath:directory
	                        withIntermediateDirectories:YES
	                                         attributes:@{NSFilePosixPermissions: @0700}
	                                              error:nil];
	return directory;
}

NSArray<NSNumber*>* storedEntryIds() {
	NSMutableArray<NSNumber*>* identifiers = [NSMutableArray array];
	for (NSString* name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:cliphistDirectory() error:nil]) {
		long long identifier = name.longLongValue;
		if (identifier > 0) [identifiers addObject:@(identifier)];
	}
	return [identifiers sortedArrayUsingSelector:@selector(compare:)];
}

NSString* entryPath(NSNumber* identifier) {
	return [cliphistDirectory() stringByAppendingPathComponent:identifier.stringValue];
}

NSString* imageDescription(NSData* data) {
	NSBitmapImageRep* image = [NSBitmapImageRep imageRepWithData:data];
	if (!image) return nil;
	const auto* bytes = static_cast<const unsigned char*>(data.bytes);
	NSString* format = data.length > 2 && bytes[0] == 0xFF && bytes[1] == 0xD8 ? @"jpeg" : @"png";
	return [NSString stringWithFormat:@"[[ binary data %lu KiB %@ %ldx%ld ]]",
	                                  static_cast<unsigned long>(data.length / 1024),
	                                  format,
	                                  static_cast<long>(image.pixelsWide),
	                                  static_cast<long>(image.pixelsHigh)];
}

NSString* previewFor(NSData* data) {
	if (NSString* image = imageDescription(data)) return image;
	NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"";
	NSArray* words = [text componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
	NSString* collapsed = [[words filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"length > 0"]]
	    componentsJoinedByString:@" "];
	return collapsed.length > CLIPHIST_PREVIEW_CHARACTERS ? [collapsed substringToIndex:CLIPHIST_PREVIEW_CHARACTERS]
	                                                       : collapsed;
}

NSNumber* identifierFromLine(NSString* line) {
	NSString* firstField = [line componentsSeparatedByString:@"\t"].firstObject;
	long long identifier =
	    [firstField stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].longLongValue;
	return identifier > 0 ? @(identifier) : nil;
}

NSString* lineArgumentOrStandardInput(int argc, char** argv, int firstArgumentIndex) {
	if (argc > firstArgumentIndex) return @(argv[firstArgumentIndex]);
	NSData* input = [NSFileHandle.fileHandleWithStandardInput readDataToEndOfFile];
	return [[NSString alloc] initWithData:input encoding:NSUTF8StringEncoding] ?: @"";
}

NSData* currentClipboardData(NSString* requestedMime) {
	NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
	BOOL wantsImage = [requestedMime hasPrefix:@"image/"];
	if (wantsImage || requestedMime.length == 0) {
		if (NSData* png = [pasteboard dataForType:NSPasteboardTypePNG]) return png;
		if (NSData* tiff = [pasteboard dataForType:NSPasteboardTypeTIFF]) {
			NSBitmapImageRep* image = [NSBitmapImageRep imageRepWithData:tiff];
			if (NSData* converted = [image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]) {
				return converted;
			}
		}
		if (wantsImage) return nil;
	}
	return [[pasteboard stringForType:NSPasteboardTypeString] dataUsingEncoding:NSUTF8StringEncoding];
}

void runWithStandardInput(const std::vector<std::string>& command, NSData* input) {
	int pipeDescriptors[2];
	if (pipe(pipeDescriptors) != 0) return;

	posix_spawn_file_actions_t fileActions;
	posix_spawn_file_actions_init(&fileActions);
	posix_spawn_file_actions_adddup2(&fileActions, pipeDescriptors[0], STDIN_FILENO);
	posix_spawn_file_actions_addclose(&fileActions, pipeDescriptors[1]);

	std::vector<char*> arguments;
	for (const auto& argument: command) arguments.push_back(const_cast<char*>(argument.c_str()));
	arguments.push_back(nullptr);

	pid_t childProcess = 0;
	auto spawned = posix_spawnp(&childProcess, arguments[0], &fileActions, nullptr, arguments.data(), environ) == 0;
	posix_spawn_file_actions_destroy(&fileActions);
	close(pipeDescriptors[0]);

	if (spawned && input.length > 0) {
		const auto* bytes = static_cast<const char*>(input.bytes);
		size_t written = 0;
		while (written < input.length) {
			auto result = write(pipeDescriptors[1], bytes + written, input.length - written);
			if (result <= 0) break;
			written += static_cast<size_t>(result);
		}
	}
	close(pipeDescriptors[1]);
	if (spawned) waitpid(childProcess, nullptr, 0);
}

int runAndWait(const std::vector<std::string>& command) {
	std::vector<char*> arguments;
	for (const auto& argument: command) arguments.push_back(const_cast<char*>(argument.c_str()));
	arguments.push_back(nullptr);

	pid_t childProcess = 0;
	if (posix_spawn(&childProcess, arguments[0], nullptr, nullptr, arguments.data(), environ) != 0) return 1;
	int status = 0;
	waitpid(childProcess, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int printUsage(const char* usage) {
	std::fputs(usage, stderr);
	return 1;
}

void removeEntry(NSNumber* identifier) {
	[NSFileManager.defaultManager removeItemAtPath:entryPath(identifier) error:nil];
}

}

int watchClipboard(NSString* requestedMime, const std::vector<std::string>& command) {
	if (command.empty()) return printUsage("wl-paste --watch: missing command\n");
	setenv("CLIPBOARD_STATE", "data", 1);

	NSInteger lastChangeCount = -1;
	while (true) {
		@autoreleasepool {
			NSInteger changeCount = NSPasteboard.generalPasteboard.changeCount;
			if (changeCount != lastChangeCount) {
				lastChangeCount = changeCount;
				if (NSData* data = currentClipboardData(requestedMime)) runWithStandardInput(command, data);
			}
		}
		usleep(CLIPBOARD_POLL_MICROSECONDS);
	}
}

int runCliphist(int argc, char** argv) {
	@autoreleasepool {
		if (argc < 2) return printUsage("usage: cliphist <store|list|decode|delete|delete-query|wipe>\n");
		std::string_view command = argv[1];

		if (command == "store") {
			NSString* state = NSProcessInfo.processInfo.environment[@"CLIPBOARD_STATE"];
			if (state && ![state isEqualToString:@"data"]) return 0;

			NSData* data = [NSFileHandle.fileHandleWithStandardInput readDataToEndOfFile];
			if (data.length == 0 || data.length > CLIPHIST_MAXIMUM_ENTRY_BYTES) return 0;

			for (NSNumber* identifier in storedEntryIds()) {
				if ([[NSData dataWithContentsOfFile:entryPath(identifier)] isEqualToData:data]) removeEntry(identifier);
			}

			NSArray<NSNumber*>* identifiers = storedEntryIds();
			long long nextIdentifier = identifiers.count > 0 ? identifiers.lastObject.longLongValue + 1 : 1;
			[data writeToFile:entryPath(@(nextIdentifier)) atomically:YES];

			identifiers = storedEntryIds();
			if (identifiers.count > CLIPHIST_MAXIMUM_ENTRIES) {
				for (NSUInteger index = 0; index < identifiers.count - CLIPHIST_MAXIMUM_ENTRIES; ++index) {
					removeEntry(identifiers[index]);
				}
			}
			return 0;
		}

		if (command == "list") {
			for (NSNumber* identifier in storedEntryIds().reverseObjectEnumerator) {
				NSData* data = [NSData dataWithContentsOfFile:entryPath(identifier)];
				if (data) std::printf("%s\t%s\n", identifier.stringValue.UTF8String, previewFor(data).UTF8String);
			}
			return 0;
		}

		if (command == "decode") {
			NSNumber* identifier = identifierFromLine(lineArgumentOrStandardInput(argc, argv, 2));
			NSData* data = identifier ? [NSData dataWithContentsOfFile:entryPath(identifier)] : nil;
			if (!data) return printUsage("cliphist: entry not found\n");
			std::fwrite(data.bytes, 1, data.length, stdout);
			return 0;
		}

		if (command == "delete") {
			for (NSString* line in [lineArgumentOrStandardInput(argc, argv, 2) componentsSeparatedByString:@"\n"]) {
				if (NSNumber* identifier = identifierFromLine(line)) removeEntry(identifier);
			}
			return 0;
		}

		if (command == "delete-query") {
			if (argc < 3) return printUsage("usage: cliphist delete-query <text>\n");
			NSString* query = @(argv[2]);
			for (NSNumber* identifier in storedEntryIds()) {
				NSData* data = [NSData dataWithContentsOfFile:entryPath(identifier)];
				NSString* text = data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : nil;
				if ([text containsString:query]) removeEntry(identifier);
			}
			return 0;
		}

		if (command == "wipe") {
			for (NSNumber* identifier in storedEntryIds()) removeEntry(identifier);
			return 0;
		}

		return printUsage("cliphist: unknown command\n");
	}
}

int runSwww(int argc, char** argv) {
	@autoreleasepool {
		if (argc < 2) return printUsage("usage: swww <img|query|init|kill|clear-cache>\n");
		std::string_view command = argv[1];

		if (command == "init" || command == "kill" || command == "clear-cache") return 0;

		if (command == "query") {
			for (NSScreen* screen in NSScreen.screens) {
				NSURL* image = [NSWorkspace.sharedWorkspace desktopImageURLForScreen:screen];
				std::printf(
				    ": %s: %.0fx%.0f, scale: %.0f, currently displaying: image: %s\n",
				    screen.localizedName.UTF8String,
				    screen.frame.size.width * screen.backingScaleFactor,
				    screen.frame.size.height * screen.backingScaleFactor,
				    screen.backingScaleFactor,
				    image.path.UTF8String ?: ""
				);
			}
			return 0;
		}

		if (command != "img") return printUsage("swww: unsupported command on macOS\n");

		static const std::vector<std::string_view> optionsWithValues {
		    "-o", "--outputs", "-t", "--transition-type", "--transition-step", "--transition-duration",
		    "--transition-fps", "--transition-angle", "--transition-pos", "--transition-bezier",
		    "--transition-wave", "--resize", "--fill-color", "-f", "--filter",
		};

		NSString* imagePath = nil;
		NSString* outputNames = nil;
		for (int index = 2; index < argc; ++index) {
			std::string_view argument = argv[index];
			if ((argument == "-o" || argument == "--outputs") && index + 1 < argc) {
				outputNames = @(argv[++index]);
			} else if (std::ranges::find(optionsWithValues, argument) != optionsWithValues.end()) {
				++index;
			} else if (!argument.starts_with("-")) {
				imagePath = @(argv[index]).stringByExpandingTildeInPath;
			}
		}
		if (!imagePath || ![NSFileManager.defaultManager fileExistsAtPath:imagePath]) {
			return printUsage("swww: image not found\n");
		}

		NSURL* imageUrl = [NSURL fileURLWithPath:imagePath];
		bool succeeded = true;
		for (NSScreen* screen in NSScreen.screens) {
			if (outputNames && ![outputNames containsString:screen.localizedName]) continue;
			NSError* error = nil;
			succeeded = [NSWorkspace.sharedWorkspace setDesktopImageURL:imageUrl forScreen:screen options:@{} error:&error]
			         && succeeded;
		}
		return succeeded ? 0 : 1;
	}
}

int runHyprpicker(int argc, char** argv) {
	@autoreleasepool {
		bool copyToClipboard = false;
		bool lowercase = false;
		std::string format = "hex";
		for (int index = 1; index < argc; ++index) {
			std::string_view argument = argv[index];
			if (argument == "-a" || argument == "--autocopy") copyToClipboard = true;
			else if (argument == "-l" || argument == "--lowercase-hex") lowercase = true;
			else if ((argument == "-f" || argument == "--format") && index + 1 < argc) format = argv[++index];
			else if (argument.starts_with("--format=")) format = std::string(argument.substr(9));
		}

		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

		__block NSColor* pickedColor = nil;
		NSColorSampler* sampler = [[NSColorSampler alloc] init];
		[sampler showSamplerWithSelectionHandler:^(NSColor* color) {
			pickedColor = [color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
			[NSApp stop:nil];
			[NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
			                                    location:NSZeroPoint
			                               modifierFlags:0
			                                   timestamp:0
			                                windowNumber:0
			                                     context:nil
			                                     subtype:0
			                                       data1:0
			                                       data2:0]
			         atStart:YES];
		}];
		[NSApp run];

		if (!pickedColor) return 1;

		auto red = static_cast<int>(std::lround(pickedColor.redComponent * 255));
		auto green = static_cast<int>(std::lround(pickedColor.greenComponent * 255));
		auto blue = static_cast<int>(std::lround(pickedColor.blueComponent * 255));

		NSString* output = nil;
		if (format == "rgb") {
			output = [NSString stringWithFormat:@"%d %d %d", red, green, blue];
		} else if (format == "hsl" || format == "hsv") {
			CGFloat hue = 0;
			CGFloat saturation = 0;
			CGFloat brightness = 0;
			[pickedColor getHue:&hue saturation:&saturation brightness:&brightness alpha:nullptr];
			output = [NSString stringWithFormat:@"%.0f %.0f%% %.0f%%", hue * 360, saturation * 100, brightness * 100];
		} else {
			output = [NSString stringWithFormat:lowercase ? @"#%02x%02x%02x" : @"#%02X%02X%02X", red, green, blue];
		}

		std::printf("%s\n", output.UTF8String);
		if (copyToClipboard) {
			[NSPasteboard.generalPasteboard clearContents];
			[NSPasteboard.generalPasteboard setString:output forType:NSPasteboardTypeString];
		}
		return 0;
	}
}

int runGrim(int argc, char** argv) {
	@autoreleasepool {
		std::string geometry;
		std::string outputName;
		std::string destination;
		bool includeCursor = false;
		for (int index = 1; index < argc; ++index) {
			std::string_view argument = argv[index];
			if (argument == "-g" && index + 1 < argc) {
				geometry = argv[++index];
			} else if (argument == "-o" && index + 1 < argc) {
				outputName = argv[++index];
			} else if (argument == "-c") {
				includeCursor = true;
			} else if ((argument == "-t" || argument == "-q" || argument == "-s" || argument == "-l") && index + 1 < argc) {
				++index;
			} else if (!argument.starts_with("-") || argument == "-") {
				destination = argv[index];
			}
		}

		bool toStandardOutput = destination == "-";
		if (destination.empty()) {
			NSDictionary* environment = NSProcessInfo.processInfo.environment;
			NSString* directory = environment[@"GRIM_DEFAULT_DIR"] ?: environment[@"XDG_PICTURES_DIR"]
			    ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Pictures"];
			NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
			formatter.dateFormat = @"yyyyMMdd_HHmmss";
			NSString* fileName = [NSString stringWithFormat:@"%@_grim.png", [formatter stringFromDate:NSDate.date]];
			destination = [directory stringByAppendingPathComponent:fileName].UTF8String;
		}

		std::string capturePath = destination;
		if (toStandardOutput) {
			capturePath = std::string(NSTemporaryDirectory().UTF8String) + "macshell-grim-" + std::to_string(getpid())
			            + ".png";
		}

		std::vector<std::string> command {"/usr/sbin/screencapture", "-x", "-t", "png"};
		if (includeCursor) command.emplace_back("-C");
		if (!geometry.empty()) {
			int x = 0;
			int y = 0;
			int width = 0;
			int height = 0;
			if (std::sscanf(geometry.c_str(), "%d,%d %dx%d", &x, &y, &width, &height) != 4) {
				return printUsage("grim: invalid geometry\n");
			}
			command.push_back(
			    "-R" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(width) + ","
			    + std::to_string(height)
			);
		} else if (!outputName.empty()) {
			NSArray<NSScreen*>* screens = NSScreen.screens;
			for (NSUInteger index = 0; index < screens.count; ++index) {
				if (outputName == screens[index].localizedName.UTF8String) {
					command.push_back("-D" + std::to_string(index + 1));
				}
			}
		} else {
			command.emplace_back("-m");
		}
		command.push_back(capturePath);

		if (runAndWait(command) != 0) return 1;

		if (toStandardOutput) {
			NSData* image = [NSData dataWithContentsOfFile:@(capturePath.c_str())];
			[NSFileManager.defaultManager removeItemAtPath:@(capturePath.c_str()) error:nil];
			if (!image) return 1;
			std::fwrite(image.bytes, 1, image.length, stdout);
		}
		return 0;
	}
}

}

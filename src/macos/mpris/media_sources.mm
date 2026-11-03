#include "media_sources.hpp"
#include <algorithm>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <utility>

#include <qcryptographichash.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qstring.h>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <ScriptingBridge/ScriptingBridge.h>

namespace qs::service::mpris::macos {

namespace {

constexpr auto SPOTIFY_BUNDLE_IDENTIFIER = "com.spotify.client";
constexpr auto MUSIC_BUNDLE_IDENTIFIER = "com.apple.Music";
constexpr auto MEDIA_REMOTE_SOURCE_IDENTIFIER = "mediaremote";

constexpr FourCharCode PLAYER_STATE_STOPPED = 'kPSS';
constexpr FourCharCode PLAYER_STATE_PLAYING = 'kPSP';
constexpr FourCharCode PLAYER_STATE_PAUSED = 'kPSp';
constexpr FourCharCode MUSIC_REPEAT_ONE = 'kRp1';
constexpr FourCharCode MUSIC_REPEAT_ALL = 'kAll';
constexpr FourCharCode MUSIC_REPEAT_OFF = 'kRpO';

constexpr int MEDIA_REMOTE_COMMAND_PLAY = 0;
constexpr int MEDIA_REMOTE_COMMAND_PAUSE = 1;
constexpr int MEDIA_REMOTE_COMMAND_TOGGLE = 2;
constexpr int MEDIA_REMOTE_COMMAND_STOP = 3;
constexpr int MEDIA_REMOTE_COMMAND_NEXT = 4;
constexpr int MEDIA_REMOTE_COMMAND_PREVIOUS = 5;

using NowPlayingInfoHandler = void (^)(NSDictionary* information);
using NowPlayingProcessHandler = void (^)(int processIdentifier);
using GetNowPlayingInfoFunction = void (*)(dispatch_queue_t queue, NowPlayingInfoHandler handler);
using GetNowPlayingProcessFunction = void (*)(dispatch_queue_t queue, NowPlayingProcessHandler handler);
using SendCommandFunction = Boolean (*)(int command, NSDictionary* options);
using SetElapsedTimeFunction = void (*)(double elapsedTime);
using RegisterNotificationsFunction = void (*)(dispatch_queue_t queue);

struct MediaRemote {
	GetNowPlayingInfoFunction getNowPlayingInfo = nullptr;
	GetNowPlayingProcessFunction getNowPlayingProcess = nullptr;
	SendCommandFunction sendCommand = nullptr;
	SetElapsedTimeFunction setElapsedTime = nullptr;
	RegisterNotificationsFunction registerForNotifications = nullptr;
};

const MediaRemote& mediaRemote() {
	static const MediaRemote library = []() {
		MediaRemote loaded;
		void* handle = dlopen(
		    "/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote",
		    RTLD_LAZY
		);
		if (!handle) return loaded;

		loaded.getNowPlayingInfo =
		    reinterpret_cast<GetNowPlayingInfoFunction>(dlsym(handle, "MRMediaRemoteGetNowPlayingInfo"));
		loaded.getNowPlayingProcess = reinterpret_cast<GetNowPlayingProcessFunction>(
		    dlsym(handle, "MRMediaRemoteGetNowPlayingApplicationPID")
		);
		loaded.sendCommand = reinterpret_cast<SendCommandFunction>(dlsym(handle, "MRMediaRemoteSendCommand"));
		loaded.setElapsedTime =
		    reinterpret_cast<SetElapsedTimeFunction>(dlsym(handle, "MRMediaRemoteSetElapsedTime"));
		loaded.registerForNotifications = reinterpret_cast<RegisterNotificationsFunction>(
		    dlsym(handle, "MRMediaRemoteRegisterForNowPlayingNotifications")
		);
		return loaded;
	}();
	return library;
}

std::mutex& mediaRemoteMutex() {
	static std::mutex mutex;
	return mutex;
}

std::optional<MediaSnapshot>& cachedMediaRemoteSnapshot() {
	static std::optional<MediaSnapshot> snapshot;
	return snapshot;
}

QString artworkCacheDirectory() {
	auto directory = QDir(QDir::tempPath()).filePath(QStringLiteral("macshell-artwork"));
	QDir().mkpath(directory);
	return directory;
}

QString writeArtwork(NSData* imageData, const QString& cacheKey) {
	if (!imageData || cacheKey.isEmpty()) return {};

	auto fileName = QString::fromLatin1(
	    QCryptographicHash::hash(cacheKey.toUtf8(), QCryptographicHash::Sha1).toHex()
	);
	auto path = QDir(artworkCacheDirectory()).filePath(fileName + QStringLiteral(".png"));

	if (!QFileInfo::exists(path)) {
		NSBitmapImageRep* bitmap = [NSBitmapImageRep imageRepWithData:imageData];
		if (!bitmap) {
			NSImage* image = [[NSImage alloc] initWithData:imageData];
			bitmap = image ? [NSBitmapImageRep imageRepWithData:image.TIFFRepresentation] : nil;
		}
		NSData* pngData = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
		if (!pngData || ![pngData writeToFile:path.toNSString() atomically:YES]) return {};
	}

	return QStringLiteral("file://") + path;
}

NSRunningApplication* runningApplication(const char* bundleIdentifier) {
	NSArray<NSRunningApplication*>* applications = [NSRunningApplication
	    runningApplicationsWithBundleIdentifier:[NSString stringWithUTF8String:bundleIdentifier]];
	return applications.firstObject;
}

SBApplication* scriptableApplication(const QString& bundleIdentifier) {
	if (!runningApplication(bundleIdentifier.toUtf8().constData())) return nil;
	return [SBApplication applicationWithBundleIdentifier:bundleIdentifier.toNSString()];
}

id valueForKeySafely(id object, NSString* key) {
	@try {
		return [object valueForKey:key];
	} @catch (NSException*) {
		return nil;
	}
}

QString stringValue(id object, NSString* key) {
	id value = valueForKeySafely(object, key);
	return [value isKindOfClass:NSString.class] ? QString::fromNSString(value) : QString();
}

double numberValue(id object, NSString* key) {
	id value = valueForKeySafely(object, key);
	return [value isKindOfClass:NSNumber.class] ? [value doubleValue] : 0;
}

PlaybackStatus playbackStatusFor(SBApplication* application) {
	auto playerState = static_cast<FourCharCode>(numberValue(application, @"playerState"));
	switch (playerState) {
	case PLAYER_STATE_PLAYING: return PlaybackStatus::Playing;
	case PLAYER_STATE_PAUSED: return PlaybackStatus::Paused;
	case PLAYER_STATE_STOPPED: return PlaybackStatus::Stopped;
	default: return PlaybackStatus::Paused;
	}
}

void invokeScriptCommand(SBApplication* application, NSString* commandName) {
	SEL selector = NSSelectorFromString(commandName);
	if (![application respondsToSelector:selector]) return;

	using CommandFunction = void (*)(id, SEL);
	reinterpret_cast<CommandFunction>([application methodForSelector:selector])(application, selector);
}

std::optional<MediaSnapshot> readSpotify() {
	QString bundleIdentifier = QString::fromLatin1(SPOTIFY_BUNDLE_IDENTIFIER);
	SBApplication* spotify = scriptableApplication(bundleIdentifier);
	if (!spotify) return std::nullopt;

	id track = valueForKeySafely(spotify, @"currentTrack");
	auto title = stringValue(track, @"name");
	if (title.isEmpty()) return std::nullopt;

	MediaSnapshot snapshot;
	snapshot.sourceIdentifier = bundleIdentifier;
	snapshot.bundleIdentifier = bundleIdentifier;
	snapshot.identity = QStringLiteral("Spotify");
	snapshot.trackIdentifier = stringValue(track, @"id");
	snapshot.title = title;
	snapshot.artist = stringValue(track, @"artist");
	snapshot.album = stringValue(track, @"album");
	snapshot.albumArtist = stringValue(track, @"albumArtist");
	snapshot.artUrl = stringValue(track, @"artworkUrl");
	snapshot.lengthSeconds = numberValue(track, @"duration") / 1000.0;
	snapshot.positionSeconds = numberValue(spotify, @"playerPosition");
	snapshot.status = playbackStatusFor(spotify);
	snapshot.volume = numberValue(spotify, @"soundVolume") / 100.0;
	snapshot.shuffle = numberValue(spotify, @"shuffling") != 0;
	snapshot.repeat = numberValue(spotify, @"repeating") != 0 ? RepeatMode::All : RepeatMode::Off;
	snapshot.supportsSeeking = true;
	return snapshot;
}

std::optional<MediaSnapshot> readMusic() {
	QString bundleIdentifier = QString::fromLatin1(MUSIC_BUNDLE_IDENTIFIER);
	SBApplication* music = scriptableApplication(bundleIdentifier);
	if (!music) return std::nullopt;

	id track = valueForKeySafely(music, @"currentTrack");
	auto title = stringValue(track, @"name");
	if (title.isEmpty()) return std::nullopt;

	MediaSnapshot snapshot;
	snapshot.sourceIdentifier = bundleIdentifier;
	snapshot.bundleIdentifier = bundleIdentifier;
	snapshot.identity = QStringLiteral("Music");
	snapshot.trackIdentifier = stringValue(track, @"persistentID");
	snapshot.title = title;
	snapshot.artist = stringValue(track, @"artist");
	snapshot.album = stringValue(track, @"album");
	snapshot.albumArtist = stringValue(track, @"albumArtist");
	snapshot.lengthSeconds = numberValue(track, @"duration");
	snapshot.positionSeconds = numberValue(music, @"playerPosition");
	snapshot.status = playbackStatusFor(music);
	snapshot.volume = numberValue(music, @"soundVolume") / 100.0;
	snapshot.shuffle = numberValue(music, @"shuffleEnabled") != 0;
	snapshot.supportsSeeking = true;

	switch (static_cast<FourCharCode>(numberValue(music, @"songRepeat"))) {
	case MUSIC_REPEAT_ONE: snapshot.repeat = RepeatMode::Track; break;
	case MUSIC_REPEAT_ALL: snapshot.repeat = RepeatMode::All; break;
	default: snapshot.repeat = RepeatMode::Off; break;
	}

	id artworks = valueForKeySafely(track, @"artworks");
	id firstArtwork = [artworks respondsToSelector:@selector(firstObject)] ? [artworks firstObject] : nil;
	id artworkData = valueForKeySafely(firstArtwork, @"rawData");
	if ([artworkData isKindOfClass:NSData.class]) {
		snapshot.artUrl = writeArtwork(artworkData, snapshot.trackIdentifier + snapshot.title);
	}

	return snapshot;
}

MediaSnapshot snapshotFromMediaRemote(NSDictionary* information, int processIdentifier) {
	MediaSnapshot snapshot;
	snapshot.sourceIdentifier = QString::fromLatin1(MEDIA_REMOTE_SOURCE_IDENTIFIER);

	NSRunningApplication* application =
	    [NSRunningApplication runningApplicationWithProcessIdentifier:processIdentifier];
	snapshot.bundleIdentifier = QString::fromNSString(application.bundleIdentifier ?: @"");
	snapshot.identity = QString::fromNSString(application.localizedName ?: @"Now Playing");

	snapshot.title = QString::fromNSString(information[@"kMRMediaRemoteNowPlayingInfoTitle"] ?: @"");
	snapshot.artist = QString::fromNSString(information[@"kMRMediaRemoteNowPlayingInfoArtist"] ?: @"");
	snapshot.album = QString::fromNSString(information[@"kMRMediaRemoteNowPlayingInfoAlbum"] ?: @"");
	snapshot.trackIdentifier = QString::fromNSString(
	    [information[@"kMRMediaRemoteNowPlayingInfoContentItemIdentifier"] description] ?: @""
	);
	snapshot.lengthSeconds = [information[@"kMRMediaRemoteNowPlayingInfoDuration"] doubleValue];

	auto rate = [information[@"kMRMediaRemoteNowPlayingInfoPlaybackRate"] doubleValue];
	auto elapsed = [information[@"kMRMediaRemoteNowPlayingInfoElapsedTime"] doubleValue];
	NSDate* timestamp = information[@"kMRMediaRemoteNowPlayingInfoTimestamp"];
	if (timestamp && rate > 0) elapsed += -timestamp.timeIntervalSinceNow * rate;

	snapshot.positionSeconds = elapsed;
	snapshot.rate = rate > 0 ? rate : 1;
	snapshot.status = rate > 0 ? PlaybackStatus::Playing : PlaybackStatus::Paused;
	snapshot.supportsSeeking = snapshot.lengthSeconds > 0;
	snapshot.artUrl = writeArtwork(
	    information[@"kMRMediaRemoteNowPlayingInfoArtworkData"],
	    snapshot.bundleIdentifier + snapshot.title + snapshot.album
	);

	return snapshot;
}

void sendMediaRemoteCommand(MediaCommand command) {
	const auto& library = mediaRemote();
	if (!library.sendCommand) return;

	int nativeCommand = MEDIA_REMOTE_COMMAND_TOGGLE;
	switch (command) {
	case MediaCommand::Play: nativeCommand = MEDIA_REMOTE_COMMAND_PLAY; break;
	case MediaCommand::Pause: nativeCommand = MEDIA_REMOTE_COMMAND_PAUSE; break;
	case MediaCommand::TogglePlaying: nativeCommand = MEDIA_REMOTE_COMMAND_TOGGLE; break;
	case MediaCommand::Stop: nativeCommand = MEDIA_REMOTE_COMMAND_STOP; break;
	case MediaCommand::Next: nativeCommand = MEDIA_REMOTE_COMMAND_NEXT; break;
	case MediaCommand::Previous: nativeCommand = MEDIA_REMOTE_COMMAND_PREVIOUS; break;
	}

	library.sendCommand(nativeCommand, nil);
}

void sendScriptCommand(SBApplication* application, bool isMusic, MediaCommand command) {
	switch (command) {
	case MediaCommand::Play:
		invokeScriptCommand(application, isMusic ? @"playpause" : @"play");
		break;
	case MediaCommand::Pause: invokeScriptCommand(application, @"pause"); break;
	case MediaCommand::TogglePlaying: invokeScriptCommand(application, @"playpause"); break;
	case MediaCommand::Stop: invokeScriptCommand(application, isMusic ? @"stop" : @"pause"); break;
	case MediaCommand::Next: invokeScriptCommand(application, @"nextTrack"); break;
	case MediaCommand::Previous: invokeScriptCommand(application, @"previousTrack"); break;
	}
}

}

QList<MediaSnapshot> readMediaSnapshots() {
	QList<MediaSnapshot> snapshots;

	@autoreleasepool {
		if (auto spotify = readSpotify()) snapshots.append(*spotify);
		if (auto music = readMusic()) snapshots.append(*music);
	}

	std::lock_guard lock(mediaRemoteMutex());
	if (const auto& remote = cachedMediaRemoteSnapshot()) {
		auto duplicatesScriptedSource = std::ranges::any_of(snapshots, [&remote](const MediaSnapshot& snapshot) {
			return snapshot.bundleIdentifier == remote->bundleIdentifier;
		});
		if (!duplicatesScriptedSource && !remote->title.isEmpty()) snapshots.append(*remote);
	}

	return snapshots;
}

void requestMediaRemoteRefresh(std::function<void()> onUpdated) {
	const auto& library = mediaRemote();
	if (!library.getNowPlayingInfo || !library.getNowPlayingProcess) return;

	static std::once_flag notificationsRegistered;
	std::call_once(notificationsRegistered, [&library]() {
		if (library.registerForNotifications) library.registerForNotifications(dispatch_get_main_queue());
	});

	auto handler = std::make_shared<std::function<void()>>(std::move(onUpdated));
	library.getNowPlayingProcess(dispatch_get_main_queue(), ^(int processIdentifier) {
		library.getNowPlayingInfo(dispatch_get_main_queue(), ^(NSDictionary* information) {
			{
				std::lock_guard lock(mediaRemoteMutex());
				if (information.count > 0 && processIdentifier > 0) {
					cachedMediaRemoteSnapshot() = snapshotFromMediaRemote(information, processIdentifier);
				} else {
					cachedMediaRemoteSnapshot().reset();
				}
			}
			if (*handler) (*handler)();
		});
	});
}

void observeMediaChanges(std::function<void()> onChange) {
	auto handler = std::make_shared<std::function<void()>>(std::move(onChange));
	auto notify = ^(NSNotification*) {
		if (*handler) (*handler)();
	};

	for (NSString* name in @[
		@"kMRMediaRemoteNowPlayingInfoDidChangeNotification",
		@"kMRMediaRemoteNowPlayingApplicationIsPlayingDidChangeNotification",
		@"kMRMediaRemoteNowPlayingApplicationDidChangeNotification",
	])
	{
		[NSNotificationCenter.defaultCenter addObserverForName:name
		                                                object:nil
		                                                 queue:NSOperationQueue.mainQueue
		                                            usingBlock:notify];
	}

	for (NSString* name in @[@"com.spotify.client.PlaybackStateChanged", @"com.apple.Music.playerInfo"]) {
		[NSDistributedNotificationCenter.defaultCenter addObserverForName:name
		                                                           object:nil
		                                                            queue:NSOperationQueue.mainQueue
		                                                       usingBlock:notify];
	}
}

void sendCommand(const QString& sourceIdentifier, MediaCommand command) {
	if (sourceIdentifier == QLatin1StringView(MEDIA_REMOTE_SOURCE_IDENTIFIER)) {
		sendMediaRemoteCommand(command);
		return;
	}

	if (SBApplication* application = scriptableApplication(sourceIdentifier)) {
		sendScriptCommand(application, sourceIdentifier == QLatin1StringView(MUSIC_BUNDLE_IDENTIFIER), command);
	}
}

void seekTo(const QString& sourceIdentifier, qreal positionSeconds) {
	if (sourceIdentifier == QLatin1StringView(MEDIA_REMOTE_SOURCE_IDENTIFIER)) {
		if (mediaRemote().setElapsedTime) mediaRemote().setElapsedTime(positionSeconds);
		return;
	}

	if (SBApplication* application = scriptableApplication(sourceIdentifier)) {
		[application setValue:@(positionSeconds) forKey:@"playerPosition"];
	}
}

void setVolume(const QString& sourceIdentifier, qreal volume) {
	if (SBApplication* application = scriptableApplication(sourceIdentifier)) {
		[application setValue:@(static_cast<int>(std::clamp(volume, 0.0, 1.0) * 100)) forKey:@"soundVolume"];
	}
}

void setShuffle(const QString& sourceIdentifier, bool shuffle) {
	SBApplication* application = scriptableApplication(sourceIdentifier);
	if (!application) return;

	auto isMusic = sourceIdentifier == QLatin1StringView(MUSIC_BUNDLE_IDENTIFIER);
	[application setValue:@(shuffle) forKey:isMusic ? @"shuffleEnabled" : @"shuffling"];
}

void setRepeat(const QString& sourceIdentifier, RepeatMode repeat) {
	SBApplication* application = scriptableApplication(sourceIdentifier);
	if (!application) return;

	if (sourceIdentifier == QLatin1StringView(MUSIC_BUNDLE_IDENTIFIER)) {
		auto nativeRepeat = repeat == RepeatMode::Track ? MUSIC_REPEAT_ONE
		                  : repeat == RepeatMode::All   ? MUSIC_REPEAT_ALL
		                                                : MUSIC_REPEAT_OFF;
		[application setValue:@(nativeRepeat) forKey:@"songRepeat"];
	} else {
		[application setValue:@(repeat != RepeatMode::Off) forKey:@"repeating"];
	}
}

void raiseApplication(const QString& bundleIdentifier) {
	[runningApplication(bundleIdentifier.toUtf8().constData()) activateWithOptions:0];
}

void quitApplication(const QString& bundleIdentifier) {
	[runningApplication(bundleIdentifier.toUtf8().constData()) terminate];
}

}

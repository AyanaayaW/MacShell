#pragma once

#include <functional>
#include <optional>

#include <qlist.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::service::mpris::macos {

enum class PlaybackStatus : quint8 {
	Stopped,
	Playing,
	Paused,
};

enum class RepeatMode : quint8 {
	Off,
	Track,
	All,
};

enum class MediaCommand : quint8 {
	Play,
	Pause,
	TogglePlaying,
	Stop,
	Next,
	Previous,
};

struct MediaSnapshot {
	QString sourceIdentifier;
	QString bundleIdentifier;
	QString identity;
	QString trackIdentifier;
	QString title;
	QString artist;
	QString album;
	QString albumArtist;
	QString artUrl;
	qreal lengthSeconds = 0;
	qreal positionSeconds = 0;
	qreal rate = 1;
	PlaybackStatus status = PlaybackStatus::Stopped;
	std::optional<qreal> volume;
	std::optional<bool> shuffle;
	std::optional<RepeatMode> repeat;
	bool supportsSeeking = false;
};

QList<MediaSnapshot> readMediaSnapshots();

void sendCommand(const QString& sourceIdentifier, MediaCommand command);
void seekTo(const QString& sourceIdentifier, qreal positionSeconds);
void setVolume(const QString& sourceIdentifier, qreal volume);
void setShuffle(const QString& sourceIdentifier, bool shuffle);
void setRepeat(const QString& sourceIdentifier, RepeatMode repeat);
void raiseApplication(const QString& bundleIdentifier);
void quitApplication(const QString& bundleIdentifier);

void requestMediaRemoteRefresh(std::function<void()> onUpdated);
void observeMediaChanges(std::function<void()> onChange);

}

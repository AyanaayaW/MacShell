#include "mpris.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

#include <qdatetime.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qset.h>
#include <qstring.h>
#include <qtypes.h>
#include <qvariant.h>

#include "media_sources.hpp"

namespace qs::service::mpris {

namespace {

Q_LOGGING_CATEGORY(logMpris, "quickshell.service.mpris.macos", QtWarningMsg);

constexpr auto POLL_INTERVAL_MILLISECONDS = 2000;
constexpr auto REFRESH_DEBOUNCE_MILLISECONDS = 150;
constexpr qreal POSITION_DRIFT_TOLERANCE_SECONDS = 1.5;
constexpr qreal MICROSECONDS_PER_SECOND = 1000000.0;

MprisPlaybackState::Enum playbackStateFor(macos::PlaybackStatus status) {
	switch (status) {
	case macos::PlaybackStatus::Playing: return MprisPlaybackState::Playing;
	case macos::PlaybackStatus::Paused: return MprisPlaybackState::Paused;
	case macos::PlaybackStatus::Stopped: return MprisPlaybackState::Stopped;
	}
	return MprisPlaybackState::Stopped;
}

MprisLoopState::Enum loopStateFor(macos::RepeatMode repeat) {
	switch (repeat) {
	case macos::RepeatMode::Track: return MprisLoopState::Track;
	case macos::RepeatMode::All: return MprisLoopState::Playlist;
	case macos::RepeatMode::Off: return MprisLoopState::None;
	}
	return MprisLoopState::None;
}

macos::RepeatMode repeatModeFor(MprisLoopState::Enum loopState) {
	switch (loopState) {
	case MprisLoopState::Track: return macos::RepeatMode::Track;
	case MprisLoopState::Playlist: return macos::RepeatMode::All;
	case MprisLoopState::None: return macos::RepeatMode::Off;
	}
	return macos::RepeatMode::Off;
}

QVariantMap metadataFor(const macos::MediaSnapshot& snapshot, quint32 uniqueId) {
	QVariantMap metadata {
	    {QStringLiteral("mpris:trackid"), QStringLiteral("/org/mpris/MediaPlayer2/Track/%1").arg(uniqueId)},
	    {QStringLiteral("xesam:title"), snapshot.title},
	    {QStringLiteral("xesam:album"), snapshot.album},
	};

	if (!snapshot.artist.isEmpty()) metadata.insert(QStringLiteral("xesam:artist"), QStringList {snapshot.artist});
	if (!snapshot.albumArtist.isEmpty()) {
		metadata.insert(QStringLiteral("xesam:albumArtist"), QStringList {snapshot.albumArtist});
	}
	if (!snapshot.artUrl.isEmpty()) metadata.insert(QStringLiteral("mpris:artUrl"), snapshot.artUrl);
	if (snapshot.lengthSeconds > 0) {
		metadata.insert(
		    QStringLiteral("mpris:length"),
		    static_cast<qlonglong>(snapshot.lengthSeconds * MICROSECONDS_PER_SECOND)
		);
	}

	return metadata;
}

}

QString MprisPlaybackState::toString(MprisPlaybackState::Enum status) {
	switch (status) {
	case MprisPlaybackState::Stopped: return QStringLiteral("Stopped");
	case MprisPlaybackState::Playing: return QStringLiteral("Playing");
	case MprisPlaybackState::Paused: return QStringLiteral("Paused");
	default: return QStringLiteral("Unknown Status");
	}
}

QString MprisLoopState::toString(MprisLoopState::Enum status) {
	switch (status) {
	case MprisLoopState::None: return QStringLiteral("None");
	case MprisLoopState::Track: return QStringLiteral("Track");
	case MprisLoopState::Playlist: return QStringLiteral("Playlist");
	default: return QStringLiteral("Unknown Status");
	}
}

MprisPlayer::MprisPlayer(QString sourceIdentifier, QObject* parent)
    : QObject(parent)
    , mSourceIdentifier(std::move(sourceIdentifier))
    , mPositionReferenceTime(QDateTime::currentDateTimeUtc()) {
	QObject::connect(this, &MprisPlayer::playbackStateChanged, this, &MprisPlayer::isPlayingChanged);
}

void MprisPlayer::applySnapshot(const macos::MediaSnapshot& snapshot) {
	auto trackKey = snapshot.trackIdentifier + u'\x1f' + snapshot.title + u'\x1f' + snapshot.artist
	              + u'\x1f' + snapshot.album;
	auto trackChanged = trackKey != this->mTrackKey;
	auto extrapolatedPosition = this->position();

	if (trackChanged) {
		emit this->trackChanged();
		this->mTrackKey = trackKey;
	}

	this->mBundleIdentifier = snapshot.bundleIdentifier;
	auto canControlApplication = !snapshot.bundleIdentifier.isEmpty();

	Qt::beginPropertyUpdateGroup();
	if (trackChanged) this->bUniqueId = this->bUniqueId + 1;
	this->bCanControl = true;
	this->bCanPlay = true;
	this->bCanPause = true;
	this->bCanTogglePlaying = true;
	this->bCanGoNext = true;
	this->bCanGoPrevious = true;
	this->bCanSeek = snapshot.supportsSeeking;
	this->bCanQuit = canControlApplication;
	this->bCanRaise = canControlApplication;
	this->bIdentity = snapshot.identity;
	this->bDesktopEntry = snapshot.bundleIdentifier;
	this->bLengthSupported = snapshot.lengthSeconds > 0;
	this->bTrackTitle = snapshot.title;
	this->bTrackArtist = snapshot.artist;
	this->bTrackAlbum = snapshot.album;
	this->bTrackAlbumArtist = snapshot.albumArtist;
	this->bTrackArtUrl = snapshot.artUrl;
	this->bMetadata = metadataFor(snapshot, this->bUniqueId.value());
	Qt::endPropertyUpdateGroup();

	if (!qFuzzyCompare(1 + snapshot.lengthSeconds, 1 + this->mLength)) {
		this->mLength = snapshot.lengthSeconds;
		emit this->lengthChanged();
	}

	auto positionSupported = snapshot.lengthSeconds > 0;
	if (positionSupported != this->mPositionSupported) {
		this->mPositionSupported = positionSupported;
		emit this->positionSupportedChanged();
	}

	auto playbackState = playbackStateFor(snapshot.status);
	auto playbackChanged = playbackState != this->mPlaybackState;

	this->mPositionAtReference = snapshot.positionSeconds;
	this->mPositionReferenceTime = QDateTime::currentDateTimeUtc();
	this->mRate = snapshot.rate;

	if (playbackChanged) {
		this->mPlaybackState = playbackState;
		emit this->playbackStateChanged();
	}

	auto positionDrifted =
	    std::abs(extrapolatedPosition - snapshot.positionSeconds) > POSITION_DRIFT_TOLERANCE_SECONDS;
	if (trackChanged || playbackChanged || positionDrifted) emit this->positionChanged();

	auto volumeSupported = snapshot.volume.has_value();
	if (volumeSupported != this->mVolumeSupported) {
		this->mVolumeSupported = volumeSupported;
		emit this->volumeSupportedChanged();
	}
	if (snapshot.volume && !qFuzzyCompare(1 + *snapshot.volume, 1 + this->mVolume)) {
		this->mVolume = *snapshot.volume;
		emit this->volumeChanged();
	}

	auto shuffleSupported = snapshot.shuffle.has_value();
	if (shuffleSupported != this->mShuffleSupported) {
		this->mShuffleSupported = shuffleSupported;
		emit this->shuffleSupportedChanged();
	}
	if (snapshot.shuffle && *snapshot.shuffle != this->mShuffle) {
		this->mShuffle = *snapshot.shuffle;
		emit this->shuffleChanged();
	}

	auto loopSupported = snapshot.repeat.has_value();
	if (loopSupported != this->mLoopSupported) {
		this->mLoopSupported = loopSupported;
		emit this->loopSupportedChanged();
	}
	if (snapshot.repeat && loopStateFor(*snapshot.repeat) != this->mLoopState) {
		this->mLoopState = loopStateFor(*snapshot.repeat);
		emit this->loopStateChanged();
	}

	if (trackChanged) emit this->postTrackChanged();
}

QString MprisPlayer::address() const {
	auto name = this->bIdentity.value().isEmpty() ? this->mSourceIdentifier : this->bIdentity.value();
	return QStringLiteral("org.mpris.MediaPlayer2.") + name.toLower().remove(u' ');
}

qreal MprisPlayer::position() const {
	auto position = this->mPositionAtReference;
	if (this->mPlaybackState == MprisPlaybackState::Playing) {
		auto elapsedMilliseconds = this->mPositionReferenceTime.msecsTo(QDateTime::currentDateTimeUtc());
		position += static_cast<qreal>(elapsedMilliseconds) / 1000.0 * this->mRate;
	}
	return this->mLength > 0 ? std::clamp(position, 0.0, this->mLength) : std::max(position, 0.0);
}

void MprisPlayer::setPosition(qreal position) {
	if (!this->bCanSeek) {
		qCWarning(logMpris) << "Seeking is not supported by" << this->bIdentity.value();
		return;
	}

	macos::seekTo(this->mSourceIdentifier, position);
	this->mPositionAtReference = position;
	this->mPositionReferenceTime = QDateTime::currentDateTimeUtc();
	emit this->positionChanged();
	MprisPlayer::requestRefresh();
}

void MprisPlayer::seek(qreal offset) { this->setPosition(this->position() + offset); }

void MprisPlayer::setVolume(qreal volume) {
	if (!this->mVolumeSupported) {
		qCWarning(logMpris) << "Volume control is not supported by" << this->bIdentity.value();
		return;
	}
	macos::setVolume(this->mSourceIdentifier, volume);
	MprisPlayer::requestRefresh();
}

void MprisPlayer::setPlaybackState(MprisPlaybackState::Enum playbackState) {
	switch (playbackState) {
	case MprisPlaybackState::Playing: this->play(); break;
	case MprisPlaybackState::Paused: this->pause(); break;
	case MprisPlaybackState::Stopped: this->stop(); break;
	}
}

void MprisPlayer::setPlaying(bool playing) {
	if (playing) this->play();
	else this->pause();
}

void MprisPlayer::setLoopState(MprisLoopState::Enum loopState) {
	if (!this->mLoopSupported) {
		qCWarning(logMpris) << "Looping is not supported by" << this->bIdentity.value();
		return;
	}
	macos::setRepeat(this->mSourceIdentifier, repeatModeFor(loopState));
	MprisPlayer::requestRefresh();
}

void MprisPlayer::setRate(qreal rate) {
	if (!qFuzzyCompare(rate, 1.0)) {
		qCWarning(logMpris) << "Changing the playback rate is not supported on macOS.";
	}
}

void MprisPlayer::setShuffle(bool shuffle) {
	if (!this->mShuffleSupported) {
		qCWarning(logMpris) << "Shuffle is not supported by" << this->bIdentity.value();
		return;
	}
	macos::setShuffle(this->mSourceIdentifier, shuffle);
	MprisPlayer::requestRefresh();
}

void MprisPlayer::setFullscreen([[maybe_unused]] bool fullscreen) {
	qCWarning(logMpris) << "Fullscreen control is not supported on macOS.";
}

void MprisPlayer::raise() { macos::raiseApplication(this->mBundleIdentifier); }
void MprisPlayer::quit() { macos::quitApplication(this->mBundleIdentifier); }

void MprisPlayer::openUri([[maybe_unused]] const QString& uri) {
	qCWarning(logMpris) << "Opening URIs in a player is not supported on macOS.";
}

void MprisPlayer::sendCommand(macos::MediaCommand command) {
	macos::sendCommand(this->mSourceIdentifier, command);
	MprisPlayer::requestRefresh();
}

void MprisPlayer::requestRefresh() { MprisWatcher::instance()->scheduleRefresh(); }

void MprisPlayer::next() { this->sendCommand(macos::MediaCommand::Next); }
void MprisPlayer::previous() { this->sendCommand(macos::MediaCommand::Previous); }
void MprisPlayer::play() { this->sendCommand(macos::MediaCommand::Play); }
void MprisPlayer::pause() { this->sendCommand(macos::MediaCommand::Pause); }
void MprisPlayer::stop() { this->sendCommand(macos::MediaCommand::Stop); }
void MprisPlayer::togglePlaying() { this->sendCommand(macos::MediaCommand::TogglePlaying); }

MprisWatcher* MprisWatcher::instance() {
	static auto* sharedInstance = new MprisWatcher();
	return sharedInstance;
}

MprisWatcher::MprisWatcher() {
	this->mRefreshDebounce.setSingleShot(true);
	this->mRefreshDebounce.setInterval(REFRESH_DEBOUNCE_MILLISECONDS);
	QObject::connect(&this->mRefreshDebounce, &QTimer::timeout, this, &MprisWatcher::refresh);

	QObject::connect(&this->mPollTimer, &QTimer::timeout, this, &MprisWatcher::refresh);
	this->mPollTimer.start(POLL_INTERVAL_MILLISECONDS);

	macos::observeMediaChanges([watcher = QPointer<MprisWatcher>(this)]() {
		if (watcher) watcher->scheduleRefresh();
	});

	this->refresh();
}

void MprisWatcher::scheduleRefresh() { this->mRefreshDebounce.start(); }

void MprisWatcher::refresh() {
	this->applySnapshots(macos::readMediaSnapshots());

	macos::requestMediaRemoteRefresh([watcher = QPointer<MprisWatcher>(this)]() {
		if (watcher) watcher->applySnapshots(macos::readMediaSnapshots());
	});
}

void MprisWatcher::applySnapshots(const QList<macos::MediaSnapshot>& snapshots) {
	QSet<QString> presentSources;

	for (const auto& snapshot: snapshots) {
		presentSources.insert(snapshot.sourceIdentifier);

		auto* player = this->mPlayersBySource.value(snapshot.sourceIdentifier);
		if (!player) {
			player = new MprisPlayer(snapshot.sourceIdentifier, this);
			player->applySnapshot(snapshot);
			this->mPlayersBySource.insert(snapshot.sourceIdentifier, player);
			this->mPlayers.insertObject(player);
			continue;
		}

		player->applySnapshot(snapshot);
	}

	for (auto iterator = this->mPlayersBySource.begin(); iterator != this->mPlayersBySource.end();) {
		if (presentSources.contains(iterator.key())) {
			++iterator;
			continue;
		}

		auto* removedPlayer = iterator.value();
		iterator = this->mPlayersBySource.erase(iterator);
		this->mPlayers.removeObject(removedPlayer);
		removedPlayer->deleteLater();
	}
}

}

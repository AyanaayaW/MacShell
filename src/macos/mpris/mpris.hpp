#pragma once

#include <qdatetime.h>
#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/model.hpp"
#include "media_sources.hpp"

namespace qs::service::mpris {

class MprisPlaybackState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Stopped = 0,
		Playing = 1,
		Paused = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::mpris::MprisPlaybackState::Enum status);
};

class MprisLoopState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		Track = 1,
		Playlist = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::mpris::MprisLoopState::Enum status);
};

class MprisPlayer: public QObject {
	Q_OBJECT;
	Q_PROPERTY(bool canControl READ default NOTIFY canControlChanged BINDABLE bindableCanControl);
	Q_PROPERTY(bool canPlay READ default NOTIFY canPlayChanged BINDABLE bindableCanPlay);
	Q_PROPERTY(bool canPause READ default NOTIFY canPauseChanged BINDABLE bindableCanPause);
	Q_PROPERTY(bool canTogglePlaying READ default NOTIFY canTogglePlayingChanged BINDABLE bindableCanTogglePlaying);
	Q_PROPERTY(bool canSeek READ default NOTIFY canSeekChanged BINDABLE bindableCanSeek);
	Q_PROPERTY(bool canGoNext READ default NOTIFY canGoNextChanged BINDABLE bindableCanGoNext);
	Q_PROPERTY(bool canGoPrevious READ default NOTIFY canGoPreviousChanged BINDABLE bindableCanGoPrevious);
	Q_PROPERTY(bool canQuit READ default NOTIFY canQuitChanged BINDABLE bindableCanQuit);
	Q_PROPERTY(bool canRaise READ default NOTIFY canRaiseChanged BINDABLE bindableCanRaise);
	Q_PROPERTY(bool canSetFullscreen READ default NOTIFY canSetFullscreenChanged BINDABLE bindableCanSetFullscreen);
	Q_PROPERTY(QString identity READ default NOTIFY identityChanged BINDABLE bindableIdentity);
	Q_PROPERTY(QString desktopEntry READ default NOTIFY desktopEntryChanged BINDABLE bindableDesktopEntry);
	Q_PROPERTY(QString dbusName READ address CONSTANT);
	Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged);
	Q_PROPERTY(bool positionSupported READ positionSupported NOTIFY positionSupportedChanged);
	Q_PROPERTY(qreal length READ length NOTIFY lengthChanged);
	Q_PROPERTY(bool lengthSupported READ default NOTIFY lengthSupportedChanged BINDABLE bindableLengthSupported);
	Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged);
	Q_PROPERTY(bool volumeSupported READ volumeSupported NOTIFY volumeSupportedChanged);
	Q_PROPERTY(QVariantMap metadata READ default NOTIFY metadataChanged BINDABLE bindableMetadata);
	Q_PROPERTY(quint32 uniqueId READ default NOTIFY uniqueIdChanged BINDABLE bindableUniqueId);
	Q_PROPERTY(QString trackTitle READ default NOTIFY trackTitleChanged BINDABLE bindableTrackTitle);
	Q_PROPERTY(QString trackArtist READ default NOTIFY trackArtistChanged BINDABLE bindableTrackArtist);
	Q_PROPERTY(QString trackArtists READ default NOTIFY trackArtistChanged BINDABLE bindableTrackArtist);
	Q_PROPERTY(QString trackAlbum READ default NOTIFY trackAlbumChanged BINDABLE bindableTrackAlbum);
	Q_PROPERTY(QString trackAlbumArtist READ default NOTIFY trackAlbumArtistChanged BINDABLE bindableTrackAlbumArtist);
	Q_PROPERTY(QString trackArtUrl READ default NOTIFY trackArtUrlChanged BINDABLE bindableTrackArtUrl);
	Q_PROPERTY(qs::service::mpris::MprisPlaybackState::Enum playbackState READ playbackState WRITE setPlaybackState NOTIFY playbackStateChanged);
	Q_PROPERTY(bool isPlaying READ isPlaying WRITE setPlaying NOTIFY isPlayingChanged);
	Q_PROPERTY(qs::service::mpris::MprisLoopState::Enum loopState READ loopState WRITE setLoopState NOTIFY loopStateChanged);
	Q_PROPERTY(bool loopSupported READ loopSupported NOTIFY loopSupportedChanged);
	Q_PROPERTY(qreal rate READ rate WRITE setRate NOTIFY rateChanged);
	Q_PROPERTY(qreal minRate READ default NOTIFY minRateChanged BINDABLE bindableMinRate);
	Q_PROPERTY(qreal maxRate READ default NOTIFY maxRateChanged BINDABLE bindableMaxRate);
	Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged);
	Q_PROPERTY(bool shuffleSupported READ shuffleSupported NOTIFY shuffleSupportedChanged);
	Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged);
	Q_PROPERTY(QList<QString> supportedUriSchemes READ default NOTIFY supportedUriSchemesChanged BINDABLE bindableSupportedUriSchemes);
	Q_PROPERTY(QList<QString> supportedMimeTypes READ default NOTIFY supportedMimeTypesChanged BINDABLE bindableSupportedMimeTypes);
	QML_ELEMENT;
	QML_UNCREATABLE("MprisPlayers can only be acquired from Mpris");

public:
	MprisPlayer(QString sourceIdentifier, QObject* parent);

	void applySnapshot(const macos::MediaSnapshot& snapshot);

	Q_INVOKABLE void raise();
	Q_INVOKABLE void quit();
	Q_INVOKABLE static void openUri(const QString& uri);
	Q_INVOKABLE void next();
	Q_INVOKABLE void previous();
	Q_INVOKABLE void seek(qreal offset);
	Q_INVOKABLE void play();
	Q_INVOKABLE void pause();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void togglePlaying();

	[[nodiscard]] QString address() const;
	[[nodiscard]] qreal position() const;
	void setPosition(qreal position);
	[[nodiscard]] bool positionSupported() const { return this->mPositionSupported; }
	[[nodiscard]] qreal length() const { return this->mLength; }
	[[nodiscard]] qreal volume() const { return this->mVolume; }
	void setVolume(qreal volume);
	[[nodiscard]] bool volumeSupported() const { return this->mVolumeSupported; }
	[[nodiscard]] MprisPlaybackState::Enum playbackState() const { return this->mPlaybackState; }
	void setPlaybackState(MprisPlaybackState::Enum playbackState);
	[[nodiscard]] bool isPlaying() const { return this->mPlaybackState == MprisPlaybackState::Playing; }
	void setPlaying(bool playing);
	[[nodiscard]] MprisLoopState::Enum loopState() const { return this->mLoopState; }
	void setLoopState(MprisLoopState::Enum loopState);
	[[nodiscard]] bool loopSupported() const { return this->mLoopSupported; }
	[[nodiscard]] qreal rate() const { return this->mRate; }
	static void setRate(qreal rate);
	[[nodiscard]] bool shuffle() const { return this->mShuffle; }
	void setShuffle(bool shuffle);
	[[nodiscard]] bool shuffleSupported() const { return this->mShuffleSupported; }
	[[nodiscard]] static bool fullscreen() { return false; }
	static void setFullscreen(bool fullscreen);

	[[nodiscard]] QBindable<bool> bindableCanControl() { return &this->bCanControl; }
	[[nodiscard]] QBindable<bool> bindableCanPlay() { return &this->bCanPlay; }
	[[nodiscard]] QBindable<bool> bindableCanPause() { return &this->bCanPause; }
	[[nodiscard]] QBindable<bool> bindableCanTogglePlaying() { return &this->bCanTogglePlaying; }
	[[nodiscard]] QBindable<bool> bindableCanSeek() { return &this->bCanSeek; }
	[[nodiscard]] QBindable<bool> bindableCanGoNext() { return &this->bCanGoNext; }
	[[nodiscard]] QBindable<bool> bindableCanGoPrevious() { return &this->bCanGoPrevious; }
	[[nodiscard]] QBindable<bool> bindableCanQuit() { return &this->bCanQuit; }
	[[nodiscard]] QBindable<bool> bindableCanRaise() { return &this->bCanRaise; }
	[[nodiscard]] QBindable<bool> bindableCanSetFullscreen() { return &this->bCanSetFullscreen; }
	[[nodiscard]] QBindable<QString> bindableIdentity() { return &this->bIdentity; }
	[[nodiscard]] QBindable<QString> bindableDesktopEntry() { return &this->bDesktopEntry; }
	[[nodiscard]] QBindable<bool> bindableLengthSupported() { return &this->bLengthSupported; }
	[[nodiscard]] QBindable<QVariantMap> bindableMetadata() { return &this->bMetadata; }
	[[nodiscard]] QBindable<quint32> bindableUniqueId() { return &this->bUniqueId; }
	[[nodiscard]] QBindable<QString> bindableTrackTitle() { return &this->bTrackTitle; }
	[[nodiscard]] QBindable<QString> bindableTrackArtist() { return &this->bTrackArtist; }
	[[nodiscard]] QBindable<QString> bindableTrackAlbum() { return &this->bTrackAlbum; }
	[[nodiscard]] QBindable<QString> bindableTrackAlbumArtist() { return &this->bTrackAlbumArtist; }
	[[nodiscard]] QBindable<QString> bindableTrackArtUrl() { return &this->bTrackArtUrl; }
	[[nodiscard]] QBindable<qreal> bindableMinRate() { return &this->bMinRate; }
	[[nodiscard]] QBindable<qreal> bindableMaxRate() { return &this->bMaxRate; }
	[[nodiscard]] QBindable<QList<QString>> bindableSupportedUriSchemes() {
		return &this->bSupportedUriSchemes;
	}
	[[nodiscard]] QBindable<QList<QString>> bindableSupportedMimeTypes() {
		return &this->bSupportedMimeTypes;
	}

signals:
	void trackChanged();
	void postTrackChanged();
	void canControlChanged();
	void canPlayChanged();
	void canPauseChanged();
	void canTogglePlayingChanged();
	void canSeekChanged();
	void canGoNextChanged();
	void canGoPreviousChanged();
	void canQuitChanged();
	void canRaiseChanged();
	void canSetFullscreenChanged();
	void identityChanged();
	void desktopEntryChanged();
	void positionChanged();
	void positionSupportedChanged();
	void lengthChanged();
	void lengthSupportedChanged();
	void volumeChanged();
	void volumeSupportedChanged();
	void metadataChanged();
	void uniqueIdChanged();
	void trackTitleChanged();
	void trackArtistChanged();
	void trackAlbumChanged();
	void trackAlbumArtistChanged();
	void trackArtUrlChanged();
	void playbackStateChanged();
	void isPlayingChanged();
	void loopStateChanged();
	void loopSupportedChanged();
	void rateChanged();
	void minRateChanged();
	void maxRateChanged();
	void shuffleChanged();
	void shuffleSupportedChanged();
	void fullscreenChanged();
	void supportedUriSchemesChanged();
	void supportedMimeTypesChanged();

private:
	void sendCommand(macos::MediaCommand command);
	static void requestRefresh();

	QString mSourceIdentifier;
	QString mBundleIdentifier;
	QString mTrackKey;
	qreal mPositionAtReference = 0;
	QDateTime mPositionReferenceTime;
	bool mPositionSupported = false;
	qreal mLength = 0;
	qreal mVolume = 1;
	bool mVolumeSupported = false;
	MprisPlaybackState::Enum mPlaybackState = MprisPlaybackState::Stopped;
	MprisLoopState::Enum mLoopState = MprisLoopState::None;
	bool mLoopSupported = false;
	qreal mRate = 1;
	bool mShuffle = false;
	bool mShuffleSupported = false;

	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanControl, &MprisPlayer::canControlChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanPlay, &MprisPlayer::canPlayChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanPause, &MprisPlayer::canPauseChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanTogglePlaying, &MprisPlayer::canTogglePlayingChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanSeek, &MprisPlayer::canSeekChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanGoNext, &MprisPlayer::canGoNextChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanGoPrevious, &MprisPlayer::canGoPreviousChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanQuit, &MprisPlayer::canQuitChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanRaise, &MprisPlayer::canRaiseChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanSetFullscreen, &MprisPlayer::canSetFullscreenChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bIdentity, &MprisPlayer::identityChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bDesktopEntry, &MprisPlayer::desktopEntryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bLengthSupported, &MprisPlayer::lengthSupportedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QVariantMap, bMetadata, &MprisPlayer::metadataChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, quint32, bUniqueId, &MprisPlayer::uniqueIdChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackTitle, &MprisPlayer::trackTitleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackArtist, &MprisPlayer::trackArtistChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackAlbum, &MprisPlayer::trackAlbumChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackAlbumArtist, &MprisPlayer::trackAlbumArtistChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackArtUrl, &MprisPlayer::trackArtUrlChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MprisPlayer, qreal, bMinRate, 1, &MprisPlayer::minRateChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MprisPlayer, qreal, bMaxRate, 1, &MprisPlayer::maxRateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QList<QString>, bSupportedUriSchemes, &MprisPlayer::supportedUriSchemesChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QList<QString>, bSupportedMimeTypes, &MprisPlayer::supportedMimeTypesChanged);
};

class MprisWatcher: public QObject {
	Q_OBJECT;

public:
	static MprisWatcher* instance();

	[[nodiscard]] ObjectModel<MprisPlayer>* players() { return &this->mPlayers; }
	void refresh();
	void scheduleRefresh();

private:
	MprisWatcher();

	void applySnapshots(const QList<macos::MediaSnapshot>& snapshots);

	ObjectModel<MprisPlayer> mPlayers {this};
	QHash<QString, MprisPlayer*> mPlayersBySource;
	QTimer mPollTimer;
	QTimer mRefreshDebounce;
};

class MprisQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Mpris);
	QML_SINGLETON;
	Q_PROPERTY(UntypedObjectModel* players READ players CONSTANT);

public:
	explicit MprisQml(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static ObjectModel<MprisPlayer>* players() { return MprisWatcher::instance()->players(); }
};

}

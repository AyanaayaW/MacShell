#pragma once

#include <memory>
#include <vector>

#include <CoreAudio/CoreAudio.h>
#include <qflags.h>
#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qqmllist.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../../core/model.hpp"
#include "core_audio.hpp"
#include "level_meter.hpp"

namespace qs::service::pipewire {

class PwAudioChannel: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum {
		Unknown = 0,
		NA = 1,
		Mono = 2,
		FrontLeft = 3,
		FrontRight = 4,
		FrontCenter = 5,
		LowFrequencyEffects = 6,
		SideLeft = 7,
		SideRight = 8,
		FrontLeftCenter = 9,
		FrontRightCenter = 10,
		RearCenter = 11,
		RearLeft = 12,
		RearRight = 13,
		TopCenter = 14,
		TopFrontLeft = 15,
		TopFrontCenter = 16,
		TopFrontRight = 17,
		TopRearLeft = 18,
		TopRearCenter = 19,
		TopRearRight = 20,
		RearLeftCenter = 21,
		RearRightCenter = 22,
		FrontLeftWide = 23,
		FrontRightWide = 24,
		LowFrequencyEffects2 = 25,
		FrontLeftHigh = 26,
		FrontCenterHigh = 27,
		FrontRightHigh = 28,
		TopFrontLeftCenter = 29,
		TopFrontRightCenter = 30,
		TopSideLeft = 31,
		TopSideRight = 32,
		LowFrequencyEffectsLeft = 33,
		LowFrequencyEffectsRight = 34,
		BottomCenter = 35,
		BottomLeftCenter = 36,
		BottomRightCenter = 37,
		AuxRangeStart = 0x1000,
		AuxRangeEnd = 0x1fff,
		CustomRangeStart = 0x10000,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::pipewire::PwAudioChannel::Enum value);
};

class PwNodeType: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Flag : quint8 {
		Untracked = 0b0,
		Audio = 0b1,
		Video = 0b10,
		Stream = 0b100,
		Source = 0b1000,
		Sink = 0b10000,
		AudioSink = Audio | Sink,
		AudioSource = Audio | Source,
		AudioDuplex = Audio | Sink | Source,
		AudioOutStream = Audio | Sink | Stream,
		AudioInStream = Audio | Source | Stream,
		VideoSource = Video | Source,
		VideoSink = Video | Sink,
	};
	Q_ENUM(Flag);
	Q_DECLARE_FLAGS(Flags, Flag);

	Q_INVOKABLE static QString toString(qs::service::pipewire::PwNodeType::Flags type);
};

class PwLinkState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : qint8 {
		Error = -2,
		Unlinked = -1,
		Init = 0,
		Negotiating = 1,
		Allocating = 2,
		Paused = 3,
		Active = 4,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::pipewire::PwLinkState::Enum value);
};

class PwNodeIface;

class PwNodeAudioIface: public QObject {
	Q_OBJECT;
	Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged);
	Q_PROPERTY(float volume READ averageVolume WRITE setAverageVolume NOTIFY volumesChanged);
	Q_PROPERTY(QVector<qs::service::pipewire::PwAudioChannel::Enum> channels READ channels NOTIFY channelsChanged);
	Q_PROPERTY(QVector<float> volumes READ volumes WRITE setVolumes NOTIFY volumesChanged);
	QML_NAMED_ELEMENT(PwNodeAudio);
	QML_UNCREATABLE("PwNodeAudio cannot be created directly");

public:
	PwNodeAudioIface(AudioObjectID deviceId, coreaudio::AudioDirection direction, QObject* parent);

	[[nodiscard]] bool isMuted() const { return this->mMuted; }
	void setMuted(bool muted);

	[[nodiscard]] float averageVolume() const;
	void setAverageVolume(float volume);

	[[nodiscard]] QVector<PwAudioChannel::Enum> channels() const { return this->mChannels; }

	[[nodiscard]] QVector<float> volumes() const { return this->mVolumes; }
	void setVolumes(const QVector<float>& volumes);

	void refresh();

signals:
	void mutedChanged();
	void volumesChanged();
	void channelsChanged();

private:
	void listenForChanges();
	void scheduleRefresh();

	AudioObjectID deviceId;
	coreaudio::AudioDirection direction;
	QList<quint32> channelElements;
	bool mMuted = false;
	float mMainVolume = 0;
	bool mHasMainVolume = false;
	QVector<PwAudioChannel::Enum> mChannels;
	QVector<float> mVolumes;
	std::vector<std::unique_ptr<coreaudio::PropertyListener>> listeners;
};

class PwNodeIface: public QObject {
	Q_OBJECT;
	Q_PROPERTY(quint32 id READ id CONSTANT);
	Q_PROPERTY(QString name READ name CONSTANT);
	Q_PROPERTY(QString description READ description CONSTANT);
	Q_PROPERTY(QString nickname READ nickname CONSTANT);
	Q_PROPERTY(bool isSink READ isSink CONSTANT);
	Q_PROPERTY(bool isStream READ isStream CONSTANT);
	Q_PROPERTY(qs::service::pipewire::PwNodeType::Flags type READ type CONSTANT);
	Q_PROPERTY(QVariantMap properties READ properties NOTIFY propertiesChanged);
	Q_PROPERTY(qs::service::pipewire::PwNodeAudioIface* audio READ audio CONSTANT);
	Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged);
	QML_NAMED_ELEMENT(PwNode);
	QML_UNCREATABLE("PwNodes cannot be created directly");

public:
	PwNodeIface(AudioObjectID deviceId, coreaudio::AudioDirection direction, QObject* parent);

	[[nodiscard]] AudioObjectID deviceId() const { return this->mDeviceId; }
	[[nodiscard]] coreaudio::AudioDirection direction() const { return this->mDirection; }

	[[nodiscard]] quint32 id() const;
	[[nodiscard]] QString name() const { return this->mName; }
	[[nodiscard]] QString description() const { return this->mDescription; }
	[[nodiscard]] QString nickname() const { return this->mDescription; }
	[[nodiscard]] bool isSink() const { return this->mDirection == coreaudio::AudioDirection::Output; }
	[[nodiscard]] static bool isStream() { return false; }
	[[nodiscard]] PwNodeType::Flags type() const;
	[[nodiscard]] QVariantMap properties() const { return this->mProperties; }
	[[nodiscard]] PwNodeAudioIface* audio() const { return this->mAudio; }
	[[nodiscard]] static bool isReady() { return true; }

signals:
	void propertiesChanged();
	void readyChanged();

private:
	AudioObjectID mDeviceId;
	coreaudio::AudioDirection mDirection;
	QString mName;
	QString mDescription;
	QVariantMap mProperties;
	PwNodeAudioIface* mAudio;
};

class PwLinkIface: public QObject {
	Q_OBJECT;
	Q_PROPERTY(quint32 id READ id CONSTANT);
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* target READ target CONSTANT);
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* source READ source CONSTANT);
	Q_PROPERTY(qs::service::pipewire::PwLinkState::Enum state READ state NOTIFY stateChanged);
	QML_NAMED_ELEMENT(PwLink);
	QML_UNCREATABLE("PwLinks cannot be created directly");

public:
	explicit PwLinkIface(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static quint32 id() { return 0; }
	[[nodiscard]] static PwNodeIface* target() { return nullptr; }
	[[nodiscard]] static PwNodeIface* source() { return nullptr; }
	[[nodiscard]] static PwLinkState::Enum state() { return PwLinkState::Unlinked; }

signals:
	void stateChanged();
};

class PwLinkGroupIface: public QObject {
	Q_OBJECT;
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* target READ target CONSTANT);
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* source READ source CONSTANT);
	Q_PROPERTY(qs::service::pipewire::PwLinkState::Enum state READ state NOTIFY stateChanged);
	QML_NAMED_ELEMENT(PwLinkGroup);
	QML_UNCREATABLE("PwLinkGroups cannot be created directly");

public:
	explicit PwLinkGroupIface(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static PwNodeIface* target() { return nullptr; }
	[[nodiscard]] static PwNodeIface* source() { return nullptr; }
	[[nodiscard]] static PwLinkState::Enum state() { return PwLinkState::Unlinked; }

signals:
	void stateChanged();
};

class AudioGraph: public QObject {
	Q_OBJECT;

public:
	static AudioGraph* instance();

	[[nodiscard]] ObjectModel<PwNodeIface>* nodes() { return &this->mNodes; }
	[[nodiscard]] PwNodeIface* defaultNode(coreaudio::AudioDirection direction) const;
	void setDefaultNode(coreaudio::AudioDirection direction, PwNodeIface* node);

signals:
	void defaultAudioSinkChanged();
	void defaultAudioSourceChanged();

private:
	AudioGraph();

	void refreshDevices();
	void refreshDefaults();
	void scheduleRefresh();

	static quint64 nodeKey(AudioObjectID deviceId, coreaudio::AudioDirection direction);

	ObjectModel<PwNodeIface> mNodes {this};
	QHash<quint64, PwNodeIface*> mNodesByKey;
	QPointer<PwNodeIface> mDefaultSink;
	QPointer<PwNodeIface> mDefaultSource;
	std::vector<std::unique_ptr<coreaudio::PropertyListener>> listeners;
};

class Pipewire: public QObject {
	Q_OBJECT;
	Q_PROPERTY(UntypedObjectModel* nodes READ nodes CONSTANT);
	Q_PROPERTY(UntypedObjectModel* links READ links CONSTANT);
	Q_PROPERTY(UntypedObjectModel* linkGroups READ linkGroups CONSTANT);
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* defaultAudioSink READ defaultAudioSink NOTIFY defaultAudioSinkChanged);
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* defaultAudioSource READ defaultAudioSource NOTIFY defaultAudioSourceChanged);
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* preferredDefaultAudioSink READ defaultAudioSink WRITE setPreferredDefaultAudioSink NOTIFY defaultConfiguredAudioSinkChanged);
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* preferredDefaultAudioSource READ defaultAudioSource WRITE setPreferredDefaultAudioSource NOTIFY defaultConfiguredAudioSourceChanged);
	Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged);
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Pipewire(QObject* parent = nullptr);

	[[nodiscard]] static ObjectModel<PwNodeIface>* nodes() { return AudioGraph::instance()->nodes(); }
	[[nodiscard]] ObjectModel<PwLinkIface>* links() { return &this->mLinks; }
	[[nodiscard]] ObjectModel<PwLinkGroupIface>* linkGroups() { return &this->mLinkGroups; }

	[[nodiscard]] static PwNodeIface* defaultAudioSink();
	[[nodiscard]] static PwNodeIface* defaultAudioSource();
	static void setPreferredDefaultAudioSink(PwNodeIface* node);
	static void setPreferredDefaultAudioSource(PwNodeIface* node);

	[[nodiscard]] static bool isReady() { return true; }

signals:
	void defaultAudioSinkChanged();
	void defaultAudioSourceChanged();
	void defaultConfiguredAudioSinkChanged();
	void defaultConfiguredAudioSourceChanged();
	void readyChanged();

private:
	ObjectModel<PwLinkIface> mLinks {this};
	ObjectModel<PwLinkGroupIface> mLinkGroups {this};
};

class PwNodeLinkTracker: public QObject {
	Q_OBJECT;
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* node READ node WRITE setNode NOTIFY nodeChanged);
	Q_PROPERTY(QQmlListProperty<qs::service::pipewire::PwLinkGroupIface> linkGroups READ linkGroups NOTIFY linkGroupsChanged);
	QML_ELEMENT;

public:
	explicit PwNodeLinkTracker(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] PwNodeIface* node() const { return this->mNode; }
	void setNode(PwNodeIface* node);

	[[nodiscard]] QQmlListProperty<PwLinkGroupIface> linkGroups();

signals:
	void nodeChanged();
	void linkGroupsChanged();

private:
	QPointer<PwNodeIface> mNode;
	QList<PwLinkGroupIface*> mLinkGroups;
};

class PwObjectTracker: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QList<QObject*> objects READ objects WRITE setObjects NOTIFY objectsChanged);
	QML_ELEMENT;

public:
	explicit PwObjectTracker(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] QList<QObject*> objects() const { return this->mObjects; }
	void setObjects(const QList<QObject*>& objects);

signals:
	void objectsChanged();

private:
	QList<QObject*> mObjects;
};

class PwNodePeakMonitor: public QObject {
	Q_OBJECT;
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* node READ node WRITE setNode NOTIFY nodeChanged);
	Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(QVector<float> peaks READ peaks NOTIFY peaksChanged);
	Q_PROPERTY(float peak READ peak NOTIFY peakChanged);
	Q_PROPERTY(QVector<qs::service::pipewire::PwAudioChannel::Enum> channels READ channels NOTIFY channelsChanged);
	QML_ELEMENT;

public:
	explicit PwNodePeakMonitor(QObject* parent = nullptr);
	~PwNodePeakMonitor() override;
	Q_DISABLE_COPY_MOVE(PwNodePeakMonitor);

	[[nodiscard]] PwNodeIface* node() const { return this->mNode; }
	void setNode(PwNodeIface* node);

	[[nodiscard]] bool isEnabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);

	[[nodiscard]] QVector<float> peaks() const { return this->mPeaks; }
	[[nodiscard]] float peak() const { return this->mPeak; }
	[[nodiscard]] QVector<PwAudioChannel::Enum> channels() const;

signals:
	void nodeChanged();
	void enabledChanged();
	void peaksChanged();
	void peakChanged();
	void channelsChanged();

private:
	void restartMeter();
	void samplePeaks();
	void resetPeaks();

	QPointer<PwNodeIface> mNode;
	bool mEnabled = true;
	QVector<float> mPeaks;
	float mPeak = 0;
	std::unique_ptr<coreaudio::LevelMeter> mMeter;
	QTimer mSampleTimer;
};

}

Q_DECLARE_OPERATORS_FOR_FLAGS(qs::service::pipewire::PwNodeType::Flags);

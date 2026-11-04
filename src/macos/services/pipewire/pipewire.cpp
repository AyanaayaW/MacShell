#include "pipewire.hpp"
#include <algorithm>
#include <dispatch/dispatch.h>
#include <memory>

#include <qcoreapplication.h>
#include <qhash.h>
#include <qlist.h>
#include <qmetaobject.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qqmllist.h>
#include <qset.h>
#include <qstring.h>
#include <qtypes.h>
#include <qvariant.h>

#include "core_audio.hpp"

namespace qs::service::pipewire {

using coreaudio::AudioDirection;
using coreaudio::AudioTransport;
using coreaudio::PropertyListener;

namespace {

QString transportBusName(AudioTransport transport) {
	switch (transport) {
	case AudioTransport::BuiltIn: return QStringLiteral("pci");
	case AudioTransport::Bluetooth: return QStringLiteral("bluetooth");
	case AudioTransport::Usb: return QStringLiteral("usb");
	case AudioTransport::Display: return QStringLiteral("display");
	case AudioTransport::AirPlay: return QStringLiteral("network");
	case AudioTransport::Virtual: return QStringLiteral("virtual");
	case AudioTransport::Aggregate: return QStringLiteral("aggregate");
	case AudioTransport::Unknown: return QString();
	}
	return QString();
}

QString deviceIconName(AudioTransport transport, AudioDirection direction) {
	if (transport == AudioTransport::Bluetooth) return QStringLiteral("audio-headset-bluetooth");
	if (transport == AudioTransport::Display) return QStringLiteral("video-display");
	return direction == AudioDirection::Output ? QStringLiteral("audio-card")
	                                           : QStringLiteral("audio-input-microphone");
}

constexpr int PEAK_SAMPLE_INTERVAL_MILLISECONDS = 33;

dispatch_queue_t meterControlQueue() {
	static dispatch_queue_t queue = dispatch_queue_create("MacShell.LevelMeterControl", DISPATCH_QUEUE_SERIAL);
	return queue;
}

template <typename Callback>
void runOnObjectThread(QObject* receiver, Callback callback) {
	QMetaObject::invokeMethod(receiver, std::move(callback), Qt::QueuedConnection);
}

}

QString PwAudioChannel::toString(PwAudioChannel::Enum value) {
	switch (value) {
	case Unknown: return QStringLiteral("Unknown");
	case NA: return QStringLiteral("N/A");
	case Mono: return QStringLiteral("Mono");
	case FrontLeft: return QStringLiteral("Front Left");
	case FrontRight: return QStringLiteral("Front Right");
	case FrontCenter: return QStringLiteral("Front Center");
	case LowFrequencyEffects: return QStringLiteral("Low Frequency Effects");
	case SideLeft: return QStringLiteral("Side Left");
	case SideRight: return QStringLiteral("Side Right");
	case FrontLeftCenter: return QStringLiteral("Front Left Center");
	case FrontRightCenter: return QStringLiteral("Front Right Center");
	case RearCenter: return QStringLiteral("Rear Center");
	case RearLeft: return QStringLiteral("Rear Left");
	case RearRight: return QStringLiteral("Rear Right");
	case TopCenter: return QStringLiteral("Top Center");
	case TopFrontLeft: return QStringLiteral("Top Front Left");
	case TopFrontCenter: return QStringLiteral("Top Front Center");
	case TopFrontRight: return QStringLiteral("Top Front Right");
	case TopRearLeft: return QStringLiteral("Top Rear Left");
	case TopRearCenter: return QStringLiteral("Top Rear Center");
	case TopRearRight: return QStringLiteral("Top Rear Right");
	case RearLeftCenter: return QStringLiteral("Rear Left Center");
	case RearRightCenter: return QStringLiteral("Rear Right Center");
	case FrontLeftWide: return QStringLiteral("Front Left Wide");
	case FrontRightWide: return QStringLiteral("Front Right Wide");
	case LowFrequencyEffects2: return QStringLiteral("Low Frequency Effects 2");
	case FrontLeftHigh: return QStringLiteral("Front Left High");
	case FrontCenterHigh: return QStringLiteral("Front Center High");
	case FrontRightHigh: return QStringLiteral("Front Right High");
	case TopFrontLeftCenter: return QStringLiteral("Top Front Left Center");
	case TopFrontRightCenter: return QStringLiteral("Top Front Right Center");
	case TopSideLeft: return QStringLiteral("Top Side Left");
	case TopSideRight: return QStringLiteral("Top Side Right");
	case LowFrequencyEffectsLeft: return QStringLiteral("Low Frequency Effects Left");
	case LowFrequencyEffectsRight: return QStringLiteral("Low Frequency Effects Right");
	case BottomCenter: return QStringLiteral("Bottom Center");
	case BottomLeftCenter: return QStringLiteral("Bottom Left Center");
	case BottomRightCenter: return QStringLiteral("Bottom Right Center");
	default:
		if (value >= AuxRangeStart && value <= AuxRangeEnd) {
			return QStringLiteral("Aux %1").arg(value - AuxRangeStart);
		}
		if (value >= CustomRangeStart) return QStringLiteral("Custom %1").arg(value - CustomRangeStart);
		return QStringLiteral("Unknown");
	}
}

QString PwNodeType::toString(PwNodeType::Flags type) {
	switch (type.toInt()) {
	case PwNodeType::VideoSource: return QStringLiteral("VideoSource");
	case PwNodeType::VideoSink: return QStringLiteral("VideoSink");
	case PwNodeType::AudioSource: return QStringLiteral("AudioSource");
	case PwNodeType::AudioSink: return QStringLiteral("AudioSink");
	case PwNodeType::AudioDuplex: return QStringLiteral("AudioDuplex");
	case PwNodeType::AudioOutStream: return QStringLiteral("AudioOutStream");
	case PwNodeType::AudioInStream: return QStringLiteral("AudioInStream");
	case PwNodeType::Untracked: return QStringLiteral("Untracked");
	default: return QStringLiteral("Invalid");
	}
}

QString PwLinkState::toString(PwLinkState::Enum value) {
	switch (value) {
	case Error: return QStringLiteral("error");
	case Unlinked: return QStringLiteral("unlinked");
	case Init: return QStringLiteral("init");
	case Negotiating: return QStringLiteral("negotiating");
	case Allocating: return QStringLiteral("allocating");
	case Paused: return QStringLiteral("paused");
	case Active: return QStringLiteral("active");
	}
	return QStringLiteral("invalid");
}

PwNodeAudioIface::PwNodeAudioIface(AudioObjectID deviceId, AudioDirection direction, QObject* parent)
    : QObject(parent)
    , deviceId(deviceId)
    , direction(direction)
    , channelElements(coreaudio::stereoChannelElements(deviceId, direction)) {
	if (this->channelElements.size() == 2) {
		this->mChannels = {PwAudioChannel::FrontLeft, PwAudioChannel::FrontRight};
	} else {
		this->mChannels = {PwAudioChannel::Mono};
	}

	this->refresh();
	this->listenForChanges();
}

void PwNodeAudioIface::listenForChanges() {
	auto scope = coreaudio::scopeFor(this->direction);
	auto onChange = [this]() { this->scheduleRefresh(); };

	this->listeners.push_back(std::make_unique<PropertyListener>(
	    this->deviceId,
	    coreaudio::virtualMainVolumeSelector(),
	    scope,
	    kAudioObjectPropertyElementMain,
	    onChange
	));

	this->listeners.push_back(std::make_unique<PropertyListener>(
	    this->deviceId,
	    kAudioDevicePropertyMute,
	    scope,
	    kAudioObjectPropertyElementMain,
	    onChange
	));

	for (auto element: this->channelElements) {
		this->listeners.push_back(std::make_unique<PropertyListener>(
		    this->deviceId,
		    kAudioDevicePropertyVolumeScalar,
		    scope,
		    element,
		    onChange
		));
	}
}

void PwNodeAudioIface::scheduleRefresh() {
	runOnObjectThread(this, [this]() { this->refresh(); });
}

void PwNodeAudioIface::refresh() {
	auto muted = coreaudio::isMuted(this->deviceId, this->direction).value_or(false);
	auto mainVolume = coreaudio::mainVolume(this->deviceId, this->direction);

	QVector<float> volumes;
	for (auto element: this->channelElements) {
		volumes.append(coreaudio::channelVolume(this->deviceId, this->direction, element).value_or(0));
	}
	if (volumes.isEmpty()) volumes.append(mainVolume.value_or(0));

	auto volumesDiffer = volumes != this->mVolumes || mainVolume.value_or(0) != this->mMainVolume;

	this->mHasMainVolume = mainVolume.has_value();
	this->mMainVolume = mainVolume.value_or(0);
	this->mVolumes = volumes;

	if (muted != this->mMuted) {
		this->mMuted = muted;
		emit this->mutedChanged();
	}

	if (volumesDiffer) emit this->volumesChanged();
}

float PwNodeAudioIface::averageVolume() const {
	if (this->mHasMainVolume) return this->mMainVolume;
	if (this->mVolumes.isEmpty()) return 0;

	float total = 0;
	for (auto volume: this->mVolumes) total += volume;
	return total / static_cast<float>(this->mVolumes.size());
}

void PwNodeAudioIface::setAverageVolume(float volume) {
	if (!coreaudio::setMainVolume(this->deviceId, this->direction, volume)) {
		for (auto element: this->channelElements) {
			coreaudio::setChannelVolume(this->deviceId, this->direction, element, volume);
		}
	}

	this->refresh();
}

void PwNodeAudioIface::setVolumes(const QVector<float>& volumes) {
	if (volumes.isEmpty()) return;

	if (volumes.size() == this->channelElements.size()) {
		for (qsizetype channelIndex = 0; channelIndex < volumes.size(); ++channelIndex) {
			coreaudio::setChannelVolume(
			    this->deviceId,
			    this->direction,
			    this->channelElements.at(channelIndex),
			    volumes.at(channelIndex)
			);
		}
	} else {
		float total = 0;
		for (auto volume: volumes) total += volume;
		coreaudio::setMainVolume(this->deviceId, this->direction, total / static_cast<float>(volumes.size()));
	}

	this->refresh();
}

void PwNodeAudioIface::setMuted(bool muted) {
	coreaudio::setMuted(this->deviceId, this->direction, muted);
	this->refresh();
}

PwNodeIface::PwNodeIface(AudioObjectID deviceId, AudioDirection direction, QObject* parent)
    : QObject(parent)
    , mDeviceId(deviceId)
    , mDirection(direction)
    , mName(coreaudio::deviceUniqueIdentifier(deviceId))
    , mDescription(coreaudio::deviceName(deviceId))
    , mAudio(new PwNodeAudioIface(deviceId, direction, this)) {
	auto transport = coreaudio::deviceTransport(deviceId);
	auto isOutput = direction == AudioDirection::Output;

	this->mProperties = {
	    {QStringLiteral("object.id"), this->id()},
	    {QStringLiteral("node.name"), this->mName},
	    {QStringLiteral("node.description"), this->mDescription},
	    {QStringLiteral("node.nick"), this->mDescription},
	    {QStringLiteral("media.class"), isOutput ? QStringLiteral("Audio/Sink") : QStringLiteral("Audio/Source")},
	    {QStringLiteral("device.api"), QStringLiteral("coreaudio")},
	    {QStringLiteral("device.bus"), transportBusName(transport)},
	    {QStringLiteral("device.vendor.name"), coreaudio::deviceManufacturer(deviceId)},
	    {QStringLiteral("device.icon-name"), deviceIconName(transport, direction)},
	};
}

quint32 PwNodeIface::id() const {
	return this->mDeviceId * 2 + (this->mDirection == AudioDirection::Input ? 1 : 0);
}

PwNodeType::Flags PwNodeIface::type() const {
	return this->isSink() ? PwNodeType::AudioSink : PwNodeType::AudioSource;
}

AudioGraph* AudioGraph::instance() {
	static auto* sharedInstance = new AudioGraph();
	return sharedInstance;
}

AudioGraph::AudioGraph() {
	auto onDevicesChanged = [this]() { this->scheduleRefresh(); };

	for (auto selector: {kAudioHardwarePropertyDevices,
	                     kAudioHardwarePropertyDefaultOutputDevice,
	                     kAudioHardwarePropertyDefaultInputDevice})
	{
		this->listeners.push_back(std::make_unique<PropertyListener>(
		    kAudioObjectSystemObject,
		    selector,
		    kAudioObjectPropertyScopeGlobal,
		    kAudioObjectPropertyElementMain,
		    onDevicesChanged
		));
	}

	this->refreshDevices();
}

quint64 AudioGraph::nodeKey(AudioObjectID deviceId, AudioDirection direction) {
	return (static_cast<quint64>(deviceId) << 1) | (direction == AudioDirection::Input ? 1 : 0);
}

void AudioGraph::scheduleRefresh() {
	runOnObjectThread(this, [this]() { this->refreshDevices(); });
}

void AudioGraph::refreshDevices() {
	QSet<quint64> presentKeys;

	for (auto deviceId: coreaudio::deviceIdentifiers()) {
		if (coreaudio::isDeviceHidden(deviceId)) continue;

		for (auto direction: {AudioDirection::Output, AudioDirection::Input}) {
			if (!coreaudio::deviceHasStreams(deviceId, direction)) continue;

			auto key = nodeKey(deviceId, direction);
			presentKeys.insert(key);

			if (!this->mNodesByKey.contains(key)) {
				auto* node = new PwNodeIface(deviceId, direction, this);
				this->mNodesByKey.insert(key, node);
				this->mNodes.insertObject(node);
			}
		}
	}

	for (auto iterator = this->mNodesByKey.begin(); iterator != this->mNodesByKey.end();) {
		if (presentKeys.contains(iterator.key())) {
			++iterator;
			continue;
		}

		auto* removedNode = iterator.value();
		this->mNodes.removeObject(removedNode);
		iterator = this->mNodesByKey.erase(iterator);
		removedNode->deleteLater();
	}

	this->refreshDefaults();
}

void AudioGraph::refreshDefaults() {
	auto* sink = this->mNodesByKey.value(
	    nodeKey(coreaudio::defaultDevice(AudioDirection::Output), AudioDirection::Output)
	);
	auto* source = this->mNodesByKey.value(
	    nodeKey(coreaudio::defaultDevice(AudioDirection::Input), AudioDirection::Input)
	);

	if (sink != this->mDefaultSink) {
		this->mDefaultSink = sink;
		emit this->defaultAudioSinkChanged();
	}

	if (source != this->mDefaultSource) {
		this->mDefaultSource = source;
		emit this->defaultAudioSourceChanged();
	}
}

PwNodeIface* AudioGraph::defaultNode(AudioDirection direction) const {
	return direction == AudioDirection::Output ? this->mDefaultSink : this->mDefaultSource;
}

void AudioGraph::setDefaultNode(AudioDirection direction, PwNodeIface* node) {
	if (!node || node->direction() != direction) return;

	coreaudio::setDefaultDevice(direction, node->deviceId());
	this->refreshDefaults();
}

Pipewire::Pipewire(QObject* parent): QObject(parent) {
	auto* graph = AudioGraph::instance();

	QObject::connect(graph, &AudioGraph::defaultAudioSinkChanged, this, &Pipewire::defaultAudioSinkChanged);
	QObject::connect(graph, &AudioGraph::defaultAudioSinkChanged, this, &Pipewire::defaultConfiguredAudioSinkChanged);
	QObject::connect(graph, &AudioGraph::defaultAudioSourceChanged, this, &Pipewire::defaultAudioSourceChanged);
	QObject::connect(graph, &AudioGraph::defaultAudioSourceChanged, this, &Pipewire::defaultConfiguredAudioSourceChanged);
}

PwNodeIface* Pipewire::defaultAudioSink() {
	return AudioGraph::instance()->defaultNode(AudioDirection::Output);
}

PwNodeIface* Pipewire::defaultAudioSource() {
	return AudioGraph::instance()->defaultNode(AudioDirection::Input);
}

void Pipewire::setPreferredDefaultAudioSink(PwNodeIface* node) {
	AudioGraph::instance()->setDefaultNode(AudioDirection::Output, node);
}

void Pipewire::setPreferredDefaultAudioSource(PwNodeIface* node) {
	AudioGraph::instance()->setDefaultNode(AudioDirection::Input, node);
}

void PwNodeLinkTracker::setNode(PwNodeIface* node) {
	if (node == this->mNode) return;
	this->mNode = node;
	emit this->nodeChanged();
}

QQmlListProperty<PwLinkGroupIface> PwNodeLinkTracker::linkGroups() {
	return QQmlListProperty<PwLinkGroupIface>(this, &this->mLinkGroups);
}

void PwObjectTracker::setObjects(const QList<QObject*>& objects) {
	if (objects == this->mObjects) return;
	this->mObjects = objects;
	emit this->objectsChanged();
}

PwNodePeakMonitor::PwNodePeakMonitor(QObject* parent)
    : QObject(parent)
    , mMeter(std::make_shared<coreaudio::LevelMeter>()) {
	this->mSampleTimer.setInterval(PEAK_SAMPLE_INTERVAL_MILLISECONDS);
	QObject::connect(&this->mSampleTimer, &QTimer::timeout, this, &PwNodePeakMonitor::samplePeaks);
}

PwNodePeakMonitor::~PwNodePeakMonitor() {
	auto meter = this->mMeter;
	dispatch_async(meterControlQueue(), ^{ meter->stop(); });
}

void PwNodePeakMonitor::setNode(PwNodeIface* node) {
	if (node == this->mNode) return;
	this->mNode = node;
	emit this->nodeChanged();
	emit this->channelsChanged();
	this->restartMeter();
}

void PwNodePeakMonitor::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;
	emit this->enabledChanged();
	this->restartMeter();
}

void PwNodePeakMonitor::restartMeter() {
	this->mSampleTimer.stop();
	this->resetPeaks();

	auto generation = ++this->mMeterGeneration;
	auto meter = this->mMeter;
	auto shouldStart = this->mEnabled && this->mNode;
	auto deviceId = shouldStart ? this->mNode->deviceId() : kAudioObjectUnknown;
	auto direction = shouldStart ? this->mNode->direction() : coreaudio::AudioDirection::Output;
	auto channelCount = this->channels().size();
	auto description = shouldStart ? this->mNode->description() : QString();
	QPointer<PwNodePeakMonitor> monitor = this;

	dispatch_async(meterControlQueue(), ^{
		meter->stop();
		if (!shouldStart) return;

		auto started = meter->start(deviceId, direction, channelCount);
		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [monitor, generation, started, description]() {
			    if (!monitor || monitor->mMeterGeneration != generation) return;
			    if (!started) {
				    qWarning() << "Could not start audio level metering for" << description;
				    return;
			    }
			    monitor->mSampleTimer.start();
		    },
		    Qt::QueuedConnection
		);
	});
}

void PwNodePeakMonitor::resetPeaks() {
	this->mPeaks = QVector<float>(this->channels().size(), 0.0F);
	this->mPeak = 0;
	emit this->peaksChanged();
	emit this->peakChanged();
}

void PwNodePeakMonitor::samplePeaks() {
	auto peaks = this->mMeter->takePeaks();
	auto peak = peaks.isEmpty() ? 0.0F : *std::ranges::max_element(peaks);

	if (peaks != this->mPeaks) {
		this->mPeaks = peaks;
		emit this->peaksChanged();
	}

	if (!qFuzzyCompare(1.0F + peak, 1.0F + this->mPeak)) {
		this->mPeak = peak;
		emit this->peakChanged();
	}
}

QVector<PwAudioChannel::Enum> PwNodePeakMonitor::channels() const {
	return this->mNode ? this->mNode->audio()->channels() : QVector<PwAudioChannel::Enum>();
}

}

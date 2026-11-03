#include "core_audio.hpp"
#include <algorithm>
#include <array>
#include <utility>

#include <AudioToolbox/AudioServices.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

namespace qs::service::pipewire::coreaudio {

namespace {

AudioObjectPropertyAddress propertyAddress(
    AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal,
    AudioObjectPropertyElement element = kAudioObjectPropertyElementMain
) {
	return AudioObjectPropertyAddress {selector, scope, element};
}

template <typename Value>
std::optional<Value> readProperty(AudioObjectID objectId, const AudioObjectPropertyAddress& address) {
	if (!AudioObjectHasProperty(objectId, &address)) return std::nullopt;

	Value value {};
	UInt32 size = sizeof(Value);
	if (AudioObjectGetPropertyData(objectId, &address, 0, nullptr, &size, &value) != noErr) {
		return std::nullopt;
	}
	return value;
}

template <typename Value>
bool writeProperty(AudioObjectID objectId, const AudioObjectPropertyAddress& address, Value value) {
	if (!AudioObjectHasProperty(objectId, &address)) return false;

	Boolean settable = false;
	if (AudioObjectIsPropertySettable(objectId, &address, &settable) != noErr || !settable) {
		return false;
	}

	return AudioObjectSetPropertyData(objectId, &address, 0, nullptr, sizeof(Value), &value) == noErr;
}

QString readStringProperty(AudioObjectID objectId, AudioObjectPropertySelector selector) {
	auto address = propertyAddress(selector);
	if (!AudioObjectHasProperty(objectId, &address)) return {};

	CFStringRef value = nullptr;
	UInt32 size = sizeof(CFStringRef);
	if (AudioObjectGetPropertyData(objectId, &address, 0, nullptr, &size, static_cast<void*>(&value)) != noErr || !value) {
		return {};
	}

	auto result = QString::fromCFString(value);
	CFRelease(value);
	return result;
}

AudioObjectPropertySelector defaultDeviceSelector(AudioDirection direction) {
	return direction == AudioDirection::Output ? kAudioHardwarePropertyDefaultOutputDevice
	                                           : kAudioHardwarePropertyDefaultInputDevice;
}

}

AudioObjectPropertyScope scopeFor(AudioDirection direction) {
	return direction == AudioDirection::Output ? kAudioDevicePropertyScopeOutput
	                                           : kAudioDevicePropertyScopeInput;
}

AudioObjectPropertySelector virtualMainVolumeSelector() {
	return kAudioHardwareServiceDeviceProperty_VirtualMainVolume;
}

QList<AudioObjectID> deviceIdentifiers() {
	auto address = propertyAddress(kAudioHardwarePropertyDevices);

	UInt32 size = 0;
	if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
		return {};
	}

	QList<AudioObjectID> identifiers(static_cast<qsizetype>(size / sizeof(AudioObjectID)));
	if (AudioObjectGetPropertyData(
	        kAudioObjectSystemObject,
	        &address,
	        0,
	        nullptr,
	        &size,
	        identifiers.data()
	    )
	    != noErr)
	{
		return {};
	}

	identifiers.resize(static_cast<qsizetype>(size / sizeof(AudioObjectID)));
	return identifiers;
}

bool deviceHasStreams(AudioObjectID deviceId, AudioDirection direction) {
	auto address = propertyAddress(kAudioDevicePropertyStreams, scopeFor(direction));

	UInt32 size = 0;
	return AudioObjectGetPropertyDataSize(deviceId, &address, 0, nullptr, &size) == noErr && size > 0;
}

bool isDeviceHidden(AudioObjectID deviceId) {
	return readProperty<UInt32>(deviceId, propertyAddress(kAudioDevicePropertyIsHidden)).value_or(0)
	    != 0;
}

QString deviceName(AudioObjectID deviceId) {
	return readStringProperty(deviceId, kAudioObjectPropertyName);
}

QString deviceUniqueIdentifier(AudioObjectID deviceId) {
	return readStringProperty(deviceId, kAudioDevicePropertyDeviceUID);
}

QString deviceManufacturer(AudioObjectID deviceId) {
	return readStringProperty(deviceId, kAudioObjectPropertyManufacturer);
}

AudioTransport deviceTransport(AudioObjectID deviceId) {
	auto transport =
	    readProperty<UInt32>(deviceId, propertyAddress(kAudioDevicePropertyTransportType)).value_or(0);

	switch (transport) {
	case kAudioDeviceTransportTypeBuiltIn: return AudioTransport::BuiltIn;
	case kAudioDeviceTransportTypeBluetooth:
	case kAudioDeviceTransportTypeBluetoothLE: return AudioTransport::Bluetooth;
	case kAudioDeviceTransportTypeUSB: return AudioTransport::Usb;
	case kAudioDeviceTransportTypeHDMI:
	case kAudioDeviceTransportTypeDisplayPort: return AudioTransport::Display;
	case kAudioDeviceTransportTypeAirPlay: return AudioTransport::AirPlay;
	case kAudioDeviceTransportTypeVirtual: return AudioTransport::Virtual;
	case kAudioDeviceTransportTypeAggregate:
	case kAudioDeviceTransportTypeAutoAggregate: return AudioTransport::Aggregate;
	default: return AudioTransport::Unknown;
	}
}

AudioObjectID defaultDevice(AudioDirection direction) {
	return readProperty<AudioObjectID>(
	           kAudioObjectSystemObject,
	           propertyAddress(defaultDeviceSelector(direction))
	)
	    .value_or(kAudioObjectUnknown);
}

bool setDefaultDevice(AudioDirection direction, AudioObjectID deviceId) {
	return writeProperty(
	    kAudioObjectSystemObject,
	    propertyAddress(defaultDeviceSelector(direction)),
	    deviceId
	);
}

std::optional<float> mainVolume(AudioObjectID deviceId, AudioDirection direction) {
	return readProperty<Float32>(
	    deviceId,
	    propertyAddress(virtualMainVolumeSelector(), scopeFor(direction))
	);
}

bool setMainVolume(AudioObjectID deviceId, AudioDirection direction, float volume) {
	return writeProperty<Float32>(
	    deviceId,
	    propertyAddress(virtualMainVolumeSelector(), scopeFor(direction)),
	    std::clamp(volume, 0.0F, 1.0F)
	);
}

std::optional<bool> isMuted(AudioObjectID deviceId, AudioDirection direction) {
	auto muted =
	    readProperty<UInt32>(deviceId, propertyAddress(kAudioDevicePropertyMute, scopeFor(direction)));
	if (!muted) return std::nullopt;
	return *muted != 0;
}

bool setMuted(AudioObjectID deviceId, AudioDirection direction, bool muted) {
	return writeProperty<UInt32>(
	    deviceId,
	    propertyAddress(kAudioDevicePropertyMute, scopeFor(direction)),
	    muted ? 1 : 0
	);
}

QList<quint32> stereoChannelElements(AudioObjectID deviceId, AudioDirection direction) {
	auto address = propertyAddress(kAudioDevicePropertyPreferredChannelsForStereo, scopeFor(direction));
	if (!AudioObjectHasProperty(deviceId, &address)) return {};

	std::array<UInt32, 2> elements {0, 0};
	UInt32 size = sizeof(UInt32) * elements.size();
	if (AudioObjectGetPropertyData(deviceId, &address, 0, nullptr, &size, elements.data()) != noErr) {
		return {};
	}

	QList<quint32> channelElements;
	for (auto element: elements) {
		auto volumeAddress = propertyAddress(kAudioDevicePropertyVolumeScalar, scopeFor(direction), element);
		if (element != 0 && AudioObjectHasProperty(deviceId, &volumeAddress)) {
			channelElements.append(element);
		}
	}
	return channelElements;
}

std::optional<float> channelVolume(AudioObjectID deviceId, AudioDirection direction, quint32 element) {
	return readProperty<Float32>(
	    deviceId,
	    propertyAddress(kAudioDevicePropertyVolumeScalar, scopeFor(direction), element)
	);
}

bool setChannelVolume(AudioObjectID deviceId, AudioDirection direction, quint32 element, float volume) {
	return writeProperty<Float32>(
	    deviceId,
	    propertyAddress(kAudioDevicePropertyVolumeScalar, scopeFor(direction), element),
	    std::clamp(volume, 0.0F, 1.0F)
	);
}

PropertyListener::PropertyListener(
    AudioObjectID objectId,
    AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope,
    AudioObjectPropertyElement element,
    std::function<void()> onChange
)
    : objectId(objectId)
    , address(propertyAddress(selector, scope, element))
    , onChange(std::move(onChange)) {
	if (!AudioObjectHasProperty(this->objectId, &this->address)) return;

	this->registered = AudioObjectAddPropertyListener(
	                       this->objectId,
	                       &this->address,
	                       &PropertyListener::handlePropertyChange,
	                       this
	                   )
	                == noErr;
}

PropertyListener::~PropertyListener() {
	if (!this->registered) return;

	AudioObjectRemovePropertyListener(
	    this->objectId,
	    &this->address,
	    &PropertyListener::handlePropertyChange,
	    this
	);
}

OSStatus PropertyListener::handlePropertyChange(
    [[maybe_unused]] AudioObjectID objectId,
    [[maybe_unused]] UInt32 addressCount,
    [[maybe_unused]] const AudioObjectPropertyAddress* addresses,
    void* context
) {
	auto* listener = static_cast<PropertyListener*>(context);
	if (listener->onChange) listener->onChange();
	return noErr;
}

}

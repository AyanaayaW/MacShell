#pragma once

#include <functional>
#include <optional>

#include <CoreAudio/CoreAudio.h>
#include <qlist.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::service::pipewire::coreaudio {

enum class AudioDirection : quint8 {
	Output,
	Input,
};

enum class AudioTransport : quint8 {
	Unknown,
	BuiltIn,
	Bluetooth,
	Usb,
	Display,
	AirPlay,
	Virtual,
	Aggregate,
};

QList<AudioObjectID> deviceIdentifiers();
bool deviceHasStreams(AudioObjectID deviceId, AudioDirection direction);
bool isDeviceHidden(AudioObjectID deviceId);
QString deviceName(AudioObjectID deviceId);
QString deviceUniqueIdentifier(AudioObjectID deviceId);
QString deviceManufacturer(AudioObjectID deviceId);
AudioTransport deviceTransport(AudioObjectID deviceId);

AudioObjectID defaultDevice(AudioDirection direction);
bool setDefaultDevice(AudioDirection direction, AudioObjectID deviceId);

std::optional<float> mainVolume(AudioObjectID deviceId, AudioDirection direction);
bool setMainVolume(AudioObjectID deviceId, AudioDirection direction, float volume);

std::optional<bool> isMuted(AudioObjectID deviceId, AudioDirection direction);
bool setMuted(AudioObjectID deviceId, AudioDirection direction, bool muted);

QList<quint32> stereoChannelElements(AudioObjectID deviceId, AudioDirection direction);
std::optional<float> channelVolume(AudioObjectID deviceId, AudioDirection direction, quint32 element);
bool setChannelVolume(AudioObjectID deviceId, AudioDirection direction, quint32 element, float volume);

AudioObjectPropertyScope scopeFor(AudioDirection direction);
AudioObjectPropertySelector virtualMainVolumeSelector();

class PropertyListener {
public:
	PropertyListener(
	    AudioObjectID objectId,
	    AudioObjectPropertySelector selector,
	    AudioObjectPropertyScope scope,
	    AudioObjectPropertyElement element,
	    std::function<void()> onChange
	);
	~PropertyListener();
	PropertyListener(const PropertyListener&) = delete;
	PropertyListener& operator=(const PropertyListener&) = delete;
	PropertyListener(PropertyListener&&) = delete;
	PropertyListener& operator=(PropertyListener&&) = delete;

private:
	static OSStatus handlePropertyChange(
	    AudioObjectID objectId,
	    UInt32 addressCount,
	    const AudioObjectPropertyAddress* addresses,
	    void* context
	);

	AudioObjectID objectId;
	AudioObjectPropertyAddress address;
	std::function<void()> onChange;
	bool registered = false;
};

}

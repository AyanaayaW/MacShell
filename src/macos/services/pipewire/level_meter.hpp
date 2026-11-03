#pragma once

#include <mutex>

#include <CoreAudio/CoreAudio.h>
#include <qlist.h>
#include <qtypes.h>

#include "core_audio.hpp"

namespace qs::service::pipewire::coreaudio {

class LevelMeter {
public:
	LevelMeter() = default;
	~LevelMeter();
	LevelMeter(const LevelMeter&) = delete;
	LevelMeter& operator=(const LevelMeter&) = delete;
	LevelMeter(LevelMeter&&) = delete;
	LevelMeter& operator=(LevelMeter&&) = delete;

	bool start(AudioObjectID deviceId, AudioDirection direction, qsizetype channelCount);
	void stop();

	[[nodiscard]] bool isRunning() const { return this->ioProcId != nullptr; }
	[[nodiscard]] QList<float> takePeaks();

	void recordBuffers(const AudioBufferList* buffers);

private:
	bool startOutputTap(AudioObjectID deviceId);

	AudioObjectID meteredDeviceId = kAudioObjectUnknown;
	AudioObjectID tapId = kAudioObjectUnknown;
	AudioObjectID aggregateDeviceId = kAudioObjectUnknown;
	AudioDeviceIOProcID ioProcId = nullptr;
	qsizetype channelCount = 1;

	std::mutex peaksMutex;
	QList<float> accumulatedPeaks;
};

}

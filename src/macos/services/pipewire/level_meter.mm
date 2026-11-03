#include "level_meter.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>

#include <qlist.h>
#include <qlogging.h>
#include <qstring.h>

#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

namespace qs::service::pipewire::coreaudio {

LevelMeter::~LevelMeter() { this->stop(); }

bool LevelMeter::start(AudioObjectID deviceId, AudioDirection direction, qsizetype channelCount) {
	this->stop();
	this->channelCount = std::max<qsizetype>(channelCount, 1);
	this->accumulatedPeaks = QList<float>(this->channelCount, 0.0F);

	if (direction == AudioDirection::Output) {
		if (!this->startOutputTap(deviceId)) return false;
	} else {
		this->meteredDeviceId = deviceId;
	}

	auto ioBlock = ^(
	    const AudioTimeStamp*,
	    const AudioBufferList* inputData,
	    const AudioTimeStamp*,
	    AudioBufferList*,
	    const AudioTimeStamp*
	) {
		this->recordBuffers(inputData);
	};

	if (AudioDeviceCreateIOProcIDWithBlock(&this->ioProcId, this->meteredDeviceId, nullptr, ioBlock) != noErr) {
		this->ioProcId = nullptr;
		this->stop();
		return false;
	}

	if (AudioDeviceStart(this->meteredDeviceId, this->ioProcId) != noErr) {
		this->stop();
		return false;
	}

	return true;
}

bool LevelMeter::startOutputTap(AudioObjectID deviceId) {
	if (@available(macOS 14.2, *)) {
		CATapDescription* description = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
		description.privateTap = YES;
		description.muteBehavior = CATapUnmuted;
		description.name = @"MacShell Level Meter";

		if (AudioHardwareCreateProcessTap(description, &this->tapId) != noErr) {
			this->tapId = kAudioObjectUnknown;
			return false;
		}

		auto outputUid = deviceUniqueIdentifier(deviceId).toNSString();
		NSString* aggregateUid = [NSUUID UUID].UUIDString;
		NSDictionary* aggregateDescription = @{
			@kAudioAggregateDeviceNameKey: @"MacShell Level Meter",
			@kAudioAggregateDeviceUIDKey: aggregateUid,
			@kAudioAggregateDeviceMainSubDeviceKey: outputUid,
			@kAudioAggregateDeviceIsPrivateKey: @YES,
			@kAudioAggregateDeviceIsStackedKey: @NO,
			@kAudioAggregateDeviceTapAutoStartKey: @YES,
			@kAudioAggregateDeviceSubDeviceListKey: @[@{@kAudioSubDeviceUIDKey: outputUid}],
			@kAudioAggregateDeviceTapListKey: @[@{
				@kAudioSubTapDriftCompensationKey: @YES,
				@kAudioSubTapUIDKey: description.UUID.UUIDString,
			}],
		};

		if (AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggregateDescription, &this->aggregateDeviceId)
		    != noErr)
		{
			this->aggregateDeviceId = kAudioObjectUnknown;
			return false;
		}

		this->meteredDeviceId = this->aggregateDeviceId;
		return true;
	}

	return false;
}

void LevelMeter::stop() {
	if (this->ioProcId != nullptr) {
		AudioDeviceStop(this->meteredDeviceId, this->ioProcId);
		AudioDeviceDestroyIOProcID(this->meteredDeviceId, this->ioProcId);
		this->ioProcId = nullptr;
	}

	if (this->aggregateDeviceId != kAudioObjectUnknown) {
		AudioHardwareDestroyAggregateDevice(this->aggregateDeviceId);
		this->aggregateDeviceId = kAudioObjectUnknown;
	}

	if (this->tapId != kAudioObjectUnknown) {
		if (@available(macOS 14.2, *)) AudioHardwareDestroyProcessTap(this->tapId);
		this->tapId = kAudioObjectUnknown;
	}

	this->meteredDeviceId = kAudioObjectUnknown;
}

void LevelMeter::recordBuffers(const AudioBufferList* buffers) {
	if (!buffers) return;

	QList<float> bufferPeaks(this->channelCount, 0.0F);
	qsizetype channelOffset = 0;

	for (UInt32 bufferIndex = 0; bufferIndex < buffers->mNumberBuffers; ++bufferIndex) {
		const auto& buffer = buffers->mBuffers[bufferIndex];
		if (!buffer.mData || buffer.mNumberChannels == 0) continue;

		const auto* samples = static_cast<const float*>(buffer.mData);
		auto interleavedChannels = static_cast<qsizetype>(buffer.mNumberChannels);
		auto sampleCount = static_cast<qsizetype>(buffer.mDataByteSize / sizeof(float));

		for (qsizetype sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
			auto channelIndex = std::min(channelOffset + sampleIndex % interleavedChannels, this->channelCount - 1);
			bufferPeaks[channelIndex] = std::max(bufferPeaks[channelIndex], std::abs(samples[sampleIndex]));
		}

		channelOffset += interleavedChannels;
	}

	std::lock_guard lock(this->peaksMutex);
	for (qsizetype channelIndex = 0; channelIndex < this->channelCount; ++channelIndex) {
		this->accumulatedPeaks[channelIndex] = std::max(this->accumulatedPeaks[channelIndex], bufferPeaks[channelIndex]);
	}
}

QList<float> LevelMeter::takePeaks() {
	std::lock_guard lock(this->peaksMutex);
	auto peaks = this->accumulatedPeaks;
	std::ranges::fill(this->accumulatedPeaks, 0.0F);
	return peaks;
}

}

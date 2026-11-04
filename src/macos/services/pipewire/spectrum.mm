#include "spectrum.hpp"
#include <algorithm>
#include <cmath>

#include <qcoreapplication.h>
#include <qlogging.h>

#include <Accelerate/Accelerate.h>
#include <dispatch/dispatch.h>

namespace qs::service::pipewire {

namespace {

constexpr int FFT_LOG2_SIZE = 12;
constexpr int FFT_SIZE = 1 << FFT_LOG2_SIZE;
constexpr int QUIET_FRAMES_BEFORE_IDLE = 30;
constexpr float SILENCE_THRESHOLD = 0.01F;
constexpr float PEAK_DECAY_PER_FRAME = 0.995F;
constexpr float MINIMUM_RUNNING_PEAK = 0.02F;
constexpr float RISE_RATE = 0.65F;

dispatch_queue_t spectrumControlQueue() {
	static dispatch_queue_t queue = dispatch_queue_create("MacShell.SpectrumControl", DISPATCH_QUEUE_SERIAL);
	return queue;
}

FFTSetup sharedFftSetup() {
	static FFTSetup setup = vDSP_create_fftsetup(FFT_LOG2_SIZE, kFFTRadix2);
	return setup;
}

}

PwAudioSpectrum::PwAudioSpectrum(QObject* parent)
    : QObject(parent)
    , mMeter(std::make_shared<coreaudio::LevelMeter>())
    , mWindow(FFT_SIZE) {
	vDSP_hann_window(this->mWindow.data(), FFT_SIZE, vDSP_HANN_NORM);
	this->mMeter->enableSampleCapture(FFT_SIZE);
	this->mFrameTimer.setTimerType(Qt::PreciseTimer);
	QObject::connect(&this->mFrameTimer, &QTimer::timeout, this, &PwAudioSpectrum::computeFrame);
	this->resetValues();
}

PwAudioSpectrum::~PwAudioSpectrum() {
	auto meter = this->mMeter;
	dispatch_async(spectrumControlQueue(), ^{ meter->stop(); });
}

void PwAudioSpectrum::setNode(PwNodeIface* node) {
	if (node == this->mNode) return;
	this->mNode = node;
	emit this->nodeChanged();
	this->restartCapture();
}

void PwAudioSpectrum::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;
	emit this->enabledChanged();
	this->restartCapture();
}

void PwAudioSpectrum::setBandCount(int bandCount) {
	bandCount = std::clamp(bandCount, 1, 512);
	if (bandCount == this->mBandCount) return;
	this->mBandCount = bandCount;
	emit this->bandCountChanged();
	this->resetValues();
}

void PwAudioSpectrum::setFrameRate(int frameRate) {
	frameRate = std::clamp(frameRate, 1, 240);
	if (frameRate == this->mFrameRate) return;
	this->mFrameRate = frameRate;
	emit this->frameRateChanged();
	if (this->mFrameTimer.isActive()) this->mFrameTimer.start(1000 / this->mFrameRate);
}

void PwAudioSpectrum::setLowerCutoff(int lowerCutoff) {
	if (lowerCutoff == this->mLowerCutoff) return;
	this->mLowerCutoff = std::max(lowerCutoff, 1);
	emit this->lowerCutoffChanged();
}

void PwAudioSpectrum::setUpperCutoff(int upperCutoff) {
	if (upperCutoff == this->mUpperCutoff) return;
	this->mUpperCutoff = std::max(upperCutoff, this->mLowerCutoff + 1);
	emit this->upperCutoffChanged();
}

void PwAudioSpectrum::setNoiseReduction(qreal noiseReduction) {
	noiseReduction = std::clamp(noiseReduction, 0.0, 0.99);
	if (qFuzzyCompare(noiseReduction, this->mNoiseReduction)) return;
	this->mNoiseReduction = noiseReduction;
	emit this->noiseReductionChanged();
}

void PwAudioSpectrum::setSmoothing(bool smoothing) {
	if (smoothing == this->mSmoothing) return;
	this->mSmoothing = smoothing;
	emit this->smoothingChanged();
}

void PwAudioSpectrum::setIdle(bool idle) {
	if (idle == this->mIdle) return;
	this->mIdle = idle;
	emit this->idleChanged();
}

void PwAudioSpectrum::resetValues() {
	this->mValues = QList<float>(this->mBandCount, 0.0F);
	emit this->valuesChanged();
}

void PwAudioSpectrum::restartCapture() {
	this->mFrameTimer.stop();
	this->resetValues();
	this->setIdle(true);

	auto generation = ++this->mCaptureGeneration;
	auto meter = this->mMeter;
	auto shouldStart = this->mEnabled && this->mNode;
	auto deviceId = shouldStart ? this->mNode->deviceId() : kAudioObjectUnknown;
	auto direction = shouldStart ? this->mNode->direction() : coreaudio::AudioDirection::Output;
	QPointer<PwAudioSpectrum> spectrum = this;

	dispatch_async(spectrumControlQueue(), ^{
		meter->stop();
		if (!shouldStart) return;

		auto started = meter->start(deviceId, direction, 2);
		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [spectrum, generation, started]() {
			    if (!spectrum || spectrum->mCaptureGeneration != generation) return;
			    if (!started) {
				    qWarning() << "Could not start audio capture for the spectrum; allow MacShell to record system audio.";
				    return;
			    }
			    spectrum->mFrameTimer.start(1000 / spectrum->mFrameRate);
		    },
		    Qt::QueuedConnection
		);
	});
}

void PwAudioSpectrum::computeFrame() {
	auto samples = this->mMeter->latestSamples(FFT_SIZE);
	vDSP_vmul(samples.data(), 1, this->mWindow.data(), 1, samples.data(), 1, FFT_SIZE);

	std::vector<float> realParts(FFT_SIZE / 2);
	std::vector<float> imaginaryParts(FFT_SIZE / 2);
	DSPSplitComplex splitComplex {realParts.data(), imaginaryParts.data()};
	vDSP_ctoz(reinterpret_cast<const DSPComplex*>(samples.data()), 2, &splitComplex, 1, FFT_SIZE / 2);
	vDSP_fft_zrip(sharedFftSetup(), &splitComplex, 1, FFT_LOG2_SIZE, kFFTDirection_Forward);

	std::vector<float> magnitudes(FFT_SIZE / 2);
	vDSP_zvabs(&splitComplex, 1, magnitudes.data(), 1, FFT_SIZE / 2);

	auto binWidth = this->mMeter->sampleRate() / FFT_SIZE;
	auto lowerFrequency = static_cast<double>(this->mLowerCutoff);
	auto upperFrequency = std::min(static_cast<double>(this->mUpperCutoff), this->mMeter->sampleRate() / 2);
	auto frequencyRatio = upperFrequency / lowerFrequency;

	QList<float> bands(this->mBandCount, 0.0F);
	float framePeak = 0.0F;
	for (int band = 0; band < this->mBandCount; ++band) {
		auto bandStart = lowerFrequency * std::pow(frequencyRatio, static_cast<double>(band) / this->mBandCount);
		auto bandEnd = lowerFrequency * std::pow(frequencyRatio, static_cast<double>(band + 1) / this->mBandCount);
		auto firstBin = std::clamp(static_cast<int>(bandStart / binWidth), 1, FFT_SIZE / 2 - 1);
		auto lastBin = std::clamp(static_cast<int>(std::ceil(bandEnd / binWidth)), firstBin + 1, FFT_SIZE / 2);

		float energy = 0.0F;
		for (int bin = firstBin; bin < lastBin; ++bin) energy = std::max(energy, magnitudes[static_cast<size_t>(bin)]);
		auto level = std::log10(1.0F + energy);
		bands[band] = level;
		framePeak = std::max(framePeak, level);
	}

	this->mRunningPeak = std::max({framePeak, this->mRunningPeak * PEAK_DECAY_PER_FRAME, MINIMUM_RUNNING_PEAK});
	auto fallRate = static_cast<float>(this->mNoiseReduction);

	bool silent = true;
	for (int band = 0; band < this->mBandCount; ++band) {
		auto target = std::clamp(bands[band] / this->mRunningPeak, 0.0F, 1.0F);
		auto previous = this->mValues.value(band);
		float next = target;
		if (this->mSmoothing) {
			next = target > previous ? previous + (target - previous) * RISE_RATE : previous * fallRate + target * (1.0F - fallRate);
		}
		bands[band] = next;
		if (next > SILENCE_THRESHOLD) silent = false;
	}

	this->mValues = bands;
	emit this->valuesChanged();

	this->mQuietFrames = silent ? this->mQuietFrames + 1 : 0;
	this->setIdle(this->mQuietFrames >= QUIET_FRAMES_BEFORE_IDLE);
}

}

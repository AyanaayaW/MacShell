#pragma once

#include <memory>
#include <vector>

#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "level_meter.hpp"
#include "pipewire.hpp"

namespace qs::service::pipewire {

class PwAudioSpectrum: public QObject {
	Q_OBJECT;
	Q_PROPERTY(qs::service::pipewire::PwNodeIface* node READ node WRITE setNode NOTIFY nodeChanged);
	Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(int bandCount READ bandCount WRITE setBandCount NOTIFY bandCountChanged);
	Q_PROPERTY(int barCount READ bandCount WRITE setBandCount NOTIFY bandCountChanged);
	Q_PROPERTY(int frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged);
	Q_PROPERTY(int lowerCutoff READ lowerCutoff WRITE setLowerCutoff NOTIFY lowerCutoffChanged);
	Q_PROPERTY(int upperCutoff READ upperCutoff WRITE setUpperCutoff NOTIFY upperCutoffChanged);
	Q_PROPERTY(qreal noiseReduction READ noiseReduction WRITE setNoiseReduction NOTIFY noiseReductionChanged);
	Q_PROPERTY(bool smoothing READ smoothing WRITE setSmoothing NOTIFY smoothingChanged);
	Q_PROPERTY(QList<float> values READ values NOTIFY valuesChanged);
	Q_PROPERTY(bool idle READ isIdle NOTIFY idleChanged);
	QML_ELEMENT;

public:
	explicit PwAudioSpectrum(QObject* parent = nullptr);
	~PwAudioSpectrum() override;
	Q_DISABLE_COPY_MOVE(PwAudioSpectrum);

	[[nodiscard]] PwNodeIface* node() const { return this->mNode; }
	void setNode(PwNodeIface* node);
	[[nodiscard]] bool isEnabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);
	[[nodiscard]] int bandCount() const { return this->mBandCount; }
	void setBandCount(int bandCount);
	[[nodiscard]] int frameRate() const { return this->mFrameRate; }
	void setFrameRate(int frameRate);
	[[nodiscard]] int lowerCutoff() const { return this->mLowerCutoff; }
	void setLowerCutoff(int lowerCutoff);
	[[nodiscard]] int upperCutoff() const { return this->mUpperCutoff; }
	void setUpperCutoff(int upperCutoff);
	[[nodiscard]] qreal noiseReduction() const { return this->mNoiseReduction; }
	void setNoiseReduction(qreal noiseReduction);
	[[nodiscard]] bool smoothing() const { return this->mSmoothing; }
	void setSmoothing(bool smoothing);
	[[nodiscard]] QList<float> values() const { return this->mValues; }
	[[nodiscard]] bool isIdle() const { return this->mIdle; }

signals:
	void nodeChanged();
	void enabledChanged();
	void bandCountChanged();
	void frameRateChanged();
	void lowerCutoffChanged();
	void upperCutoffChanged();
	void noiseReductionChanged();
	void smoothingChanged();
	void valuesChanged();
	void idleChanged();

private:
	void restartCapture();
	void computeFrame();
	void setIdle(bool idle);
	void resetValues();

	QPointer<PwNodeIface> mNode;
	bool mEnabled = true;
	int mBandCount = 32;
	int mFrameRate = 60;
	int mLowerCutoff = 50;
	int mUpperCutoff = 12000;
	qreal mNoiseReduction = 0.77;
	bool mSmoothing = true;
	QList<float> mValues;
	bool mIdle = true;
	int mQuietFrames = 0;
	float mRunningPeak = 0.0F;

	std::shared_ptr<coreaudio::LevelMeter> mMeter;
	quint64 mCaptureGeneration = 0;
	QTimer mFrameTimer;
	std::vector<float> mWindow;
};

}

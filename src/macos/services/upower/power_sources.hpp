#pragma once

#include <functional>

#include <qlist.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::service::upower {

enum class PowerSourceKind : quint8 {
	InternalBattery,
	UninterruptiblePowerSupply,
	PowerAdapter,
	Mouse,
	Keyboard,
	Touchpad,
	Headphones,
	OtherPeripheral,
};

enum class PowerSourceStatus : quint8 {
	Unknown,
	Charging,
	Discharging,
	Empty,
	FullyCharged,
	PendingCharge,
};

struct PowerSourceReading {
	QString identifier;
	QString model;
	PowerSourceKind kind = PowerSourceKind::OtherPeripheral;
	PowerSourceStatus status = PowerSourceStatus::Unknown;
	bool isPresent = true;
	qreal percentage = 0;
	qreal energyWattHours = 0;
	qreal energyCapacityWattHours = 0;
	qreal changeRateWatts = 0;
	qint64 secondsToEmpty = 0;
	qint64 secondsToFull = 0;
	qreal healthPercentage = 0;
};

QList<PowerSourceReading> readPowerSources();
bool isRunningOnBattery();

class PowerSourceChangeListener {
public:
	explicit PowerSourceChangeListener(std::function<void()> onChange);
	~PowerSourceChangeListener();
	PowerSourceChangeListener(const PowerSourceChangeListener&) = delete;
	PowerSourceChangeListener& operator=(const PowerSourceChangeListener&) = delete;
	PowerSourceChangeListener(PowerSourceChangeListener&&) = delete;
	PowerSourceChangeListener& operator=(PowerSourceChangeListener&&) = delete;

private:
	static void handleNotification(void* context);

	std::function<void()> onChange;
	void* runLoopSource = nullptr;
};

}

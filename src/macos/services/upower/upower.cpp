#include "upower.hpp"
#include <cmath>

#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtypes.h>

#include "power_sources.hpp"

namespace qs::service::upower {

namespace {

constexpr auto PERIODIC_REFRESH_INTERVAL_MILLISECONDS = 30000;

UPowerDeviceType::Enum deviceTypeFor(PowerSourceKind kind) {
	switch (kind) {
	case PowerSourceKind::InternalBattery: return UPowerDeviceType::Battery;
	case PowerSourceKind::UninterruptiblePowerSupply: return UPowerDeviceType::Ups;
	case PowerSourceKind::PowerAdapter: return UPowerDeviceType::LinePower;
	case PowerSourceKind::Mouse: return UPowerDeviceType::Mouse;
	case PowerSourceKind::Keyboard: return UPowerDeviceType::Keyboard;
	case PowerSourceKind::Touchpad: return UPowerDeviceType::Touchpad;
	case PowerSourceKind::Headphones: return UPowerDeviceType::Headphones;
	case PowerSourceKind::OtherPeripheral: return UPowerDeviceType::BluetoothGeneric;
	}
	return UPowerDeviceType::Unknown;
}

UPowerDeviceState::Enum deviceStateFor(PowerSourceStatus status) {
	switch (status) {
	case PowerSourceStatus::Unknown: return UPowerDeviceState::Unknown;
	case PowerSourceStatus::Charging: return UPowerDeviceState::Charging;
	case PowerSourceStatus::Discharging: return UPowerDeviceState::Discharging;
	case PowerSourceStatus::Empty: return UPowerDeviceState::Empty;
	case PowerSourceStatus::FullyCharged: return UPowerDeviceState::FullyCharged;
	case PowerSourceStatus::PendingCharge: return UPowerDeviceState::PendingCharge;
	}
	return UPowerDeviceState::Unknown;
}

QString batteryIconName(qreal percentage, UPowerDeviceState::Enum state) {
	if (state == UPowerDeviceState::FullyCharged) {
		return QStringLiteral("battery-level-100-charged-symbolic");
	}

	auto roundedLevel = static_cast<int>(std::lround(percentage * 10.0)) * 10;
	auto chargingSuffix =
	    state == UPowerDeviceState::Charging ? QStringLiteral("-charging") : QString();
	return QStringLiteral("battery-level-%1%2-symbolic").arg(roundedLevel).arg(chargingSuffix);
}

QString iconNameFor(const PowerSourceReading& reading, UPowerDeviceState::Enum state) {
	switch (reading.kind) {
	case PowerSourceKind::PowerAdapter: return QStringLiteral("ac-adapter-symbolic");
	case PowerSourceKind::Mouse: return QStringLiteral("input-mouse-symbolic");
	case PowerSourceKind::Keyboard: return QStringLiteral("input-keyboard-symbolic");
	case PowerSourceKind::Touchpad: return QStringLiteral("input-touchpad-symbolic");
	case PowerSourceKind::Headphones: return QStringLiteral("audio-headphones-symbolic");
	default: return batteryIconName(reading.percentage, state);
	}
}

}

QString UPowerDeviceState::toString(UPowerDeviceState::Enum state) {
	switch (state) {
	case UPowerDeviceState::Unknown: return QStringLiteral("Unknown");
	case UPowerDeviceState::Charging: return QStringLiteral("Charging");
	case UPowerDeviceState::Discharging: return QStringLiteral("Discharging");
	case UPowerDeviceState::Empty: return QStringLiteral("Empty");
	case UPowerDeviceState::FullyCharged: return QStringLiteral("Fully Charged");
	case UPowerDeviceState::PendingCharge: return QStringLiteral("Pending Charge");
	case UPowerDeviceState::PendingDischarge: return QStringLiteral("Pending Discharge");
	default: return QStringLiteral("Invalid Status");
	}
}

QString UPowerDeviceType::toString(UPowerDeviceType::Enum type) {
	switch (type) {
	case UPowerDeviceType::Unknown: return QStringLiteral("Unknown");
	case UPowerDeviceType::LinePower: return QStringLiteral("Line Power");
	case UPowerDeviceType::Battery: return QStringLiteral("Battery");
	case UPowerDeviceType::Ups: return QStringLiteral("Ups");
	case UPowerDeviceType::Monitor: return QStringLiteral("Monitor");
	case UPowerDeviceType::Mouse: return QStringLiteral("Mouse");
	case UPowerDeviceType::Keyboard: return QStringLiteral("Keyboard");
	case UPowerDeviceType::Pda: return QStringLiteral("Pda");
	case UPowerDeviceType::Phone: return QStringLiteral("Phone");
	case UPowerDeviceType::MediaPlayer: return QStringLiteral("Media Player");
	case UPowerDeviceType::Tablet: return QStringLiteral("Tablet");
	case UPowerDeviceType::Computer: return QStringLiteral("Computer");
	case UPowerDeviceType::GamingInput: return QStringLiteral("Gaming Input");
	case UPowerDeviceType::Pen: return QStringLiteral("Pen");
	case UPowerDeviceType::Touchpad: return QStringLiteral("Touchpad");
	case UPowerDeviceType::Modem: return QStringLiteral("Modem");
	case UPowerDeviceType::Network: return QStringLiteral("Network");
	case UPowerDeviceType::Headset: return QStringLiteral("Headset");
	case UPowerDeviceType::Speakers: return QStringLiteral("Speakers");
	case UPowerDeviceType::Headphones: return QStringLiteral("Headphones");
	case UPowerDeviceType::Video: return QStringLiteral("Video");
	case UPowerDeviceType::OtherAudio: return QStringLiteral("Other Audio");
	case UPowerDeviceType::RemoteControl: return QStringLiteral("Remote Control");
	case UPowerDeviceType::Printer: return QStringLiteral("Printer");
	case UPowerDeviceType::Scanner: return QStringLiteral("Scanner");
	case UPowerDeviceType::Camera: return QStringLiteral("Camera");
	case UPowerDeviceType::Wearable: return QStringLiteral("Wearable");
	case UPowerDeviceType::Toy: return QStringLiteral("Toy");
	case UPowerDeviceType::BluetoothGeneric: return QStringLiteral("Bluetooth Generic");
	default: return QStringLiteral("Invalid Type");
	}
}

UPowerDevice::UPowerDevice(QObject* parent): QObject(parent) {
	this->bIsLaptopBattery.setBinding([this]() {
		return this->bType == UPowerDeviceType::Battery && this->bPowerSupply;
	});

	this->bHealthSupported.setBinding([this]() { return this->bHealthPercentage.value() != 0; });
}

void UPowerDevice::applyReading(const PowerSourceReading& reading) {
	auto state = deviceStateFor(reading.status);
	auto isSystemSupply = reading.kind == PowerSourceKind::InternalBattery
	                   || reading.kind == PowerSourceKind::UninterruptiblePowerSupply
	                   || reading.kind == PowerSourceKind::PowerAdapter;

	Qt::beginPropertyUpdateGroup();
	this->bType = deviceTypeFor(reading.kind);
	this->bPowerSupply = isSystemSupply;
	this->bEnergy = reading.energyWattHours;
	this->bEnergyCapacity = reading.energyCapacityWattHours;
	this->bChangeRate = reading.changeRateWatts;
	this->bTimeToEmpty = static_cast<qreal>(reading.secondsToEmpty);
	this->bTimeToFull = static_cast<qreal>(reading.secondsToFull);
	this->bPercentage = reading.percentage;
	this->bIsPresent = reading.isPresent;
	this->bState = state;
	this->bHealthPercentage = reading.healthPercentage;
	this->bIconName = iconNameFor(reading, state);
	this->bNativePath = reading.identifier;
	this->bModel = reading.model;
	this->bReady = true;
	Qt::endPropertyUpdateGroup();
}

void UPowerDevice::markAbsent() {
	Qt::beginPropertyUpdateGroup();
	this->bType = UPowerDeviceType::Unknown;
	this->bPowerSupply = false;
	this->bIsPresent = false;
	this->bState = UPowerDeviceState::Unknown;
	this->bIconName = QString();
	this->bReady = true;
	Qt::endPropertyUpdateGroup();
}

UPower* UPower::instance() {
	static auto* sharedInstance = new UPower();
	return sharedInstance;
}

UPower::UPower() {
	this->mChangeListener = std::make_unique<PowerSourceChangeListener>([this]() { this->refresh(); });

	QObject::connect(&this->mPeriodicRefresh, &QTimer::timeout, this, &UPower::refresh);
	this->mPeriodicRefresh.start(PERIODIC_REFRESH_INTERVAL_MILLISECONDS);

	this->refresh();
}

void UPower::refresh() {
	auto readings = readPowerSources();

	const PowerSourceReading* internalBattery = nullptr;
	QSet<QString> seenIdentifiers;

	for (const auto& reading: readings) {
		if (reading.kind == PowerSourceKind::InternalBattery && !internalBattery) {
			internalBattery = &reading;
		}

		seenIdentifiers.insert(reading.identifier);

		auto* device = this->mDevicesByIdentifier.value(reading.identifier);
		if (!device) {
			device = new UPowerDevice(this);
			device->applyReading(reading);
			this->mDevicesByIdentifier.insert(reading.identifier, device);
			this->mDeviceModel.insertObject(device);
		} else {
			device->applyReading(reading);
		}
	}

	for (auto iterator = this->mDevicesByIdentifier.begin();
	     iterator != this->mDevicesByIdentifier.end();)
	{
		if (seenIdentifiers.contains(iterator.key())) {
			++iterator;
			continue;
		}

		this->mDeviceModel.removeObject(iterator.value());
		iterator.value()->deleteLater();
		iterator = this->mDevicesByIdentifier.erase(iterator);
	}

	if (internalBattery) this->mDisplayDevice.applyReading(*internalBattery);
	else this->mDisplayDevice.markAbsent();

	this->bOnBattery = isRunningOnBattery();
}

UPowerQml::UPowerQml(QObject* parent): QObject(parent) {
	QObject::connect(UPower::instance(), &UPower::onBatteryChanged, this, &UPowerQml::onBatteryChanged);
}

}

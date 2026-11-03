#include "power_sources.hpp"
#include <algorithm>
#include <optional>
#include <utility>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/pwr_mgt/IOPM.h>

namespace qs::service::upower {

namespace {

template <typename CoreFoundationType>
class CoreFoundationReference {
public:
	explicit CoreFoundationReference(CoreFoundationType value = nullptr): value(value) {}
	~CoreFoundationReference() {
		if (this->value) CFRelease(this->value);
	}
	CoreFoundationReference(const CoreFoundationReference&) = delete;
	CoreFoundationReference& operator=(const CoreFoundationReference&) = delete;
	CoreFoundationReference(CoreFoundationReference&&) = delete;
	CoreFoundationReference& operator=(CoreFoundationReference&&) = delete;

	[[nodiscard]] CoreFoundationType get() const { return this->value; }
	explicit operator bool() const { return this->value != nullptr; }

private:
	CoreFoundationType value;
};

class IOObjectReference {
public:
	explicit IOObjectReference(io_object_t object = IO_OBJECT_NULL): object(object) {}
	~IOObjectReference() {
		if (this->object != IO_OBJECT_NULL) IOObjectRelease(this->object);
	}
	IOObjectReference(const IOObjectReference&) = delete;
	IOObjectReference& operator=(const IOObjectReference&) = delete;
	IOObjectReference(IOObjectReference&&) = delete;
	IOObjectReference& operator=(IOObjectReference&&) = delete;

	[[nodiscard]] io_object_t get() const { return this->object; }

private:
	io_object_t object;
};

CFTypeRef dictionaryValue(CFDictionaryRef dictionary, CFStringRef key) {
	return dictionary ? CFDictionaryGetValue(dictionary, key) : nullptr;
}

std::optional<qint64> integerValue(CFDictionaryRef dictionary, CFStringRef key) {
	auto value = dictionaryValue(dictionary, key);
	if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) return std::nullopt;

	qint64 result = 0;
	CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &result);
	return result;
}

bool booleanValue(CFDictionaryRef dictionary, CFStringRef key) {
	auto value = dictionaryValue(dictionary, key);
	return value && CFGetTypeID(value) == CFBooleanGetTypeID()
	    && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
}

QString stringValue(CFDictionaryRef dictionary, CFStringRef key) {
	auto value = dictionaryValue(dictionary, key);
	if (!value || CFGetTypeID(value) != CFStringGetTypeID()) return {};
	return QString::fromCFString(static_cast<CFStringRef>(value));
}

bool stringEquals(CFDictionaryRef dictionary, CFStringRef key, CFStringRef expected) {
	auto value = dictionaryValue(dictionary, key);
	return value && CFGetTypeID(value) == CFStringGetTypeID()
	    && CFStringCompare(static_cast<CFStringRef>(value), expected, 0) == kCFCompareEqualTo;
}

qint64 minutesToSeconds(std::optional<qint64> minutes) {
	return minutes && *minutes > 0 ? *minutes * 60 : 0;
}

void applySmartBatteryDetails(PowerSourceReading& reading) {
	IOObjectReference service(
	    IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSmartBattery"))
	);
	if (service.get() == IO_OBJECT_NULL) return;

	CFMutableDictionaryRef rawProperties = nullptr;
	if (IORegistryEntryCreateCFProperties(service.get(), &rawProperties, kCFAllocatorDefault, 0)
	    != KERN_SUCCESS)
	{
		return;
	}
	CoreFoundationReference<CFMutableDictionaryRef> properties(rawProperties);

	auto batteryDataValue = dictionaryValue(properties.get(), CFSTR("BatteryData"));
	auto batteryData = batteryDataValue && CFGetTypeID(batteryDataValue) == CFDictionaryGetTypeID()
	                     ? static_cast<CFDictionaryRef>(batteryDataValue)
	                     : nullptr;

	auto firstAvailable = [&](CFStringRef batteryDataKey, CFStringRef legacyKey) {
		auto value = integerValue(batteryData, batteryDataKey);
		return value && *value > 0 ? value : integerValue(properties.get(), legacyKey);
	};

	auto remainingMilliampHours = firstAvailable(CFSTR("RemainingCapacity"), CFSTR("AppleRawCurrentCapacity"));
	auto fullChargeMilliampHours = firstAvailable(CFSTR("FullChargeCapacity"), CFSTR("AppleRawMaxCapacity"));
	auto nominalMilliampHours = firstAvailable(CFSTR("NominalChargeCapacity"), CFSTR("NominalChargeCapacity"));
	auto designMilliampHours = firstAvailable(CFSTR("DesignCapacity"), CFSTR("DesignCapacity"));
	auto voltageMillivolts = integerValue(properties.get(), CFSTR("Voltage"));
	auto amperageMilliamps = integerValue(properties.get(), CFSTR("InstantAmperage"));
	if (!amperageMilliamps) amperageMilliamps = integerValue(properties.get(), CFSTR("Amperage"));

	auto model = stringValue(properties.get(), CFSTR("DeviceName"));
	if (!model.isEmpty()) reading.model = model;

	if (voltageMillivolts && *voltageMillivolts > 0) {
		auto volts = static_cast<qreal>(*voltageMillivolts) / 1000.0;
		auto toWattHours = [volts](qint64 milliampHours) {
			return static_cast<qreal>(milliampHours) / 1000.0 * volts;
		};

		if (remainingMilliampHours) reading.energyWattHours = toWattHours(*remainingMilliampHours);
		if (fullChargeMilliampHours) {
			reading.energyCapacityWattHours = toWattHours(*fullChargeMilliampHours);
		}
		if (amperageMilliamps) {
			reading.changeRateWatts = static_cast<qreal>(*amperageMilliamps) / 1000.0 * volts;
		}
	}

	auto healthReferenceMilliampHours = nominalMilliampHours ? nominalMilliampHours : fullChargeMilliampHours;
	if (healthReferenceMilliampHours && designMilliampHours && *designMilliampHours > 0) {
		reading.healthPercentage = std::min(
		    100.0,
		    static_cast<qreal>(*healthReferenceMilliampHours) * 100.0
		        / static_cast<qreal>(*designMilliampHours)
		);
	}
}

PowerSourceReading readSystemPowerSource(CFDictionaryRef description) {
	PowerSourceReading reading;

	reading.identifier = stringValue(description, CFSTR(kIOPSNameKey));
	reading.model = reading.identifier;
	reading.kind = stringEquals(description, CFSTR(kIOPSTypeKey), CFSTR(kIOPSUPSType))
	                 ? PowerSourceKind::UninterruptiblePowerSupply
	                 : PowerSourceKind::InternalBattery;
	reading.isPresent = booleanValue(description, CFSTR(kIOPSIsPresentKey));

	auto currentCapacity = integerValue(description, CFSTR(kIOPSCurrentCapacityKey)).value_or(0);
	auto maximumCapacity = integerValue(description, CFSTR(kIOPSMaxCapacityKey)).value_or(0);
	reading.percentage = maximumCapacity > 0
	                       ? std::clamp(static_cast<qreal>(currentCapacity) / maximumCapacity, 0.0, 1.0)
	                       : 0.0;

	auto isCharging = booleanValue(description, CFSTR(kIOPSIsChargingKey));
	auto isCharged = booleanValue(description, CFSTR(kIOPSIsChargedKey));
	auto onExternalPower =
	    stringEquals(description, CFSTR(kIOPSPowerSourceStateKey), CFSTR(kIOPSACPowerValue));

	if (isCharging) reading.status = PowerSourceStatus::Charging;
	else if (isCharged || (onExternalPower && reading.percentage >= 0.995)) {
		reading.status = PowerSourceStatus::FullyCharged;
	} else if (onExternalPower) reading.status = PowerSourceStatus::PendingCharge;
	else if (reading.percentage <= 0) reading.status = PowerSourceStatus::Empty;
	else reading.status = PowerSourceStatus::Discharging;

	reading.secondsToEmpty =
	    isCharging || onExternalPower
	        ? 0
	        : minutesToSeconds(integerValue(description, CFSTR(kIOPSTimeToEmptyKey)));
	reading.secondsToFull =
	    isCharging ? minutesToSeconds(integerValue(description, CFSTR(kIOPSTimeToFullChargeKey))) : 0;

	if (reading.kind == PowerSourceKind::InternalBattery) applySmartBatteryDetails(reading);

	return reading;
}

std::optional<PowerSourceReading> readPowerAdapter() {
	CoreFoundationReference<CFDictionaryRef> adapter(IOPSCopyExternalPowerAdapterDetails());
	if (!adapter) return std::nullopt;

	PowerSourceReading reading;
	reading.identifier = QStringLiteral("PowerAdapter");
	reading.kind = PowerSourceKind::PowerAdapter;
	reading.status = PowerSourceStatus::Unknown;

	auto name = stringValue(adapter.get(), CFSTR("Name"));
	auto watts = integerValue(adapter.get(), CFSTR(kIOPSPowerAdapterWattsKey));
	reading.model = !name.isEmpty() ? name
	              : watts       ? QStringLiteral("%1W Power Adapter").arg(*watts)
	                            : QStringLiteral("Power Adapter");
	return reading;
}

PowerSourceKind peripheralKind(const QString& productName) {
	auto lowercaseName = productName.toLower();
	if (lowercaseName.contains(QStringLiteral("mouse"))) return PowerSourceKind::Mouse;
	if (lowercaseName.contains(QStringLiteral("keyboard"))) return PowerSourceKind::Keyboard;
	if (lowercaseName.contains(QStringLiteral("trackpad"))) return PowerSourceKind::Touchpad;
	if (lowercaseName.contains(QStringLiteral("airpods"))
	    || lowercaseName.contains(QStringLiteral("beats"))
	    || lowercaseName.contains(QStringLiteral("headphone")))
	{
		return PowerSourceKind::Headphones;
	}
	return PowerSourceKind::OtherPeripheral;
}

QList<PowerSourceReading> readPeripheralBatteries() {
	QList<PowerSourceReading> readings;

	io_iterator_t rawIterator = IO_OBJECT_NULL;
	if (IOServiceGetMatchingServices(
	        kIOMainPortDefault,
	        IOServiceMatching("AppleDeviceManagementHIDEventService"),
	        &rawIterator
	    )
	    != KERN_SUCCESS)
	{
		return readings;
	}
	IOObjectReference iterator(rawIterator);

	while (auto rawService = IOIteratorNext(iterator.get())) {
		IOObjectReference service(rawService);

		CFMutableDictionaryRef rawProperties = nullptr;
		if (IORegistryEntryCreateCFProperties(service.get(), &rawProperties, kCFAllocatorDefault, 0)
		    != KERN_SUCCESS)
		{
			continue;
		}
		CoreFoundationReference<CFMutableDictionaryRef> properties(rawProperties);

		auto batteryPercent = integerValue(properties.get(), CFSTR("BatteryPercent"));
		if (!batteryPercent) continue;

		PowerSourceReading reading;
		reading.model = stringValue(properties.get(), CFSTR("Product"));
		reading.identifier = stringValue(properties.get(), CFSTR("DeviceAddress"));
		if (reading.identifier.isEmpty()) reading.identifier = reading.model;
		reading.kind = peripheralKind(reading.model);
		reading.percentage = std::clamp(static_cast<qreal>(*batteryPercent) / 100.0, 0.0, 1.0);

		auto statusFlags = integerValue(properties.get(), CFSTR("BatteryStatusFlags")).value_or(0);
		auto isCharging = (statusFlags & 0x2) != 0;
		reading.status = isCharging                  ? PowerSourceStatus::Charging
		               : reading.percentage >= 0.995 ? PowerSourceStatus::FullyCharged
		                                             : PowerSourceStatus::Discharging;

		readings.append(reading);
	}

	return readings;
}

}

QList<PowerSourceReading> readPowerSources() {
	QList<PowerSourceReading> readings;

	CoreFoundationReference<CFTypeRef> snapshot(IOPSCopyPowerSourcesInfo());
	if (snapshot) {
		CoreFoundationReference<CFArrayRef> sources(IOPSCopyPowerSourcesList(snapshot.get()));
		auto sourceCount = sources ? CFArrayGetCount(sources.get()) : 0;

		for (CFIndex sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex) {
			auto description = IOPSGetPowerSourceDescription(
			    snapshot.get(),
			    CFArrayGetValueAtIndex(sources.get(), sourceIndex)
			);
			if (description) readings.append(readSystemPowerSource(description));
		}
	}

	if (auto adapter = readPowerAdapter()) readings.append(*adapter);
	readings.append(readPeripheralBatteries());

	return readings;
}

bool isRunningOnBattery() {
	CoreFoundationReference<CFTypeRef> snapshot(IOPSCopyPowerSourcesInfo());
	if (!snapshot) return false;

	auto providingSource = IOPSGetProvidingPowerSourceType(snapshot.get());
	return providingSource
	    && CFStringCompare(providingSource, CFSTR(kIOPMBatteryPowerKey), 0) == kCFCompareEqualTo;
}

PowerSourceChangeListener::PowerSourceChangeListener(std::function<void()> onChange)
    : onChange(std::move(onChange)) {
	auto* source = IOPSNotificationCreateRunLoopSource(
	    &PowerSourceChangeListener::handleNotification,
	    this
	);
	if (!source) return;

	CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
	this->runLoopSource = source;
}

PowerSourceChangeListener::~PowerSourceChangeListener() {
	if (!this->runLoopSource) return;

	auto* source = static_cast<CFRunLoopSourceRef>(this->runLoopSource);
	CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
	CFRelease(source);
}

void PowerSourceChangeListener::handleNotification(void* context) {
	auto* listener = static_cast<PowerSourceChangeListener*>(context);
	if (listener->onChange) listener->onChange();
}

}

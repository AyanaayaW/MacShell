#include "bluetooth.hpp"
#include <memory>
#include <optional>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qset.h>
#include <qstring.h>
#include <qtypes.h>

#include "bluetooth_backend.hpp"

namespace qs::bluetooth {

namespace {

Q_LOGGING_CATEGORY(logBluetooth, "quickshell.bluetooth.macos", QtWarningMsg);

constexpr auto REFRESH_INTERVAL_MILLISECONDS = 2000;

}

QString BluetoothAdapterState::toString(BluetoothAdapterState::Enum state) {
	switch (state) {
	case BluetoothAdapterState::Disabled: return QStringLiteral("Disabled");
	case BluetoothAdapterState::Enabled: return QStringLiteral("Enabled");
	case BluetoothAdapterState::Enabling: return QStringLiteral("Enabling");
	case BluetoothAdapterState::Disabling: return QStringLiteral("Disabling");
	case BluetoothAdapterState::Blocked: return QStringLiteral("Blocked");
	default: return QStringLiteral("Unknown");
	}
}

QString BluetoothDeviceState::toString(BluetoothDeviceState::Enum state) {
	switch (state) {
	case BluetoothDeviceState::Disconnected: return QStringLiteral("Disconnected");
	case BluetoothDeviceState::Connected: return QStringLiteral("Connected");
	case BluetoothDeviceState::Disconnecting: return QStringLiteral("Disconnecting");
	case BluetoothDeviceState::Connecting: return QStringLiteral("Connecting");
	default: return QStringLiteral("Unknown");
	}
}

BluetoothDevice::BluetoothDevice(const QString& address, BluetoothAdapter* adapter)
    : QObject(adapter)
    , mAdapter(adapter) {
	this->bAddress = address;
	QObject::connect(this, &BluetoothDevice::pairedChanged, this, &BluetoothDevice::bondedChanged);
	QObject::connect(this, &BluetoothDevice::pairedChanged, this, &BluetoothDevice::trustedChanged);
	QObject::connect(this, &BluetoothDevice::stateChanged, this, &BluetoothDevice::connectedChanged);
	QObject::connect(this, &BluetoothDevice::deviceNameChanged, this, &BluetoothDevice::nameChanged);
}

void BluetoothDevice::applyInfo(const macos::DeviceInfo& info) {
	auto isTransitioning = this->bState == BluetoothDeviceState::Connecting
	                    || this->bState == BluetoothDeviceState::Disconnecting;

	Qt::beginPropertyUpdateGroup();
	this->bDeviceName = info.name;
	this->bIcon = info.iconName;
	this->bPaired = info.isPaired;
	if (!isTransitioning) {
		this->bState = info.isConnected ? BluetoothDeviceState::Connected : BluetoothDeviceState::Disconnected;
	}
	if (info.batteryFraction) this->bBattery = *info.batteryFraction;
	Qt::endPropertyUpdateGroup();

	auto hasBattery = info.batteryFraction.has_value();
	if (hasBattery != this->mBatteryAvailable) {
		this->mBatteryAvailable = hasBattery;
		emit this->batteryAvailableChanged();
	}
}

QString BluetoothDevice::name() const {
	return this->mCustomName.isEmpty() ? this->bDeviceName.value() : this->mCustomName;
}

void BluetoothDevice::setName(const QString& name) {
	if (name == this->mCustomName) return;
	this->mCustomName = name;
	emit this->nameChanged();
}

void BluetoothDevice::setConnected(bool connected) {
	if (connected) this->connect();
	else this->disconnect();
}

void BluetoothDevice::setTrusted(bool trusted) {
	if (this->mTrusted == trusted) return;
	this->mTrusted = trusted;
	emit this->trustedChanged();
}

void BluetoothDevice::setBlocked(bool blocked) {
	if (blocked == this->mBlocked) return;
	this->mBlocked = blocked;
	if (blocked) this->disconnect();
	emit this->blockedChanged();
}

bool BluetoothDevice::wakeAllowed() { return macos::readController().isRemoteWakeEnabled; }

void BluetoothDevice::setWakeAllowed(bool wakeAllowed) {
	macos::setRemoteWakeEnabled(wakeAllowed);
	emit this->wakeAllowedChanged();
}

QString BluetoothDevice::path() const {
	auto pathAddress = this->bAddress.value();
	return BluetoothAdapter::path() + QStringLiteral("/dev_") + pathAddress.replace(u':', u'_');
}

void BluetoothDevice::refreshFromSystem() { this->mAdapter->refreshDevice(this->bAddress.value()); }

void BluetoothDevice::onConnectionAttemptFinished() {
	this->bState = BluetoothDeviceState::Disconnected;
	this->refreshFromSystem();
}

void BluetoothDevice::connect() {
	if (this->mBlocked || this->bState == BluetoothDeviceState::Connected) return;

	this->bState = BluetoothDeviceState::Connecting;
	macos::connectDevice(this->bAddress.value(), [device = QPointer<BluetoothDevice>(this)](bool succeeded) {
		if (!device) return;
		if (!succeeded) qCWarning(logBluetooth) << "Could not connect to" << device->name();
		device->onConnectionAttemptFinished();
	});
}

void BluetoothDevice::disconnect() {
	if (this->bState == BluetoothDeviceState::Disconnected) return;

	this->bState = BluetoothDeviceState::Disconnecting;
	macos::disconnectDevice(this->bAddress.value(), [device = QPointer<BluetoothDevice>(this)](bool succeeded) {
		if (!device) return;
		if (!succeeded) qCWarning(logBluetooth) << "Could not disconnect from" << device->name();
		device->onConnectionAttemptFinished();
	});
}

void BluetoothDevice::pair() {
	if (this->mPairing || this->bPaired) return;

	this->mPairing = std::make_unique<macos::DevicePairing>(
	    this->bAddress.value(),
	    [device = QPointer<BluetoothDevice>(this)](bool succeeded) {
		    if (!device) return;
		    if (!succeeded) qCWarning(logBluetooth) << "Pairing failed for" << device->name();
		    device->mPairing.reset();
		    emit device->pairingChanged();
		    device->refreshFromSystem();
	    }
	);

	if (!this->mPairing->hasStarted()) {
		qCWarning(logBluetooth) << "Could not start pairing with" << this->name();
		this->mPairing.reset();
		return;
	}

	emit this->pairingChanged();
}

void BluetoothDevice::cancelPair() {
	if (!this->mPairing) return;
	this->mPairing.reset();
	emit this->pairingChanged();
}

void BluetoothDevice::forget() {
	if (!macos::forgetDevice(this->bAddress.value())) {
		qCWarning(logBluetooth) << "Could not forget" << this->name();
		return;
	}
	this->refreshFromSystem();
}

BluetoothAdapter::BluetoothAdapter(QObject* parent)
    : QObject(parent)
    , mDiscovery([this](const macos::DeviceInfo& info) { this->addDiscoveredDevice(info); }) {
	QObject::connect(this, &BluetoothAdapter::stateChanged, this, &BluetoothAdapter::enabledChanged);
	this->refresh();
}

BluetoothDevice* BluetoothAdapter::deviceForAddress(const QString& address, bool createIfMissing) {
	if (auto* device = this->mDevicesByAddress.value(address)) return device;
	if (!createIfMissing) return nullptr;

	auto* device = new BluetoothDevice(address, this);
	this->mDevicesByAddress.insert(address, device);
	this->mDevices.insertObject(device);
	emit this->deviceAdded(device);
	return device;
}

void BluetoothAdapter::removeDevice(BluetoothDevice* device) {
	this->mDevicesByAddress.remove(device->bindableAddress().value());
	this->mDevices.removeObject(device);
	emit this->deviceRemoved(device);
	device->deleteLater();
}

void BluetoothAdapter::addDiscoveredDevice(const macos::DeviceInfo& info) {
	if (info.address.isEmpty()) return;
	this->deviceForAddress(info.address, true)->applyInfo(info);
}

void BluetoothAdapter::refresh() {
	auto controller = macos::readController();

	auto systemState = !controller.isAvailable ? BluetoothAdapterState::Blocked
	                 : controller.isPoweredOn  ? BluetoothAdapterState::Enabled
	                                           : BluetoothAdapterState::Disabled;

	if (this->mRequestedPower && *this->mRequestedPower == controller.isPoweredOn) {
		this->mRequestedPower.reset();
	}

	Qt::beginPropertyUpdateGroup();
	this->bName = controller.name;
	if (this->mRequestedPower) {
		this->bState = *this->mRequestedPower ? BluetoothAdapterState::Enabling
		                                      : BluetoothAdapterState::Disabling;
	} else {
		this->bState = systemState;
	}
	Qt::endPropertyUpdateGroup();

	if (controller.isDiscoverable != this->mDiscoverable) {
		this->mDiscoverable = controller.isDiscoverable;
		emit this->discoverableChanged();
	}

	QSet<QString> pairedAddresses;
	for (const auto& info: macos::readPairedDevices()) {
		pairedAddresses.insert(info.address);
		this->deviceForAddress(info.address, true)->applyInfo(info);
	}

	auto keepUnpaired = this->mDiscovery.isRunning();
	for (auto* device: QList(this->mDevices.valueList())) {
		auto address = device->bindableAddress().value();
		if (pairedAddresses.contains(address)) continue;
		if (keepUnpaired && !device->bindablePaired().value()) continue;
		this->removeDevice(device);
	}
}

void BluetoothAdapter::refreshDevice(const QString& address) {
	auto* device = this->deviceForAddress(address, false);
	if (!device) return;

	auto info = macos::readDevice(address);
	if (!info || (!info->isPaired && !this->mDiscovery.isRunning())) {
		this->removeDevice(device);
		return;
	}

	device->applyInfo(*info);
}

void BluetoothAdapter::setEnabled(bool enabled) {
	if (enabled == this->enabled() && !this->mRequestedPower) return;

	this->mRequestedPower = enabled;
	macos::setPoweredOn(enabled);
	this->refresh();
}

void BluetoothAdapter::setDiscoverable(bool discoverable) {
	macos::setDiscoverable(discoverable);
	this->refresh();
}

void BluetoothAdapter::setDiscoverableTimeout(quint32 timeout) {
	if (timeout == this->mDiscoverableTimeout) return;
	this->mDiscoverableTimeout = timeout;
	emit this->discoverableTimeoutChanged();
}

void BluetoothAdapter::setDiscovering(bool discovering) {
	if (discovering == this->discovering()) return;

	if (discovering) this->mDiscovery.start();
	else this->mDiscovery.stop();

	emit this->discoveringChanged();
	if (!discovering) this->refresh();
}

void BluetoothAdapter::setPairable(bool pairable) {
	if (pairable == this->mPairable) return;
	this->mPairable = pairable;
	emit this->pairableChanged();
}

void BluetoothAdapter::setPairableTimeout(quint32 timeout) {
	if (timeout == this->mPairableTimeout) return;
	this->mPairableTimeout = timeout;
	emit this->pairableTimeoutChanged();
}

BluetoothManager* BluetoothManager::instance() {
	static auto* sharedInstance = new BluetoothManager();
	return sharedInstance;
}

BluetoothManager::BluetoothManager() {
	this->mAdapter = new BluetoothAdapter(this);

	for (auto* device: this->mAdapter->devices()->valueList()) this->mDevices.insertObject(device);
	QObject::connect(this->mAdapter, &BluetoothAdapter::deviceAdded, this, [this](BluetoothDevice* device) {
		this->mDevices.insertObject(device);
	});
	QObject::connect(this->mAdapter, &BluetoothAdapter::deviceRemoved, this, [this](BluetoothDevice* device) {
		this->mDevices.removeObject(device);
	});

	if (macos::readController().isAvailable) {
		this->mAdapters.insertObject(this->mAdapter);
		this->bDefaultAdapter = this->mAdapter;
	}

	this->mConnectionObserver = std::make_unique<macos::ConnectionObserver>([this]() { this->refresh(); });

	QObject::connect(&this->mRefreshTimer, &QTimer::timeout, this, &BluetoothManager::refresh);
	this->mRefreshTimer.start(REFRESH_INTERVAL_MILLISECONDS);
}

void BluetoothManager::refresh() { this->mAdapter->refresh(); }

BluezQml::BluezQml(QObject* parent): QObject(parent) {
	QObject::connect(
	    BluetoothManager::instance(),
	    &BluetoothManager::defaultAdapterChanged,
	    this,
	    &BluezQml::defaultAdapterChanged
	);
}

}

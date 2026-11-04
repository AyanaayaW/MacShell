#pragma once

#include <functional>
#include <optional>

#include <qlist.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::bluetooth::macos {

struct ControllerInfo {
	bool isAvailable = false;
	QString name;
	QString address;
	bool isPoweredOn = false;
	bool isDiscoverable = false;
	bool isRemoteWakeEnabled = false;
};

struct DeviceInfo {
	QString address;
	QString name;
	QString iconName;
	bool isConnected = false;
	bool isPaired = false;
	std::optional<qreal> batteryFraction;
};

void requestBluetoothAccess(std::function<void(bool granted)> onDecided);

ControllerInfo readController();
void setPoweredOn(bool poweredOn);
void setDiscoverable(bool discoverable);
void setRemoteWakeEnabled(bool enabled);

QList<DeviceInfo> readPairedDevices();
std::optional<DeviceInfo> readDevice(const QString& address);

void connectDevice(const QString& address, std::function<void(bool succeeded)> onFinished);
void disconnectDevice(const QString& address, std::function<void(bool succeeded)> onFinished);
bool forgetDevice(const QString& address);

class DeviceDiscovery {
public:
	explicit DeviceDiscovery(std::function<void(const DeviceInfo&)> onDeviceFound);
	~DeviceDiscovery();
	DeviceDiscovery(const DeviceDiscovery&) = delete;
	DeviceDiscovery& operator=(const DeviceDiscovery&) = delete;
	DeviceDiscovery(DeviceDiscovery&&) = delete;
	DeviceDiscovery& operator=(DeviceDiscovery&&) = delete;

	void start();
	void stop();
	[[nodiscard]] bool isRunning() const { return this->running; }

private:
	void* inquiryDelegate = nullptr;
	bool running = false;
};

class DevicePairing {
public:
	DevicePairing(const QString& address, std::function<void(bool succeeded)> onFinished);
	~DevicePairing();
	DevicePairing(const DevicePairing&) = delete;
	DevicePairing& operator=(const DevicePairing&) = delete;
	DevicePairing(DevicePairing&&) = delete;
	DevicePairing& operator=(DevicePairing&&) = delete;

	[[nodiscard]] bool hasStarted() const { return this->pairingDelegate != nullptr; }
	void cancel();

private:
	void* pairingDelegate = nullptr;
};

class ConnectionObserver {
public:
	explicit ConnectionObserver(std::function<void()> onConnectionChanged);
	~ConnectionObserver();
	ConnectionObserver(const ConnectionObserver&) = delete;
	ConnectionObserver& operator=(const ConnectionObserver&) = delete;
	ConnectionObserver(ConnectionObserver&&) = delete;
	ConnectionObserver& operator=(ConnectionObserver&&) = delete;

private:
	void* observerDelegate = nullptr;
};

}

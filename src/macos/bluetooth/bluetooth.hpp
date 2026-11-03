#pragma once

#include <memory>

#include <qhash.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/model.hpp"
#include "bluetooth_backend.hpp"

namespace qs::bluetooth {

class BluetoothAdapterState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Disabled = 0,
		Enabled = 1,
		Enabling = 2,
		Disabling = 3,
		Blocked = 4,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::bluetooth::BluetoothAdapterState::Enum state);
};

class BluetoothDeviceState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Disconnected = 0,
		Connected = 1,
		Disconnecting = 2,
		Connecting = 3,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::bluetooth::BluetoothDeviceState::Enum state);
};

class BluetoothAdapter;

class BluetoothDevice: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString address READ default NOTIFY addressChanged BINDABLE bindableAddress);
	Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged);
	Q_PROPERTY(QString deviceName READ default NOTIFY deviceNameChanged BINDABLE bindableDeviceName);
	Q_PROPERTY(QString icon READ default NOTIFY iconChanged BINDABLE bindableIcon);
	Q_PROPERTY(qs::bluetooth::BluetoothDeviceState::Enum state READ default NOTIFY stateChanged BINDABLE bindableState);
	Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY connectedChanged);
	Q_PROPERTY(bool paired READ default NOTIFY pairedChanged BINDABLE bindablePaired);
	Q_PROPERTY(bool bonded READ default NOTIFY bondedChanged BINDABLE bindableBonded);
	Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged);
	Q_PROPERTY(bool trusted READ trusted WRITE setTrusted NOTIFY trustedChanged);
	Q_PROPERTY(bool blocked READ blocked WRITE setBlocked NOTIFY blockedChanged);
	Q_PROPERTY(bool wakeAllowed READ wakeAllowed WRITE setWakeAllowed NOTIFY wakeAllowedChanged);
	Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY batteryAvailableChanged);
	Q_PROPERTY(qreal battery READ default NOTIFY batteryChanged BINDABLE bindableBattery);
	Q_PROPERTY(qs::bluetooth::BluetoothAdapter* adapter READ adapter NOTIFY adapterChanged);
	Q_PROPERTY(QString dbusPath READ path CONSTANT);
	QML_ELEMENT;
	QML_UNCREATABLE("");

public:
	BluetoothDevice(const QString& address, BluetoothAdapter* adapter);

	void applyInfo(const macos::DeviceInfo& info);

	[[nodiscard]] QString name() const;
	void setName(const QString& name);

	[[nodiscard]] bool connected() const { return this->bState == BluetoothDeviceState::Connected; }
	void setConnected(bool connected);

	[[nodiscard]] bool pairing() const { return this->mPairing != nullptr; }
	[[nodiscard]] bool trusted() const { return this->mTrusted.value_or(this->bPaired.value()); }
	void setTrusted(bool trusted);
	[[nodiscard]] bool blocked() const { return this->mBlocked; }
	void setBlocked(bool blocked);
	[[nodiscard]] static bool wakeAllowed();
	void setWakeAllowed(bool wakeAllowed);
	[[nodiscard]] bool batteryAvailable() const { return this->mBatteryAvailable; }
	[[nodiscard]] BluetoothAdapter* adapter() const { return this->mAdapter; }
	[[nodiscard]] QString path() const;

	Q_INVOKABLE void connect();
	Q_INVOKABLE void disconnect();
	Q_INVOKABLE void pair();
	Q_INVOKABLE void cancelPair();
	Q_INVOKABLE void forget();

	[[nodiscard]] QBindable<QString> bindableAddress() { return &this->bAddress; }
	[[nodiscard]] QBindable<QString> bindableDeviceName() { return &this->bDeviceName; }
	[[nodiscard]] QBindable<QString> bindableIcon() { return &this->bIcon; }
	[[nodiscard]] QBindable<BluetoothDeviceState::Enum> bindableState() { return &this->bState; }
	[[nodiscard]] QBindable<bool> bindablePaired() { return &this->bPaired; }
	[[nodiscard]] QBindable<bool> bindableBonded() { return &this->bPaired; }
	[[nodiscard]] QBindable<qreal> bindableBattery() { return &this->bBattery; }

signals:
	void addressChanged();
	void deviceNameChanged();
	void nameChanged();
	void connectedChanged();
	void stateChanged();
	void pairedChanged();
	void bondedChanged();
	void pairingChanged();
	void trustedChanged();
	void blockedChanged();
	void wakeAllowedChanged();
	void iconChanged();
	void batteryAvailableChanged();
	void batteryChanged();
	void adapterChanged();

private:
	void refreshFromSystem();
	void onConnectionAttemptFinished();

	BluetoothAdapter* mAdapter;
	QString mCustomName;
	std::optional<bool> mTrusted;
	bool mBlocked = false;
	bool mBatteryAvailable = false;
	std::unique_ptr<macos::DevicePairing> mPairing;

	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bAddress, &BluetoothDevice::addressChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bDeviceName, &BluetoothDevice::deviceNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bIcon, &BluetoothDevice::iconChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, BluetoothDeviceState::Enum, bState, &BluetoothDevice::stateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bPaired, &BluetoothDevice::pairedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, qreal, bBattery, &BluetoothDevice::batteryChanged);
};

class BluetoothAdapter: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString name READ default NOTIFY nameChanged BINDABLE bindableName);
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(qs::bluetooth::BluetoothAdapterState::Enum state READ default NOTIFY stateChanged BINDABLE bindableState);
	Q_PROPERTY(bool discoverable READ discoverable WRITE setDiscoverable NOTIFY discoverableChanged);
	Q_PROPERTY(quint32 discoverableTimeout READ discoverableTimeout WRITE setDiscoverableTimeout NOTIFY discoverableTimeoutChanged);
	Q_PROPERTY(bool discovering READ discovering WRITE setDiscovering NOTIFY discoveringChanged);
	Q_PROPERTY(bool pairable READ pairable WRITE setPairable NOTIFY pairableChanged);
	Q_PROPERTY(quint32 pairableTimeout READ pairableTimeout WRITE setPairableTimeout NOTIFY pairableTimeoutChanged);
	Q_PROPERTY(UntypedObjectModel* devices READ devices CONSTANT);
	Q_PROPERTY(QString adapterId READ adapterId CONSTANT);
	Q_PROPERTY(QString dbusPath READ path CONSTANT);
	QML_ELEMENT;
	QML_UNCREATABLE("");

public:
	explicit BluetoothAdapter(QObject* parent);

	void refresh();
	void refreshDevice(const QString& address);

	[[nodiscard]] bool enabled() const { return this->bState == BluetoothAdapterState::Enabled; }
	void setEnabled(bool enabled);
	[[nodiscard]] bool discoverable() const { return this->mDiscoverable; }
	void setDiscoverable(bool discoverable);
	[[nodiscard]] quint32 discoverableTimeout() const { return this->mDiscoverableTimeout; }
	void setDiscoverableTimeout(quint32 timeout);
	[[nodiscard]] bool discovering() const { return this->mDiscovery.isRunning(); }
	void setDiscovering(bool discovering);
	[[nodiscard]] bool pairable() const { return this->mPairable; }
	void setPairable(bool pairable);
	[[nodiscard]] quint32 pairableTimeout() const { return this->mPairableTimeout; }
	void setPairableTimeout(quint32 timeout);
	[[nodiscard]] ObjectModel<BluetoothDevice>* devices() { return &this->mDevices; }
	[[nodiscard]] static QString adapterId() { return QStringLiteral("hci0"); }
	[[nodiscard]] static QString path() { return QStringLiteral("/org/bluez/hci0"); }

	[[nodiscard]] QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QBindable<BluetoothAdapterState::Enum> bindableState() { return &this->bState; }

signals:
	void nameChanged();
	void enabledChanged();
	void stateChanged();
	void discoverableChanged();
	void discoverableTimeoutChanged();
	void discoveringChanged();
	void pairableChanged();
	void pairableTimeoutChanged();
	void deviceAdded(qs::bluetooth::BluetoothDevice* device);
	void deviceRemoved(qs::bluetooth::BluetoothDevice* device);

private:
	BluetoothDevice* deviceForAddress(const QString& address, bool createIfMissing);
	void addDiscoveredDevice(const macos::DeviceInfo& info);
	void removeDevice(BluetoothDevice* device);

	ObjectModel<BluetoothDevice> mDevices {this};
	QHash<QString, BluetoothDevice*> mDevicesByAddress;
	macos::DeviceDiscovery mDiscovery;
	bool mDiscoverable = false;
	bool mPairable = true;
	quint32 mDiscoverableTimeout = 180;
	quint32 mPairableTimeout = 0;
	std::optional<bool> mRequestedPower;

	Q_OBJECT_BINDABLE_PROPERTY(BluetoothAdapter, QString, bName, &BluetoothAdapter::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothAdapter, BluetoothAdapterState::Enum, bState, &BluetoothAdapter::stateChanged);
};

class BluetoothManager: public QObject {
	Q_OBJECT;

public:
	static BluetoothManager* instance();

	[[nodiscard]] BluetoothAdapter* defaultAdapter() const { return this->mAdapter; }
	[[nodiscard]] ObjectModel<BluetoothAdapter>* adapters() { return &this->mAdapters; }
	[[nodiscard]] ObjectModel<BluetoothDevice>* devices() { return &this->mDevices; }
	[[nodiscard]] QBindable<BluetoothAdapter*> bindableDefaultAdapter() { return &this->bDefaultAdapter; }

signals:
	void defaultAdapterChanged();

private:
	BluetoothManager();

	void refresh();

	BluetoothAdapter* mAdapter = nullptr;
	ObjectModel<BluetoothAdapter> mAdapters {this};
	ObjectModel<BluetoothDevice> mDevices {this};
	QTimer mRefreshTimer;
	std::unique_ptr<macos::ConnectionObserver> mConnectionObserver;

	Q_OBJECT_BINDABLE_PROPERTY(BluetoothManager, BluetoothAdapter*, bDefaultAdapter, &BluetoothManager::defaultAdapterChanged);
};

class BluezQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Bluetooth);
	QML_SINGLETON;
	Q_PROPERTY(qs::bluetooth::BluetoothAdapter* defaultAdapter READ default NOTIFY defaultAdapterChanged BINDABLE bindableDefaultAdapter);
	Q_PROPERTY(UntypedObjectModel* adapters READ adapters CONSTANT);
	Q_PROPERTY(UntypedObjectModel* devices READ devices CONSTANT);

public:
	explicit BluezQml(QObject* parent = nullptr);

	[[nodiscard]] static ObjectModel<BluetoothAdapter>* adapters() {
		return BluetoothManager::instance()->adapters();
	}
	[[nodiscard]] static ObjectModel<BluetoothDevice>* devices() {
		return BluetoothManager::instance()->devices();
	}
	[[nodiscard]] static QBindable<BluetoothAdapter*> bindableDefaultAdapter() {
		return BluetoothManager::instance()->bindableDefaultAdapter();
	}

signals:
	void defaultAdapterChanged();
};

}

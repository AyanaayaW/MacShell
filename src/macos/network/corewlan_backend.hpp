#pragma once

#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../network/enums.hpp"
#include "../../network/qml.hpp"
#include "../../network/wifi.hpp"
#include "nm_settings.hpp"

namespace qs::network {

struct ScannedWifiNetwork {
	QString ssid;
	qint32 rssi = -100;
	WifiSecurityType::Enum security = WifiSecurityType::Unknown;
};

class CoreWlanBackend: public NetworkBackend {
	Q_OBJECT;

public:
	explicit CoreWlanBackend(QObject* parent = nullptr);
	~CoreWlanBackend() override;
	Q_DISABLE_COPY_MOVE(CoreWlanBackend);

	[[nodiscard]] bool isAvailable() const override { return this->mWifiDevice != nullptr; }
	[[nodiscard]] bool wifiEnabled() const { return this->bWifiEnabled; }
	[[nodiscard]] bool wifiHardwareEnabled() const { return this->bWifiHardwareEnabled; }
	[[nodiscard]] static bool connectivityCheckAvailable() { return true; }
	[[nodiscard]] bool connectivityCheckEnabled() const { return this->bConnectivityCheckEnabled; }
	[[nodiscard]] NetworkConnectivity::Enum connectivity() const { return this->bConnectivity; }

	void applyConnectivity(NetworkConnectivity::Enum connectivity);
	void applyScanResults(const QList<ScannedWifiNetwork>& results);
	void refreshInterfaceState();

signals:
	void deviceAdded(qs::network::NetworkDevice* device);
	void deviceRemoved(qs::network::NetworkDevice* device);
	void wifiEnabledChanged(bool enabled);
	void wifiHardwareEnabledChanged(bool enabled);
	void connectivityStateChanged(qs::network::NetworkConnectivity::Enum state);
	void connectivityCheckEnabledChanged(bool enabled);

public slots:
	void setWifiEnabled(bool enabled);
	void setConnectivityCheckEnabled(bool enabled);
	void checkConnectivity();

private:
	void startScan();
	void connectToNetwork(WifiNetwork* network, const QString& password);
	void disconnectFromNetwork();
	void forgetNetwork(WifiNetwork* network);
	WifiNetwork* networkForSsid(const QString& ssid);
	void removeNetwork(const QString& ssid);
	void updateNetworkStates();

	WifiDevice* mWifiDevice = nullptr;
	QHash<QString, WifiNetwork*> mNetworksBySsid;
	QHash<QString, NMSettings*> mSettingsBySsid;
	QString mConnectedSsid;
	QString mConnectingSsid;
	QTimer mScanTimer;
	bool mScanInFlight = false;
	void* nativeState = nullptr;

	Q_OBJECT_BINDABLE_PROPERTY(CoreWlanBackend, bool, bWifiEnabled, &CoreWlanBackend::wifiEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(CoreWlanBackend, bool, bWifiHardwareEnabled, &CoreWlanBackend::wifiHardwareEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(CoreWlanBackend, bool, bConnectivityCheckEnabled, true, &CoreWlanBackend::connectivityCheckEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(CoreWlanBackend, NetworkConnectivity::Enum, bConnectivity, &CoreWlanBackend::connectivityStateChanged);
};

}

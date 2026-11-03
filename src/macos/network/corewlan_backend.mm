#include "corewlan_backend.hpp"
#include <algorithm>
#include <array>
#include <utility>

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qpointer.h>
#include <qset.h>
#include <qstring.h>

#include "../../network/device.hpp"
#include "../../network/enums.hpp"
#include "../../network/wifi.hpp"
#include "nm_settings.hpp"

#import <CoreLocation/CoreLocation.h>
#import <Foundation/Foundation.h>
#import <Network/Network.h>

extern "C" {
#import <CoreWLAN/CoreWLAN.h>
}

using qs::network::CoreWlanBackend;

namespace {

Q_LOGGING_CATEGORY(logCoreWlan, "quickshell.network.corewlan", QtWarningMsg);

constexpr int SCAN_INTERVAL_MILLISECONDS = 10000;
constexpr qint32 WEAKEST_USABLE_RSSI = -100;
constexpr qint32 STRONGEST_EXPECTED_RSSI = -40;

qs::network::WifiSecurityType::Enum securityFor(CWNetwork* network) {
	using qs::network::WifiSecurityType;

	const std::array<std::pair<CWSecurity, WifiSecurityType::Enum>, 12> securityOrder {{
	    {kCWSecurityWPA3Enterprise, WifiSecurityType::Wpa3SuiteB192},
	    {kCWSecurityWPA3Personal, WifiSecurityType::Sae},
	    {kCWSecurityWPA3Transition, WifiSecurityType::Sae},
	    {kCWSecurityWPA2Enterprise, WifiSecurityType::Wpa2Eap},
	    {kCWSecurityWPA2Personal, WifiSecurityType::Wpa2Psk},
	    {kCWSecurityWPAEnterprise, WifiSecurityType::WpaEap},
	    {kCWSecurityWPAPersonal, WifiSecurityType::WpaPsk},
	    {kCWSecurityDynamicWEP, WifiSecurityType::DynamicWep},
	    {kCWSecurityWEP, WifiSecurityType::StaticWep},
	    {kCWSecurityOWE, WifiSecurityType::Owe},
	    {kCWSecurityOWETransition, WifiSecurityType::Owe},
	    {kCWSecurityNone, WifiSecurityType::Open},
	}};

	for (const auto& [nativeSecurity, security]: securityOrder) {
		if ([network supportsSecurity:nativeSecurity]) return security;
	}
	return WifiSecurityType::Unknown;
}

qreal signalStrengthFor(qint32 rssi) {
	auto clamped = std::clamp(rssi, WEAKEST_USABLE_RSSI, STRONGEST_EXPECTED_RSSI);
	return static_cast<qreal>(clamped - WEAKEST_USABLE_RSSI)
	     / static_cast<qreal>(STRONGEST_EXPECTED_RSSI - WEAKEST_USABLE_RSSI);
}

bool isInterfaceAssociated(CWInterface* interface) {
	return interface.powerOn && (interface.ssid != nil || interface.transmitRate > 0);
}

QSet<QString> knownSsids(CWInterface* interface) {
	QSet<QString> ssids;
	for (CWNetworkProfile* profile in interface.configuration.networkProfiles) {
		if (profile.ssid) ssids.insert(QString::fromNSString(profile.ssid));
	}
	return ssids;
}

bool runPrivilegedCommand(NSString* command) {
	NSString* escapedCommand = [[command stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"]
	    stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
	NSString* source =
	    [NSString stringWithFormat:@"do shell script \"%@\" with administrator privileges", escapedCommand];
	NSDictionary* errorInfo = nil;
	[[[NSAppleScript alloc] initWithSource:source] executeAndReturnError:&errorInfo];
	return errorInfo == nil;
}

template <typename Callback>
void postToBackend(CoreWlanBackend* backend, Callback callback) {
	QMetaObject::invokeMethod(backend, std::move(callback), Qt::QueuedConnection);
}

}

@interface MacShellWifiState: NSObject <CWEventDelegate>
@property(nonatomic, strong) CWWiFiClient* client;
@property(nonatomic, strong) CWInterface* interface;
@property(nonatomic, strong) CLLocationManager* locationManager;
@property(nonatomic, strong) dispatch_queue_t workQueue;
@property(nonatomic, strong) nw_path_monitor_t pathMonitor;
@end

@implementation MacShellWifiState {
	QPointer<CoreWlanBackend> owner;
}

- (instancetype)initWithOwner:(CoreWlanBackend*)backend {
	self = [super init];
	if (!self) return self;

	self->owner = backend;
	self.workQueue = dispatch_queue_create("MacShell.CoreWLAN", DISPATCH_QUEUE_SERIAL);
	self.client = CWWiFiClient.sharedWiFiClient;
	self.interface = self.client.interface;
	self.client.delegate = self;

	for (NSNumber* eventType in @[
		@(CWEventTypePowerDidChange),
		@(CWEventTypeSSIDDidChange),
		@(CWEventTypeLinkDidChange),
		@(CWEventTypeLinkQualityDidChange),
		@(CWEventTypeScanCacheUpdated),
	])
	{
		[self.client startMonitoringEventWithType:static_cast<CWEventType>(eventType.integerValue) error:nil];
	}

	self.locationManager = [[CLLocationManager alloc] init];
	if (self.locationManager.authorizationStatus == kCLAuthorizationStatusNotDetermined) {
		[self.locationManager requestWhenInUseAuthorization];
	}

	return self;
}

- (void)notifyInterfaceChanged {
	QPointer<CoreWlanBackend> backend = self->owner;
	if (!backend) return;
	postToBackend(backend, [backend]() {
		if (backend) backend->refreshInterfaceState();
	});
}

- (void)powerStateDidChangeForWiFiInterfaceWithName:(NSString*)interfaceName {
	[self notifyInterfaceChanged];
}

- (void)ssidDidChangeForWiFiInterfaceWithName:(NSString*)interfaceName {
	[self notifyInterfaceChanged];
}

- (void)linkDidChangeForWiFiInterfaceWithName:(NSString*)interfaceName {
	[self notifyInterfaceChanged];
}

- (void)linkQualityDidChangeForWiFiInterfaceWithName:(NSString*)interfaceName
                                                rssi:(NSInteger)rssi
                                        transmitRate:(double)transmitRate {}

- (void)scanCacheUpdatedForWiFiInterfaceWithName:(NSString*)interfaceName {
	[self notifyInterfaceChanged];
}

- (void)startPathMonitor {
	QPointer<CoreWlanBackend> backend = self->owner;
	self.pathMonitor = nw_path_monitor_create();
	nw_path_monitor_set_queue(self.pathMonitor, dispatch_get_main_queue());
	nw_path_monitor_set_update_handler(self.pathMonitor, ^(nw_path_t path) {
		using qs::network::NetworkConnectivity;
		auto connectivity = NetworkConnectivity::Unknown;
		switch (nw_path_get_status(path)) {
		case nw_path_status_satisfied: connectivity = NetworkConnectivity::Full; break;
		case nw_path_status_satisfiable: connectivity = NetworkConnectivity::Limited; break;
		case nw_path_status_unsatisfied: connectivity = NetworkConnectivity::None; break;
		default: break;
		}
		if (backend) backend->applyConnectivity(connectivity);
	});
	nw_path_monitor_start(self.pathMonitor);
}

- (void)stop {
	self.client.delegate = nil;
	[self.client stopMonitoringAllEventsAndReturnError:nil];
	if (self.pathMonitor) nw_path_monitor_cancel(self.pathMonitor);
}

@end

namespace qs::network {

namespace {

MacShellWifiState* wifiState(void* nativeState) { return (__bridge MacShellWifiState*)nativeState; }

}

CoreWlanBackend::CoreWlanBackend(QObject* parent): NetworkBackend(parent) {
	MacShellWifiState* state = [[MacShellWifiState alloc] initWithOwner:this];
	this->nativeState = (__bridge_retained void*)state;

	if (!state.interface) {
		qCWarning(logCoreWlan) << "No Wi-Fi interface found.";
		return;
	}

	this->mWifiDevice = new WifiDevice(this);
	this->mWifiDevice->bindableName().setValue(QString::fromNSString(state.interface.interfaceName ?: @""));
	this->mWifiDevice->bindableAddress().setValue(QString::fromNSString(state.interface.hardwareAddress ?: @""));
	this->mWifiDevice->bindableNmManaged().setValue(true);
	this->mWifiDevice->bindableAutoconnect().setValue(true);
	this->mWifiDevice->bindableMode().setValue(WifiDeviceMode::Station);

	QObject::connect(this->mWifiDevice, &NetworkDevice::requestDisconnect, this, &CoreWlanBackend::disconnectFromNetwork);
	QObject::connect(this->mWifiDevice, &NetworkDevice::requestSetAutoconnect, this, [this](bool autoconnect) {
		this->mWifiDevice->bindableAutoconnect().setValue(autoconnect);
	});
	QObject::connect(this->mWifiDevice, &WifiDevice::scannerEnabledChanged, this, [this](bool enabled) {
		if (enabled) {
			this->startScan();
			this->mScanTimer.start();
		} else {
			this->mScanTimer.stop();
		}
	});

	this->mScanTimer.setInterval(SCAN_INTERVAL_MILLISECONDS);
	QObject::connect(&this->mScanTimer, &QTimer::timeout, this, &CoreWlanBackend::startScan);

	[state startPathMonitor];
	this->refreshInterfaceState();
	this->startScan();

	QMetaObject::invokeMethod(
	    this,
	    [this]() { emit this->deviceAdded(this->mWifiDevice); },
	    Qt::QueuedConnection
	);
}

CoreWlanBackend::~CoreWlanBackend() {
	MacShellWifiState* state = (__bridge_transfer MacShellWifiState*)this->nativeState;
	[state stop];
}

void CoreWlanBackend::refreshInterfaceState() {
	CWInterface* interface = wifiState(this->nativeState).interface;
	if (!interface) return;

	this->bWifiHardwareEnabled = true;
	this->bWifiEnabled = interface.powerOn;

	auto isAssociated = isInterfaceAssociated(interface);
	auto currentSsid = QString::fromNSString(interface.ssid ?: @"");
	this->mConnectedSsid = isAssociated ? currentSsid : QString();
	if (this->mConnectedSsid == this->mConnectingSsid) this->mConnectingSsid.clear();

	this->mWifiDevice->bindableConnected().setValue(isAssociated);
	this->mWifiDevice->bindableState().setValue(
	    !this->mConnectingSsid.isEmpty() ? ConnectionState::Connecting
	    : isAssociated                   ? ConnectionState::Connected
	                                     : ConnectionState::Disconnected
	);

	QList<ScannedWifiNetwork> cachedResults;
	for (CWNetwork* network in interface.cachedScanResults) {
		if (!network.ssid) continue;
		cachedResults.append({
		    .ssid = QString::fromNSString(network.ssid),
		    .rssi = static_cast<qint32>(network.rssiValue),
		    .security = securityFor(network),
		});
	}

	if (!this->mConnectedSsid.isEmpty()) {
		auto alreadyListed = std::ranges::any_of(cachedResults, [this](const ScannedWifiNetwork& network) {
			return network.ssid == this->mConnectedSsid;
		});
		if (!alreadyListed) {
			cachedResults.append({
			    .ssid = this->mConnectedSsid,
			    .rssi = static_cast<qint32>(interface.rssiValue),
			    .security = WifiSecurityType::Unknown,
			});
		}
	}

	this->applyScanResults(cachedResults);
}

void CoreWlanBackend::applyScanResults(const QList<ScannedWifiNetwork>& results) {
	CWInterface* interface = wifiState(this->nativeState).interface;
	auto known = knownSsids(interface);

	QHash<QString, ScannedWifiNetwork> strongestBySsid;
	for (const auto& result: results) {
		auto existing = strongestBySsid.find(result.ssid);
		if (existing == strongestBySsid.end() || existing->rssi < result.rssi) {
			strongestBySsid.insert(result.ssid, result);
		}
	}

	for (const auto& [ssid, result]: strongestBySsid.asKeyValueRange()) {
		auto* network = this->networkForSsid(ssid);
		network->bindableSignalStrength().setValue(signalStrengthFor(result.rssi));
		if (result.security != WifiSecurityType::Unknown) network->bindableSecurity().setValue(result.security);
		network->bindableKnown().setValue(known.contains(ssid));

		if (known.contains(ssid) && !this->mSettingsBySsid.contains(ssid)) {
			auto* settings = new NMSettings(ssid, network);
			QObject::connect(settings, &NMSettings::forgetRequested, network, &Network::forget);
			this->mSettingsBySsid.insert(ssid, settings);
			network->settingsAdded(settings);
		}
	}

	for (const auto& ssid: QList(this->mNetworksBySsid.keys())) {
		if (!strongestBySsid.contains(ssid) && ssid != this->mConnectedSsid) this->removeNetwork(ssid);
	}

	this->updateNetworkStates();
}

void CoreWlanBackend::updateNetworkStates() {
	for (const auto& [ssid, network]: this->mNetworksBySsid.asKeyValueRange()) {
		auto isConnecting = ssid == this->mConnectingSsid;
		auto isConnected = ssid == this->mConnectedSsid;

		network->bindableConnected().setValue(isConnected);
		network->bindableStateChanging().setValue(isConnecting);
		network->bindableState().setValue(
		    isConnecting  ? ConnectionState::Connecting
		    : isConnected ? ConnectionState::Connected
		                  : ConnectionState::Disconnected
		);
	}
}

WifiNetwork* CoreWlanBackend::networkForSsid(const QString& ssid) {
	if (auto* network = this->mNetworksBySsid.value(ssid)) return network;

	auto* network = new WifiNetwork(ssid, this->mWifiDevice, this->mWifiDevice);
	this->mNetworksBySsid.insert(ssid, network);

	QObject::connect(network, &Network::requestConnect, this, [this, network]() {
		this->connectToNetwork(network, QString());
	});
	QObject::connect(network, &WifiNetwork::requestConnectWithPsk, this, [this, network](const QString& psk) {
		this->connectToNetwork(network, psk);
	});
	QObject::connect(network, &Network::requestConnectWithSettings, this, [this, network]() {
		this->connectToNetwork(network, QString());
	});
	QObject::connect(network, &Network::requestDisconnect, this, &CoreWlanBackend::disconnectFromNetwork);
	QObject::connect(network, &Network::requestForget, this, [this, network]() { this->forgetNetwork(network); });

	this->mWifiDevice->networkAdded(network);
	return network;
}

void CoreWlanBackend::removeNetwork(const QString& ssid) {
	auto* network = this->mNetworksBySsid.take(ssid);
	if (!network) return;

	this->mSettingsBySsid.remove(ssid);
	this->mWifiDevice->networkRemoved(network);
	network->deleteLater();
}

void CoreWlanBackend::startScan() {
	if (this->mScanInFlight) return;

	auto* state = wifiState(this->nativeState);
	CWInterface* interface = state.interface;
	if (!interface || !interface.powerOn) return;

	this->mScanInFlight = true;
	QPointer<CoreWlanBackend> backend = this;

	dispatch_async(state.workQueue, ^{
		NSError* error = nil;
		NSSet<CWNetwork*>* networks = [interface scanForNetworksWithName:nil error:&error];

		QList<ScannedWifiNetwork> results;
		for (CWNetwork* network in networks) {
			if (!network.ssid) continue;
			results.append({
			    .ssid = QString::fromNSString(network.ssid),
			    .rssi = static_cast<qint32>(network.rssiValue),
			    .security = securityFor(network),
			});
		}

		if (!backend) return;
		postToBackend(backend, [backend, results]() {
			if (!backend) return;
			backend->mScanInFlight = false;
			backend->applyScanResults(results);
		});
	});
}

void CoreWlanBackend::connectToNetwork(WifiNetwork* network, const QString& password) {
	auto* state = wifiState(this->nativeState);
	CWInterface* interface = state.interface;
	auto ssid = network->name();
	auto isKnown = network->bindableKnown().value();
	auto isOpen = network->bindableSecurity().value() == WifiSecurityType::Open;

	this->mConnectingSsid = ssid;
	this->mWifiDevice->bindableState().setValue(ConnectionState::Connecting);
	this->updateNetworkStates();

	QPointer<CoreWlanBackend> backend = this;
	QPointer<WifiNetwork> targetNetwork = network;
	NSString* nativeSsid = ssid.toNSString();
	NSString* nativePassword = password.isEmpty() ? nil : password.toNSString();

	dispatch_async(state.workQueue, ^{
		NSString* resolvedPassword = nativePassword;
		if (!resolvedPassword && isKnown && !isOpen) {
			NSString* storedPassword = nil;
			NSData* ssidData = [nativeSsid dataUsingEncoding:NSUTF8StringEncoding];
			if (CWKeychainFindWiFiPassword(kCWKeychainDomainSystem, ssidData, &storedPassword) == errSecSuccess) {
				resolvedPassword = storedPassword;
			}
		}

		auto failureReason = ConnectionFailReason::Unknown;
		auto succeeded = false;

		if (!resolvedPassword && !isOpen) {
			failureReason = ConnectionFailReason::NoSecrets;
		} else {
			NSError* error = nil;
			NSSet<CWNetwork*>* matches = [interface scanForNetworksWithName:nativeSsid error:&error];
			CWNetwork* target = matches.anyObject;
			succeeded = target && [interface associateToNetwork:target password:resolvedPassword error:&error];
			if (!succeeded) failureReason = target ? ConnectionFailReason::WifiClientFailed : ConnectionFailReason::WifiNetworkLost;
		}

		if (!backend) return;
		postToBackend(backend, [backend, targetNetwork, succeeded, failureReason]() {
			if (!backend) return;
			backend->mConnectingSsid.clear();
			if (!succeeded && targetNetwork) emit targetNetwork->connectionFailed(failureReason);
			backend->refreshInterfaceState();
		});
	});
}

void CoreWlanBackend::disconnectFromNetwork() {
	auto* state = wifiState(this->nativeState);
	this->mWifiDevice->bindableState().setValue(ConnectionState::Disconnecting);
	[state.interface disassociate];
	this->refreshInterfaceState();
}

void CoreWlanBackend::forgetNetwork(WifiNetwork* network) {
	auto* state = wifiState(this->nativeState);
	NSString* interfaceName = state.interface.interfaceName;
	if (!interfaceName) return;

	auto command = QStringLiteral("/usr/sbin/networksetup -removepreferredwirelessnetwork '%1' '%2'")
	                   .arg(QString::fromNSString(interfaceName), QString(network->name()).replace(u'\'', QStringLiteral("'\\''")));

	if (!runPrivilegedCommand(command.toNSString())) {
		qCWarning(logCoreWlan) << "Forgetting" << network->name() << "was cancelled or failed.";
		return;
	}

	if (auto* settings = this->mSettingsBySsid.take(network->name())) {
		network->settingsRemoved(settings);
		settings->deleteLater();
	}
	this->refreshInterfaceState();
}

void CoreWlanBackend::setWifiEnabled(bool enabled) {
	NSError* error = nil;
	if (![wifiState(this->nativeState).interface setPower:enabled error:&error]) {
		qCWarning(logCoreWlan) << "Could not change Wi-Fi power:" << QString::fromNSString(error.localizedDescription);
	}
	this->refreshInterfaceState();
}

void CoreWlanBackend::setConnectivityCheckEnabled(bool enabled) { this->bConnectivityCheckEnabled = enabled; }

void CoreWlanBackend::checkConnectivity() {
	auto* state = wifiState(this->nativeState);
	if (state.pathMonitor) {
		nw_path_monitor_cancel(state.pathMonitor);
		[state startPathMonitor];
	}
}

void CoreWlanBackend::applyConnectivity(NetworkConnectivity::Enum connectivity) {
	if (this->bConnectivityCheckEnabled) this->bConnectivity = connectivity;
}

}

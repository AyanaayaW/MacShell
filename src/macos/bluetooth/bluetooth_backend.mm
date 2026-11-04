#include "bluetooth_backend.hpp"
#include <algorithm>
#include <initializer_list>
#include <utility>

#include <qcoreapplication.h>
#include <qhash.h>
#include <qmetaobject.h>
#include <qstring.h>

#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>
#import <IOBluetooth/IOBluetooth.h>
#import <IOKit/IOKitLib.h>

extern "C" {
int IOBluetoothPreferenceGetControllerPowerState();
void IOBluetoothPreferenceSetControllerPowerState(int state);
int IOBluetoothPreferenceGetDiscoverableState();
void IOBluetoothPreferenceSetDiscoverableState(int state);
int IOBluetoothPreferenceGetRemoteWakeEnabled();
void IOBluetoothPreferenceSetRemoteWakeEnabled(int enabled);
}

using qs::bluetooth::macos::DeviceInfo;

namespace {

constexpr BluetoothDeviceClassMinor AUDIO_MINOR_HEADSET = 1;
constexpr BluetoothDeviceClassMinor AUDIO_MINOR_HANDS_FREE = 2;
constexpr BluetoothDeviceClassMinor AUDIO_MINOR_LOUDSPEAKER = 5;
constexpr BluetoothDeviceClassMinor AUDIO_MINOR_HEADPHONES = 6;
constexpr BluetoothDeviceClassMinor AUDIO_MINOR_VIDEO_MONITOR = 14;
constexpr BluetoothDeviceClassMinor AUDIO_MINOR_VIDEO_DISPLAY_AND_LOUDSPEAKER = 15;
constexpr BluetoothDeviceClassMinor AUDIO_MINOR_GAMING_TOY = 18;
constexpr BluetoothDeviceClassMinor PERIPHERAL_KEYBOARD_BIT = 0x10;
constexpr BluetoothDeviceClassMinor PERIPHERAL_POINTING_BIT = 0x20;
constexpr BluetoothDeviceClassMinor PERIPHERAL_SUBTYPE_MASK = 0x0f;
constexpr BluetoothDeviceClassMinor PERIPHERAL_JOYSTICK = 1;
constexpr BluetoothDeviceClassMinor PERIPHERAL_GAMEPAD = 2;
constexpr BluetoothDeviceClassMinor PERIPHERAL_DIGITIZER = 5;

QString normalizedAddress(const QString& address) {
	QString normalized;
	for (auto character: address) {
		if (character.isLetterOrNumber()) normalized.append(character.toLower());
	}
	return normalized;
}

QString iconNameFromDeviceName(const QString& deviceName) {
	auto lowercaseName = deviceName.toLower();

	auto containsAny = [&lowercaseName](std::initializer_list<const char*> keywords) {
		return std::ranges::any_of(keywords, [&lowercaseName](const char* keyword) {
			return lowercaseName.contains(QLatin1StringView(keyword));
		});
	};

	if (containsAny({"controller", "gamepad", "joy-con", "dualsense", "dualshock"})) {
		return QStringLiteral("input-gaming");
	}
	if (containsAny({"iphone", "phone", "pixel", "galaxy"})) return QStringLiteral("phone");
	if (containsAny({"airpods", "buds", "headphone", "beats"})) return QStringLiteral("audio-headphones");
	if (containsAny({"keyboard"})) return QStringLiteral("input-keyboard");
	if (containsAny({"mouse"})) return QStringLiteral("input-mouse");
	if (containsAny({"trackpad"})) return QStringLiteral("input-touchpad");
	if (containsAny({"ipad", "tablet"})) return QStringLiteral("computer");
	if (containsAny({"tv", "display"})) return QStringLiteral("video-display");
	return QStringLiteral("bluetooth");
}

QString iconNameFor(IOBluetoothDevice* device) {
	auto majorClass = device.deviceClassMajor;
	auto minorClass = device.deviceClassMinor;

	switch (majorClass) {
	case kBluetoothDeviceClassMajorAudio:
		switch (minorClass) {
		case AUDIO_MINOR_HEADSET:
		case AUDIO_MINOR_HANDS_FREE: return QStringLiteral("audio-headset");
		case AUDIO_MINOR_HEADPHONES: return QStringLiteral("audio-headphones");
		case AUDIO_MINOR_LOUDSPEAKER: return QStringLiteral("audio-speakers");
		case AUDIO_MINOR_VIDEO_MONITOR:
		case AUDIO_MINOR_VIDEO_DISPLAY_AND_LOUDSPEAKER: return QStringLiteral("video-display");
		case AUDIO_MINOR_GAMING_TOY: return QStringLiteral("input-gaming");
		default: return QStringLiteral("audio-card");
		}
	case kBluetoothDeviceClassMajorPeripheral: {
		auto subtype = minorClass & PERIPHERAL_SUBTYPE_MASK;
		if (subtype == PERIPHERAL_JOYSTICK || subtype == PERIPHERAL_GAMEPAD) {
			return QStringLiteral("input-gaming");
		}
		if (subtype == PERIPHERAL_DIGITIZER) return QStringLiteral("input-tablet");
		if ((minorClass & PERIPHERAL_KEYBOARD_BIT) != 0) return QStringLiteral("input-keyboard");
		if ((minorClass & PERIPHERAL_POINTING_BIT) != 0) return QStringLiteral("input-mouse");
		return QStringLiteral("input-keyboard");
	}
	case kBluetoothDeviceClassMajorPhone: return QStringLiteral("phone");
	case kBluetoothDeviceClassMajorComputer: return QStringLiteral("computer");
	case kBluetoothDeviceClassMajorImaging: return QStringLiteral("printer");
	default: return iconNameFromDeviceName(QString::fromNSString(device.name ?: @""));
	}
}

QHash<QString, qreal> peripheralBatteryFractions() {
	QHash<QString, qreal> fractions;

	io_iterator_t iterator = IO_OBJECT_NULL;
	if (IOServiceGetMatchingServices(
	        kIOMainPortDefault,
	        IOServiceMatching("AppleDeviceManagementHIDEventService"),
	        &iterator
	    )
	    != KERN_SUCCESS)
	{
		return fractions;
	}

	while (io_object_t service = IOIteratorNext(iterator)) {
		CFMutableDictionaryRef rawProperties = nullptr;
		if (IORegistryEntryCreateCFProperties(service, &rawProperties, kCFAllocatorDefault, 0) == KERN_SUCCESS) {
			NSDictionary* properties = CFBridgingRelease(rawProperties);
			NSNumber* batteryPercent = properties[@"BatteryPercent"];
			NSString* deviceAddress = properties[@"DeviceAddress"];

			if (batteryPercent && deviceAddress) {
				fractions.insert(
				    normalizedAddress(QString::fromNSString(deviceAddress)),
				    batteryPercent.doubleValue / 100.0
				);
			}
		}
		IOObjectRelease(service);
	}

	IOObjectRelease(iterator);
	return fractions;
}

std::optional<qreal> appleAccessoryBatteryFraction(IOBluetoothDevice* device) {
	for (NSString* key in @[@"batteryPercentCombined", @"batteryPercentSingle"]) {
		if (![device respondsToSelector:NSSelectorFromString(key)]) continue;

		NSNumber* percent = [device valueForKey:key];
		if (percent && percent.integerValue > 0) return percent.doubleValue / 100.0;
	}
	return std::nullopt;
}

DeviceInfo describeDevice(IOBluetoothDevice* device, const QHash<QString, qreal>& batteryFractions) {
	DeviceInfo info;
	info.address = QString::fromNSString(device.addressString ?: @"").toUpper().replace(u'-', u':');
	info.name = QString::fromNSString(device.name ?: device.addressString ?: @"");
	info.iconName = iconNameFor(device);
	info.isConnected = device.isConnected;
	info.isPaired = device.isPaired;

	if (auto fraction = batteryFractions.find(normalizedAddress(info.address)); fraction != batteryFractions.end()) {
		info.batteryFraction = fraction.value();
	} else {
		info.batteryFraction = appleAccessoryBatteryFraction(device);
	}

	return info;
}

IOBluetoothDevice* deviceForAddress(const QString& address) {
	return [IOBluetoothDevice deviceWithAddressString:QString(address).replace(u':', u'-').toNSString()];
}

void runOnMainQueue(std::function<void(bool)> callback, bool succeeded) {
	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [callback, succeeded]() {
		    if (callback) callback(succeeded);
	    },
	    Qt::QueuedConnection
	);
}

}

@class MacShellBluetoothAccessRequest;

static NSMutableArray<MacShellBluetoothAccessRequest*>* pendingAccessRequests() {
	static NSMutableArray<MacShellBluetoothAccessRequest*>* requests = [NSMutableArray array];
	return requests;
}

@interface MacShellBluetoothAccessRequest: NSObject <CBCentralManagerDelegate>
@property(nonatomic, strong) CBCentralManager* centralManager;
@end

@implementation MacShellBluetoothAccessRequest {
	std::function<void(bool)> decisionHandler;
}

- (instancetype)initWithHandler:(std::function<void(bool)>)handler {
	self = [super init];
	if (self) {
		self->decisionHandler = std::move(handler);
		dispatch_queue_t delegateQueue = dispatch_queue_create("MacShell.BluetoothAccess", DISPATCH_QUEUE_SERIAL);
		self.centralManager = [[CBCentralManager alloc] initWithDelegate:self
		                                                          queue:delegateQueue
		                                                        options:@{CBCentralManagerOptionShowPowerAlertKey: @NO}];
	}
	return self;
}

- (void)centralManagerDidUpdateState:(CBCentralManager*)central {
	auto authorization = CBManager.authorization;
	if (authorization == CBManagerAuthorizationNotDetermined) return;

	auto handler = std::exchange(self->decisionHandler, nullptr);
	auto granted = authorization == CBManagerAuthorizationAllowedAlways;
	MacShellBluetoothAccessRequest* finishedRequest = self;

	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [handler, granted, finishedRequest]() {
		    if (handler) handler(granted);
		    finishedRequest.centralManager.delegate = nil;
		    [pendingAccessRequests() removeObject:finishedRequest];
	    },
	    Qt::QueuedConnection
	);
}

@end

@interface MacShellBluetoothInquiryDelegate: NSObject <IOBluetoothDeviceInquiryDelegate>
@property(nonatomic, strong) IOBluetoothDeviceInquiry* inquiry;
@property(nonatomic) BOOL shouldContinue;
@end

@implementation MacShellBluetoothInquiryDelegate {
	std::function<void(const DeviceInfo&)> deviceFoundHandler;
}

- (instancetype)initWithHandler:(std::function<void(const DeviceInfo&)>)handler {
	self = [super init];
	if (self) {
		self->deviceFoundHandler = std::move(handler);
		self.inquiry = [IOBluetoothDeviceInquiry inquiryWithDelegate:self];
		self.inquiry.updateNewDeviceNames = YES;
	}
	return self;
}

- (void)deviceInquiryDeviceFound:(IOBluetoothDeviceInquiry*)sender device:(IOBluetoothDevice*)device {
	auto handler = self->deviceFoundHandler;
	if (!handler) return;
	IOBluetoothDevice* foundDevice = device;
	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [handler, foundDevice] { handler(describeDevice(foundDevice, {})); },
	    Qt::QueuedConnection
	);
}

- (void)deviceInquiryDeviceNameUpdated:(IOBluetoothDeviceInquiry*)sender
                                device:(IOBluetoothDevice*)device
                      devicesRemaining:(uint32_t)devicesRemaining {
	auto handler = self->deviceFoundHandler;
	if (!handler) return;
	IOBluetoothDevice* foundDevice = device;
	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [handler, foundDevice] { handler(describeDevice(foundDevice, {})); },
	    Qt::QueuedConnection
	);
}

- (void)deviceInquiryComplete:(IOBluetoothDeviceInquiry*)sender error:(IOReturn)error aborted:(BOOL)aborted {
	if (self.shouldContinue && !aborted) [self.inquiry start];
}

@end

@interface MacShellBluetoothPairingDelegate: NSObject
@property(nonatomic, strong) IOBluetoothDevicePair* pairing;
@end

@implementation MacShellBluetoothPairingDelegate {
	std::function<void(bool)> finishedHandler;
}

- (instancetype)initWithDevice:(IOBluetoothDevice*)device handler:(std::function<void(bool)>)handler {
	self = [super init];
	if (self) {
		self->finishedHandler = std::move(handler);
		self.pairing = [IOBluetoothDevicePair pairWithDevice:device];
		self.pairing.delegate = self;
	}
	return self;
}

- (void)devicePairingUserConfirmationRequest:(id)sender numericValue:(BluetoothNumericValue)numericValue {
	[self.pairing replyUserConfirmation:YES];
}

- (void)devicePairingFinished:(id)sender error:(IOReturn)error {
	auto handler = std::exchange(self->finishedHandler, nullptr);
	if (!handler) return;
	auto succeeded = error == kIOReturnSuccess;
	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [handler, succeeded] { handler(succeeded); },
	    Qt::QueuedConnection
	);
}

@end

@interface MacShellBluetoothConnectionDelegate: NSObject
@property(nonatomic, strong) IOBluetoothUserNotification* connectNotification;
@property(nonatomic, strong) NSMutableArray<IOBluetoothUserNotification*>* disconnectNotifications;
@end

@implementation MacShellBluetoothConnectionDelegate {
	std::function<void()> changedHandler;
}

- (instancetype)initWithHandler:(std::function<void()>)handler {
	self = [super init];
	if (self) {
		self->changedHandler = std::move(handler);
		self.disconnectNotifications = [NSMutableArray array];
		self.connectNotification = [IOBluetoothDevice registerForConnectNotifications:self
		                                                                     selector:@selector(deviceConnected:device:)];
	}
	return self;
}

- (void)deviceConnected:(IOBluetoothUserNotification*)notification device:(IOBluetoothDevice*)device {
	IOBluetoothUserNotification* disconnectNotification =
	    [device registerForDisconnectNotification:self selector:@selector(deviceDisconnected:device:)];
	if (disconnectNotification) [self.disconnectNotifications addObject:disconnectNotification];
	[self notifyChanged];
}

- (void)deviceDisconnected:(IOBluetoothUserNotification*)notification device:(IOBluetoothDevice*)device {
	[notification unregister];
	[self.disconnectNotifications removeObject:notification];
	[self notifyChanged];
}

- (void)notifyChanged {
	auto handler = self->changedHandler;
	if (!handler) return;
	QMetaObject::invokeMethod(QCoreApplication::instance(), handler, Qt::QueuedConnection);
}

- (void)stopObserving {
	[self.connectNotification unregister];
	for (IOBluetoothUserNotification* notification in self.disconnectNotifications) {
		[notification unregister];
	}
	[self.disconnectNotifications removeAllObjects];
	self->changedHandler = nullptr;
}

@end

namespace qs::bluetooth::macos {

void requestBluetoothAccess(std::function<void(bool granted)> onDecided) {
	auto authorization = CBManager.authorization;
	if (authorization != CBManagerAuthorizationNotDetermined) {
		onDecided(authorization == CBManagerAuthorizationAllowedAlways);
		return;
	}

	[pendingAccessRequests() addObject:[[MacShellBluetoothAccessRequest alloc] initWithHandler:std::move(onDecided)]];
}

ControllerInfo readController() {
	ControllerInfo info;
	IOBluetoothHostController* controller = IOBluetoothHostController.defaultController;
	if (!controller) return info;

	info.isAvailable = true;
	info.name = QString::fromNSString(controller.nameAsString ?: @"");
	info.address = QString::fromNSString(controller.addressAsString ?: @"").toUpper().replace(u'-', u':');
	info.isPoweredOn = IOBluetoothPreferenceGetControllerPowerState() != 0;
	info.isDiscoverable = IOBluetoothPreferenceGetDiscoverableState() != 0;
	info.isRemoteWakeEnabled = IOBluetoothPreferenceGetRemoteWakeEnabled() != 0;
	return info;
}

void setPoweredOn(bool poweredOn) { IOBluetoothPreferenceSetControllerPowerState(poweredOn ? 1 : 0); }

void setDiscoverable(bool discoverable) {
	IOBluetoothPreferenceSetDiscoverableState(discoverable ? 1 : 0);
}

void setRemoteWakeEnabled(bool enabled) { IOBluetoothPreferenceSetRemoteWakeEnabled(enabled ? 1 : 0); }

QList<DeviceInfo> readPairedDevices() {
	QList<DeviceInfo> devices;
	auto batteryFractions = peripheralBatteryFractions();

	for (IOBluetoothDevice* device in IOBluetoothDevice.pairedDevices) {
		devices.append(describeDevice(device, batteryFractions));
	}

	return devices;
}

std::optional<DeviceInfo> readDevice(const QString& address) {
	IOBluetoothDevice* device = deviceForAddress(address);
	if (!device) return std::nullopt;
	return describeDevice(device, peripheralBatteryFractions());
}

void connectDevice(const QString& address, std::function<void(bool succeeded)> onFinished) {
	IOBluetoothDevice* device = deviceForAddress(address);
	if (!device) {
		runOnMainQueue(std::move(onFinished), false);
		return;
	}

	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		auto result = [device openConnection];
		runOnMainQueue(onFinished, result == kIOReturnSuccess);
	});
}

void disconnectDevice(const QString& address, std::function<void(bool succeeded)> onFinished) {
	IOBluetoothDevice* device = deviceForAddress(address);
	if (!device) {
		runOnMainQueue(std::move(onFinished), false);
		return;
	}

	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		auto result = [device closeConnection];
		runOnMainQueue(onFinished, result == kIOReturnSuccess);
	});
}

bool forgetDevice(const QString& address) {
	IOBluetoothDevice* device = deviceForAddress(address);
	SEL removeSelector = NSSelectorFromString(@"remove");
	if (!device || ![device respondsToSelector:removeSelector]) return false;

	using RemoveFunction = void (*)(id, SEL);
	reinterpret_cast<RemoveFunction>([device methodForSelector:removeSelector])(device, removeSelector);
	return true;
}

DeviceDiscovery::DeviceDiscovery(std::function<void(const DeviceInfo&)> onDeviceFound) {
	MacShellBluetoothInquiryDelegate* delegate =
	    [[MacShellBluetoothInquiryDelegate alloc] initWithHandler:std::move(onDeviceFound)];
	this->inquiryDelegate = (__bridge_retained void*)delegate;
}

DeviceDiscovery::~DeviceDiscovery() {
	this->stop();
	MacShellBluetoothInquiryDelegate* delegate =
	    (__bridge_transfer MacShellBluetoothInquiryDelegate*)this->inquiryDelegate;
	delegate.inquiry.delegate = nil;
}

void DeviceDiscovery::start() {
	auto* delegate = (__bridge MacShellBluetoothInquiryDelegate*)this->inquiryDelegate;
	delegate.shouldContinue = YES;
	this->running = [delegate.inquiry start] == kIOReturnSuccess;
}

void DeviceDiscovery::stop() {
	auto* delegate = (__bridge MacShellBluetoothInquiryDelegate*)this->inquiryDelegate;
	delegate.shouldContinue = NO;
	if (this->running) [delegate.inquiry stop];
	this->running = false;
}

DevicePairing::DevicePairing(const QString& address, std::function<void(bool succeeded)> onFinished) {
	IOBluetoothDevice* device = deviceForAddress(address);
	if (!device) return;

	MacShellBluetoothPairingDelegate* delegate =
	    [[MacShellBluetoothPairingDelegate alloc] initWithDevice:device handler:std::move(onFinished)];
	if ([delegate.pairing start] != kIOReturnSuccess) return;

	this->pairingDelegate = (__bridge_retained void*)delegate;
}

DevicePairing::~DevicePairing() {
	this->cancel();
	if (!this->pairingDelegate) return;

	MacShellBluetoothPairingDelegate* delegate =
	    (__bridge_transfer MacShellBluetoothPairingDelegate*)this->pairingDelegate;
	delegate.pairing.delegate = nil;
	this->pairingDelegate = nullptr;
}

void DevicePairing::cancel() {
	if (!this->pairingDelegate) return;
	auto* delegate = (__bridge MacShellBluetoothPairingDelegate*)this->pairingDelegate;
	[delegate.pairing stop];
}

ConnectionObserver::ConnectionObserver(std::function<void()> onConnectionChanged) {
	MacShellBluetoothConnectionDelegate* delegate =
	    [[MacShellBluetoothConnectionDelegate alloc] initWithHandler:std::move(onConnectionChanged)];
	this->observerDelegate = (__bridge_retained void*)delegate;
}

ConnectionObserver::~ConnectionObserver() {
	MacShellBluetoothConnectionDelegate* delegate =
	    (__bridge_transfer MacShellBluetoothConnectionDelegate*)this->observerDelegate;
	[delegate stopObserving];
}

}

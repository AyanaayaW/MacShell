#include "idle.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <qobject.h>

namespace {

constexpr auto IDLE_POLL_INTERVAL_MILLISECONDS = 1000;

bool isAnyDisplaySleepAssertionActive() {
	CFDictionaryRef rawStatus = nullptr;
	if (IOPMCopyAssertionsStatus(&rawStatus) != kIOReturnSuccess || !rawStatus) return false;

	auto isActive = false;
	for (const auto* assertionType: {kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertPreventUserIdleSystemSleep}) {
		const auto* value = static_cast<CFNumberRef>(CFDictionaryGetValue(rawStatus, assertionType));
		int level = 0;
		if (value && CFNumberGetValue(value, kCFNumberIntType, &level) && level > 0) isActive = true;
	}

	CFRelease(rawStatus);
	return isActive;
}

qreal secondsSinceLastInput() {
	return CGEventSourceSecondsSinceLastEventType(
	    kCGEventSourceStateCombinedSessionState,
	    kCGAnyInputEventType
	);
}

}

namespace qs::wayland::idle_inhibit {

IdleInhibitor::IdleInhibitor(QObject* parent): QObject(parent) {
	QObject::connect(this, &IdleInhibitor::enabledChanged, this, &IdleInhibitor::updateAssertion);
}

IdleInhibitor::~IdleInhibitor() {
	if (this->assertionId != 0) IOPMAssertionRelease(this->assertionId);
}

void IdleInhibitor::setWindow(QObject* window) {
	if (window == this->mWindow) return;
	this->mWindow = window;
	emit this->windowChanged();
}

void IdleInhibitor::updateAssertion() {
	auto shouldInhibit = this->bEnabled.value();

	if (shouldInhibit && this->assertionId == 0) {
		IOPMAssertionID newAssertion = 0;
		if (IOPMAssertionCreateWithName(
		        kIOPMAssertionTypePreventUserIdleDisplaySleep,
		        kIOPMAssertionLevelOn,
		        CFSTR("MacShell IdleInhibitor"),
		        &newAssertion
		    )
		    == kIOReturnSuccess)
		{
			this->assertionId = newAssertion;
		}
	} else if (!shouldInhibit && this->assertionId != 0) {
		IOPMAssertionRelease(this->assertionId);
		this->assertionId = 0;
	}
}

}

namespace qs::wayland::idle_notify {

IdleMonitor::IdleMonitor(QObject* parent): QObject(parent) {
	QObject::connect(&this->mPollTimer, &QTimer::timeout, this, &IdleMonitor::evaluateIdleState);
	this->mPollTimer.start(IDLE_POLL_INTERVAL_MILLISECONDS);
}

void IdleMonitor::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;
	emit this->enabledChanged();

	if (enabled) {
		this->mPollTimer.start(IDLE_POLL_INTERVAL_MILLISECONDS);
	} else {
		this->mPollTimer.stop();
		this->bIsIdle = false;
	}
}

void IdleMonitor::evaluateIdleState() {
	auto timeout = this->bTimeout.value();
	if (timeout <= 0) {
		this->bIsIdle = false;
		return;
	}

	auto pastTimeout = secondsSinceLastInput() >= timeout;
	auto inhibited = this->bRespectInhibitors.value() && isAnyDisplaySleepAssertionActive();
	this->bIsIdle = pastTimeout && !inhibited;
}

}

#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::service::upower {

class PowerProfile: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		PowerSaver = 0,
		Balanced = 1,
		Performance = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::upower::PowerProfile::Enum profile);
};

class PerformanceDegradationReason: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		LapDetected = 1,
		HighTemperature = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::upower::PerformanceDegradationReason::Enum reason);
};

class PowerProfileHold {
	Q_GADGET;
	QML_VALUE_TYPE(powerProfileHold);
	Q_PROPERTY(qs::service::upower::PowerProfile::Enum profile MEMBER profile CONSTANT);
	Q_PROPERTY(QString applicationId MEMBER applicationId CONSTANT);
	Q_PROPERTY(QString reason MEMBER reason CONSTANT);

public:
	PowerProfile::Enum profile = PowerProfile::Balanced;
	QString applicationId;
	QString reason;

	[[nodiscard]] bool operator==(const PowerProfileHold& other) const = default;
};

class PowerProfiles: public QObject {
	Q_OBJECT;

public:
	static PowerProfiles* instance();

	void setProfile(PowerProfile::Enum profile);
	void refreshFromSystem();

	[[nodiscard]] QBindable<PowerProfile::Enum> bindableProfile() const { return &this->bProfile; }

signals:
	void profileChanged();

private:
	PowerProfiles();

	Q_OBJECT_BINDABLE_PROPERTY(PowerProfiles, PowerProfile::Enum, bProfile, &PowerProfiles::profileChanged);
};

class PowerProfilesQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(PowerProfiles);
	QML_SINGLETON;
	Q_PROPERTY(qs::service::upower::PowerProfile::Enum profile READ default WRITE setProfile NOTIFY profileChanged BINDABLE bindableProfile);
	Q_PROPERTY(bool hasPerformanceProfile READ default NOTIFY hasPerformanceProfileChanged BINDABLE bindableHasPerformanceProfile);
	Q_PROPERTY(qs::service::upower::PerformanceDegradationReason::Enum degradationReason READ default NOTIFY degradationReasonChanged BINDABLE bindableDegradationReason);
	Q_PROPERTY(QList<qs::service::upower::PowerProfileHold> holds READ default NOTIFY holdsChanged BINDABLE bindableHolds);

public:
	explicit PowerProfilesQml(QObject* parent = nullptr);

	[[nodiscard]] static QBindable<PowerProfile::Enum> bindableProfile() {
		return PowerProfiles::instance()->bindableProfile();
	}

	static void setProfile(PowerProfile::Enum profile) {
		PowerProfiles::instance()->setProfile(profile);
	}

	[[nodiscard]] QBindable<bool> bindableHasPerformanceProfile() const {
		return &this->bHasPerformanceProfile;
	}

	[[nodiscard]] QBindable<PerformanceDegradationReason::Enum> bindableDegradationReason() const {
		return &this->bDegradationReason;
	}

	[[nodiscard]] QBindable<QList<PowerProfileHold>> bindableHolds() const { return &this->bHolds; }

signals:
	void profileChanged();
	void hasPerformanceProfileChanged();
	void degradationReasonChanged();
	void holdsChanged();

private:
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, bool, bHasPerformanceProfile, &PowerProfilesQml::hasPerformanceProfileChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, PerformanceDegradationReason::Enum, bDegradationReason, &PowerProfilesQml::degradationReasonChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, QList<PowerProfileHold>, bHolds, &PowerProfilesQml::holdsChanged);
};

}

#pragma once

#include <sys/types.h>

#include <qlist.h>
#include <qmargins.h>
#include <qobject.h>
#include <qrect.h>
#include <qtimer.h>

namespace qs::macos {

struct ScreenReservation {
	QRect screenGeometry;
	QMargins reservedInsets;
};

class ExclusiveZoneEnforcer: public QObject {
	Q_OBJECT;

public:
	static ExclusiveZoneEnforcer* instance();

	void setReservations(const QList<ScreenReservation>& reservations);
	[[nodiscard]] QRect usableGeometry(const QRect& screenGeometry) const;

private:
	ExclusiveZoneEnforcer();

	void enforce();
	static void enforceReservations(const QList<ScreenReservation>& reservations, pid_t ownProcessId);

	QList<ScreenReservation> mReservations;
	QTimer mEnforceTimer;
	bool mRequestedAccessibility = false;
};

}

#include "nm_settings.hpp"
#include <utility>

#include <qlogging.h>
#include <qstring.h>
#include <qvariant.h>

namespace qs::network {

NMSettings::NMSettings(QString identifier, QObject* parent)
    : QObject(parent)
    , mIdentifier(std::move(identifier)) {}

void NMSettings::forget() { emit this->forgetRequested(); }

void NMSettings::write([[maybe_unused]] const QVariantMap& settings) {
	qWarning() << "Writing connection settings is not supported on macOS.";
}

QVariantMap NMSettings::read() const {
	return {{QStringLiteral("connection"), QVariantMap {{QStringLiteral("id"), this->mIdentifier}}}};
}

}

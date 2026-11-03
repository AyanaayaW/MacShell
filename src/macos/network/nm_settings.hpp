#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::network {

class NMSettings: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("");
	Q_PROPERTY(QString id READ id NOTIFY idChanged);
	Q_PROPERTY(QString uuid READ uuid NOTIFY uuidChanged);

public:
	NMSettings(QString identifier, QObject* parent);

	Q_INVOKABLE static void clearSecrets() {}
	Q_INVOKABLE void forget();
	Q_INVOKABLE static void write(const QVariantMap& settings);
	Q_INVOKABLE [[nodiscard]] QVariantMap read() const;

	[[nodiscard]] QString id() const { return this->mIdentifier; }
	[[nodiscard]] QString uuid() const { return this->mIdentifier; }

signals:
	void idChanged(QString id);
	void uuidChanged(QString uuid);
	void forgetRequested();

private:
	QString mIdentifier;
};

}

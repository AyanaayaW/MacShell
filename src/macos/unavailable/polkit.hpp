#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

namespace qs::service::polkit {

class Identity: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("Identities cannot be created directly.");
};

class AuthFlow: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("AuthFlow can only be obtained from PolkitAgent.");
};

class PolkitAgent: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(QString path READ path WRITE setPath);
	Q_PROPERTY(bool isRegistered READ isRegistered NOTIFY isRegisteredChanged);
	Q_PROPERTY(bool isActive READ isActive NOTIFY isActiveChanged);
	Q_PROPERTY(qs::service::polkit::AuthFlow* flow READ flow NOTIFY flowChanged);

public:
	explicit PolkitAgent(QObject* parent = nullptr);

	[[nodiscard]] QString path() const { return this->mPath; }
	void setPath(const QString& path) { this->mPath = path; }

	[[nodiscard]] static bool isRegistered() { return false; }
	[[nodiscard]] static bool isActive() { return false; }
	[[nodiscard]] static AuthFlow* flow() { return nullptr; }

signals:
	void authenticationRequestStarted();
	void isRegisteredChanged();
	void isActiveChanged();
	void flowChanged();

private:
	QString mPath;
};

}

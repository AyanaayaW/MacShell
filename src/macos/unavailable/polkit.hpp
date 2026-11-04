#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::service::polkit {

class Identity: public QObject {
	Q_OBJECT;
	Q_PROPERTY(quint32 id READ id CONSTANT);
	Q_PROPERTY(QString string READ name CONSTANT);
	Q_PROPERTY(QString displayName READ displayName CONSTANT);
	Q_PROPERTY(bool isGroup READ isGroup CONSTANT);
	QML_ELEMENT;
	QML_UNCREATABLE("Identities cannot be created directly.");

public:
	[[nodiscard]] static quint32 id() { return 0; }
	[[nodiscard]] static QString name() { return {}; }
	[[nodiscard]] static QString displayName() { return {}; }
	[[nodiscard]] static bool isGroup() { return false; }
};

class AuthFlow: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString message READ message CONSTANT);
	Q_PROPERTY(QString iconName READ iconName CONSTANT);
	Q_PROPERTY(QString actionId READ actionId CONSTANT);
	Q_PROPERTY(QString cookie READ cookie CONSTANT);
	Q_PROPERTY(QList<qs::service::polkit::Identity*> identities READ identities CONSTANT);
	Q_PROPERTY(qs::service::polkit::Identity* selectedIdentity READ selectedIdentity WRITE setSelectedIdentity NOTIFY selectedIdentityChanged);
	Q_PROPERTY(bool isResponseRequired READ isResponseRequired NOTIFY isResponseRequiredChanged);
	Q_PROPERTY(QString inputPrompt READ inputPrompt NOTIFY inputPromptChanged);
	Q_PROPERTY(bool responseVisible READ responseVisible NOTIFY responseVisibleChanged);
	Q_PROPERTY(QString supplementaryMessage READ supplementaryMessage NOTIFY supplementaryMessageChanged);
	Q_PROPERTY(bool supplementaryIsError READ supplementaryIsError NOTIFY supplementaryIsErrorChanged);
	Q_PROPERTY(bool isCompleted READ isCompleted NOTIFY isCompletedChanged);
	Q_PROPERTY(bool isSuccessful READ isSuccessful NOTIFY isSuccessfulChanged);
	Q_PROPERTY(bool isCancelled READ isCancelled NOTIFY isCancelledChanged);
	Q_PROPERTY(bool failed READ failed NOTIFY failedChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("AuthFlow can only be obtained from PolkitAgent.");

public:
	Q_INVOKABLE static void submit([[maybe_unused]] const QString& value) {}
	Q_INVOKABLE static void cancelAuthenticationRequest() {}

	[[nodiscard]] static QString message() { return {}; }
	[[nodiscard]] static QString iconName() { return {}; }
	[[nodiscard]] static QString actionId() { return {}; }
	[[nodiscard]] static QString cookie() { return {}; }
	[[nodiscard]] static QList<Identity*> identities() { return {}; }
	[[nodiscard]] static Identity* selectedIdentity() { return nullptr; }
	static void setSelectedIdentity([[maybe_unused]] Identity* identity) {}
	[[nodiscard]] static bool isResponseRequired() { return false; }
	[[nodiscard]] static QString inputPrompt() { return {}; }
	[[nodiscard]] static bool responseVisible() { return false; }
	[[nodiscard]] static QString supplementaryMessage() { return {}; }
	[[nodiscard]] static bool supplementaryIsError() { return false; }
	[[nodiscard]] static bool isCompleted() { return false; }
	[[nodiscard]] static bool isSuccessful() { return false; }
	[[nodiscard]] static bool isCancelled() { return false; }
	[[nodiscard]] static bool failed() { return false; }

signals:
	void authenticationSucceeded();
	void authenticationFailed();
	void authenticationRequestCancelled();
	void selectedIdentityChanged();
	void isResponseRequiredChanged();
	void inputPromptChanged();
	void responseVisibleChanged();
	void supplementaryMessageChanged();
	void supplementaryIsErrorChanged();
	void isCompletedChanged();
	void isSuccessfulChanged();
	void isCancelledChanged();
	void failedChanged();
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

#pragma once

#include <optional>

#include <qfilesystemwatcher.h>
#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::hyprland::global_shortcuts {

struct KeyCombination {
	quint32 keyCode = 0;
	quint32 modifiers = 0;

	[[nodiscard]] bool operator==(const KeyCombination& other) const = default;
};

std::optional<KeyCombination> parseKeyCombination(const QString& modifiers, const QString& key);
std::optional<KeyCombination> parseTriggerDescription(const QString& description);

struct ShortcutBinding {
	KeyCombination keys;
	QString dispatcher;
	QString argument;
	bool triggersOnRelease = false;
};

void setSuperModifier(quint32 carbonModifier);
void runHyprlandAutostart();

class HotKeyRegistry: public QObject {
	Q_OBJECT;

public:
	static HotKeyRegistry* instance();

	quint32 registerCombination(const KeyCombination& keys);
	void unregisterCombination(quint32 hotKeyId);
	[[nodiscard]] bool hasGlobalBinding(const QString& qualifiedName) const;

	void handleHotKey(quint32 hotKeyId, bool pressed);

signals:
	void globalShortcutTriggered(const QString& qualifiedName, bool pressed);
	void combinationTriggered(quint32 hotKeyId, bool pressed);

private:
	HotKeyRegistry();

	void reloadBindings();
	void watchBindingFiles();

	QHash<quint32, ShortcutBinding> mFileBindings;
	QHash<quint32, void*> mHotKeyReferences;
	quint32 mNextHotKeyId = 1;
	QFileSystemWatcher mFileWatcher;
};

class GlobalShortcut: public QObject {
	Q_OBJECT;
	Q_PROPERTY(bool pressed READ isPressed NOTIFY pressedChanged);
	Q_PROPERTY(QString appid READ appid WRITE setAppid NOTIFY appidChanged);
	Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged);
	Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged);
	Q_PROPERTY(QString triggerDescription READ triggerDescription WRITE setTriggerDescription NOTIFY triggerDescriptionChanged);
	QML_ELEMENT;

public:
	explicit GlobalShortcut(QObject* parent = nullptr);
	~GlobalShortcut() override;
	Q_DISABLE_COPY_MOVE(GlobalShortcut);

	[[nodiscard]] bool isPressed() const { return this->mPressed; }
	[[nodiscard]] QString appid() const { return this->mAppid; }
	void setAppid(const QString& appid);
	[[nodiscard]] QString name() const { return this->mName; }
	void setName(const QString& name);
	[[nodiscard]] QString description() const { return this->mDescription; }
	void setDescription(const QString& description);
	[[nodiscard]] QString triggerDescription() const { return this->mTriggerDescription; }
	void setTriggerDescription(const QString& triggerDescription);

signals:
	void pressed();
	void released();
	void pressedChanged();
	void appidChanged();
	void nameChanged();
	void descriptionChanged();
	void triggerDescriptionChanged();

private:
	void setPressedState(bool pressed);
	void updateOwnHotKey();
	[[nodiscard]] QString qualifiedName() const { return this->mAppid + u':' + this->mName; }

	bool mPressed = false;
	QString mAppid = QStringLiteral("quickshell");
	QString mName;
	QString mDescription;
	QString mTriggerDescription;
	quint32 mOwnHotKeyId = 0;
};

}

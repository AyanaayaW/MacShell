#pragma once

#include "../background_reader.hpp"
#include <qhash.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/model.hpp"
#include "../../core/qsmenu.hpp"

namespace qs::service::sni {

namespace Status {
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Passive = 0,
	Active = 1,
	NeedsAttention = 2,
};
Q_ENUM_NS(Enum);
}

namespace Category {
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Hardware = 0,
	SystemServices = 1,
	ApplicationStatus = 2,
	Communications = 3,
};
Q_ENUM_NS(Enum);
}

struct MenuExtraInfo {
	QString identifier;
	qint32 processId = 0;
	qint32 indexInApplication = 0;
	QString bundleIdentifier;
	QString applicationName;
	QString title;
	QString help;
};

class StatusNotifierItem: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString id READ default NOTIFY idChanged BINDABLE bindableId);
	Q_PROPERTY(QString title READ default NOTIFY titleChanged BINDABLE bindableTitle);
	Q_PROPERTY(qs::service::sni::Status::Enum status READ default NOTIFY statusChanged BINDABLE bindableStatus);
	Q_PROPERTY(qs::service::sni::Category::Enum category READ default NOTIFY categoryChanged BINDABLE bindableCategory);
	Q_PROPERTY(QString icon READ default NOTIFY iconChanged BINDABLE bindableIcon);
	Q_PROPERTY(QString tooltipTitle READ tooltipTitle NOTIFY tooltipTitleChanged);
	Q_PROPERTY(QString tooltipDescription READ tooltipDescription NOTIFY tooltipDescriptionChanged);
	Q_PROPERTY(bool hasMenu READ default NOTIFY hasMenuChanged BINDABLE bindableHasMenu);
	Q_PROPERTY(qs::menu::QsMenuHandle* menu READ menuHandle NOTIFY hasMenuChanged);
	Q_PROPERTY(bool onlyMenu READ default NOTIFY onlyMenuChanged BINDABLE bindableOnlyMenu);
	QML_NAMED_ELEMENT(SystemTrayItem);
	QML_UNCREATABLE("SystemTrayItems can only be acquired from SystemTray");

public:
	explicit StatusNotifierItem(QObject* parent): QObject(parent) {}

	void applyInfo(const MenuExtraInfo& info);

	Q_INVOKABLE void activate();
	Q_INVOKABLE void secondaryActivate();
	Q_INVOKABLE static void scroll(qint32 delta, bool horizontal);
	Q_INVOKABLE void display(QObject* parentWindow, qint32 relativeX, qint32 relativeY);

	[[nodiscard]] QString tooltipTitle() const { return this->mTooltipTitle; }
	[[nodiscard]] QString tooltipDescription() const { return this->mTooltipDescription; }
	[[nodiscard]] static qs::menu::QsMenuHandle* menuHandle() { return nullptr; }

	[[nodiscard]] QBindable<QString> bindableId() { return &this->bId; }
	[[nodiscard]] QBindable<QString> bindableTitle() { return &this->bTitle; }
	[[nodiscard]] QBindable<Status::Enum> bindableStatus() { return &this->bStatus; }
	[[nodiscard]] QBindable<Category::Enum> bindableCategory() { return &this->bCategory; }
	[[nodiscard]] QBindable<QString> bindableIcon() { return &this->bIcon; }
	[[nodiscard]] QBindable<bool> bindableHasMenu() { return &this->bHasMenu; }
	[[nodiscard]] QBindable<bool> bindableOnlyMenu() { return &this->bOnlyMenu; }

signals:
	void idChanged();
	void titleChanged();
	void iconChanged();
	void statusChanged();
	void categoryChanged();
	void tooltipTitleChanged();
	void tooltipDescriptionChanged();
	void hasMenuChanged();
	void onlyMenuChanged();

private:
	MenuExtraInfo mInfo;
	QString mTooltipTitle;
	QString mTooltipDescription;

	Q_OBJECT_BINDABLE_PROPERTY(StatusNotifierItem, QString, bId, &StatusNotifierItem::idChanged);
	Q_OBJECT_BINDABLE_PROPERTY(StatusNotifierItem, QString, bTitle, &StatusNotifierItem::titleChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(StatusNotifierItem, Status::Enum, bStatus, Status::Active, &StatusNotifierItem::statusChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(StatusNotifierItem, Category::Enum, bCategory, Category::ApplicationStatus, &StatusNotifierItem::categoryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(StatusNotifierItem, QString, bIcon, &StatusNotifierItem::iconChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(StatusNotifierItem, bool, bHasMenu, true, &StatusNotifierItem::hasMenuChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(StatusNotifierItem, bool, bOnlyMenu, true, &StatusNotifierItem::onlyMenuChanged);
};

class SystemTrayHost: public QObject {
	Q_OBJECT;

public:
	static SystemTrayHost* instance();

	[[nodiscard]] ObjectModel<StatusNotifierItem>* items() { return &this->mItems; }
	void scheduleRefresh();

private:
	SystemTrayHost();

	void refresh();
	void applyMenuExtras(const QList<MenuExtraInfo>& extras);

	ObjectModel<StatusNotifierItem> mItems {this};
	qs::macos::CoalescingBackgroundReader mReader {"MacShell.SystemTray"};
	QHash<QString, StatusNotifierItem*> mItemsByIdentifier;
	QTimer mPollTimer;
	QTimer mRefreshDebounce;
	void* applicationObserver = nullptr;
};

class SystemTray: public QObject {
	Q_OBJECT;
	Q_PROPERTY(UntypedObjectModel* items READ items CONSTANT);
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit SystemTray(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static ObjectModel<StatusNotifierItem>* items() { return SystemTrayHost::instance()->items(); }
};

}

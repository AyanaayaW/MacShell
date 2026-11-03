#include <mutex>

#include <qcoreapplication.h>
#include <qguiapplication.h>
#include <qlist.h>
#include <qmetaobject.h>
#include <qqml.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>

#include "../core/desktopentry.hpp"
#include "../core/iconimageprovider.hpp"
#include "../core/plugin.hpp"
#include "applications.hpp"
#include "cocoa.hpp"
#include "icons.hpp"
#include "panel_window.hpp"
#include "settings.hpp"

namespace {

class MacPlugin: public QsEnginePlugin {
	QList<QString> dependencies() override { return {"window"}; }

	bool applies() override { return QGuiApplication::platformName() == "cocoa"; }

	void init() override {
		qs::macos::setAccessoryApp();
		IconImageProvider::setPlatformIconResolver(qs::macos::resolveIcon);
		DesktopEntryManager::setPlatformEntryScanner([]() {
			static std::once_flag watcherStarted;
			std::call_once(watcherStarted, []() {
				QMetaObject::invokeMethod(
				    qApp,
				    &qs::macos::watchApplicationDirectories,
				    Qt::QueuedConnection
				);
			});
			return qs::macos::scanInstalledApplications();
		});

		auto settings = qs::macos::readSettings();
		qs::macos::applyLaunchAtLogin(settings.launchAtLogin);

		if (settings.hideSystemUi) {
			qs::macos::hideSystemUi();
			QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
				qs::macos::restoreSystemUi();
			});
		} else {
			qs::macos::restoreSystemUi();
		}
	}

	void registerTypes() override {
		qmlRegisterType<MacPanelInterface>("Quickshell._MacOverlay", 1, 0, "PanelWindow");

		qmlRegisterModuleImport(
		    "Quickshell",
		    QQmlModuleImportModuleAny,
		    "Quickshell._MacOverlay",
		    QQmlModuleImportLatest
		);
	}
};

QS_REGISTER_PLUGIN(MacPlugin);

}

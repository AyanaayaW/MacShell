#include <mutex>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qguiapplication.h>
#include <qlist.h>
#include <qmetaobject.h>
#include <qqml.h>
#include <qqmlengine.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>

#include "../core/desktopentry.hpp"
#include "../core/generation.hpp"
#include "../core/iconimageprovider.hpp"
#include "../core/plugin.hpp"
#include "../io/fileview.hpp"
#include "../window/proxywindow.hpp"
#include "applications.hpp"
#include "cocoa.hpp"
#include "hyprland/global_shortcuts.hpp"
#include "hyprland/hyprctl.hpp"
#include "hyprland/keyboard_layouts.hpp"
#include "icons.hpp"
#include "input_region.hpp"
#include "linux_files.hpp"
#include "panel_window.hpp"
#include "settings.hpp"

namespace {

class MacPlugin: public QsEnginePlugin {
	QList<QString> dependencies() override { return {"window"}; }

	bool applies() override { return QGuiApplication::platformName() == "cocoa"; }

	void init() override {
		qs::macos::setAccessoryApp();
		IconImageProvider::setPlatformIconResolver(qs::macos::resolveIcon);
		qs::io::FileViewReader::setVirtualFileProvider([](const QString& path) -> std::optional<QByteArray> {
			if (path.startsWith(QStringLiteral("/usr/share/X11/xkb/rules/"))
			    && (path.endsWith(QStringLiteral("base.lst")) || path.endsWith(QStringLiteral("evdev.lst"))))
			{
				return qs::hyprland::macos::xkbRulesListing();
			}
			return qs::macos::linuxPseudoFile(path);
		});
		QsQuickWindowBase::setPlatformInputRegionHandler(qs::macos::applyInputRegion);
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

		qs::hyprland::ipc::startHyprctlServer();

		auto settings = qs::macos::readSettings();
		qs::hyprland::global_shortcuts::setSuperModifier(qs::macos::carbonModifierNamed(settings.superKey));
		qs::hyprland::global_shortcuts::HotKeyRegistry::instance();
		if (settings.hyprlandAutostart) qs::hyprland::global_shortcuts::runHyprlandAutostart();
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

	void constructGeneration(EngineGeneration& generation) override {
		generation.engine->addImportPath(QDir::homePath() + "/Library/Application Support/MacShell/qml");
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

#include "window_manager.hpp"

#include <algorithm>

#include <qguiapplication.h>
#include <qmetaobject.h>
#include <qscreen.h>

namespace qs::hyprland::macos {

SpaceWindowset::SpaceWindowset(ipc::HyprlandWorkspace* workspace, SpacesWindowManager* manager)
    : qs::wm::Windowset(manager)
    , mWorkspace(workspace) {
	this->bId.setBinding([workspace]() { return QString::number(workspace->bindableId().value()); });
	this->bName.setBinding([workspace]() { return workspace->bindableName().value(); });
	this->bCoordinates.setBinding([workspace]() { return QList<qint32> {workspace->bindableId().value()}; });
	this->bActive.setBinding([workspace]() { return workspace->bindableActive().value(); });
	this->bUrgent.setBinding([workspace]() { return workspace->bindableUrgent().value(); });
	this->bProjection.setBinding([workspace, manager]() -> qs::wm::WindowsetProjection* {
		auto* monitor = workspace->bindableMonitor().value();
		QScreen* matchingScreen = QGuiApplication::primaryScreen();
		if (monitor) {
			auto monitorName = monitor->bindableName().value();
			for (auto* screen: QGuiApplication::screens()) {
				if (screen->name() == monitorName) matchingScreen = screen;
			}
		}
		return matchingScreen ? manager->projectionForScreen(matchingScreen) : nullptr;
	});
	this->bShouldDisplay = true;
	this->bCanActivate = true;
}

void SpaceWindowset::activate() {
	if (this->mWorkspace) ipc::HyprlandState::instance()->activateWorkspace(this->mWorkspace);
}

SpacesWindowManager::SpacesWindowManager() {
	auto* workspaces = ipc::HyprlandState::instance()->workspaces();
	QObject::connect(workspaces, &UntypedObjectModel::objectInsertedPost, this, &SpacesWindowManager::synchronizeWindowsets);
	QObject::connect(workspaces, &UntypedObjectModel::objectRemovedPost, this, &SpacesWindowManager::synchronizeWindowsets);
}

SpacesWindowManager* SpacesWindowManager::instance() {
	static auto* manager = [] {
		auto* created = new SpacesWindowManager();
		QMetaObject::invokeMethod(created, &SpacesWindowManager::synchronizeWindowsets, Qt::QueuedConnection);
		return created;
	}();
	return manager;
}

qs::wm::ScreenProjection* SpacesWindowManager::projectionForScreen(QScreen* screen) {
	if (auto* existing = this->mProjections.value(screen)) return existing;
	auto* projection = new qs::wm::ScreenProjection(screen, this);
	this->mProjections.insert(screen, projection);
	QObject::connect(screen, &QObject::destroyed, this, [this, screen]() { this->mProjections.remove(screen); });
	return projection;
}

void SpacesWindowManager::synchronizeWindowsets() {
	const auto& workspaces = ipc::HyprlandState::instance()->workspaces()->valueList();

	for (auto iterator = this->mWindowsets.begin(); iterator != this->mWindowsets.end();) {
		if (!workspaces.contains(iterator.key())) {
			iterator.value()->deleteLater();
			iterator = this->mWindowsets.erase(iterator);
		} else {
			++iterator;
		}
	}

	QList<qs::wm::Windowset*> orderedWindowsets;
	auto sortedWorkspaces = workspaces;
	std::ranges::sort(sortedWorkspaces, {}, [](ipc::HyprlandWorkspace* workspace) {
		return workspace->bindableId().value();
	});
	for (auto* workspace: sortedWorkspaces) {
		auto* windowset = this->mWindowsets.value(workspace);
		if (!windowset) {
			windowset = new SpaceWindowset(workspace, this);
			this->mWindowsets.insert(workspace, windowset);
		}
		orderedWindowsets << windowset;
	}
	this->bWindowsets = orderedWindowsets;
}

void installSpacesWindowManager() {
	qs::wm::WindowManager::setProvider([]() -> qs::wm::WindowManager* { return SpacesWindowManager::instance(); });
}

}

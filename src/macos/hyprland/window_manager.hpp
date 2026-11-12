#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qpointer.h>

#include "../../windowmanager/screenprojection.hpp"
#include "../../windowmanager/windowmanager.hpp"
#include "../../windowmanager/windowset.hpp"
#include "hyprland.hpp"

class QScreen;

namespace qs::hyprland::macos {

class SpacesWindowManager;

class SpaceWindowset: public qs::wm::Windowset {
	Q_OBJECT;

public:
	SpaceWindowset(ipc::HyprlandWorkspace* workspace, SpacesWindowManager* manager);

	void activate() override;

private:
	QPointer<ipc::HyprlandWorkspace> mWorkspace;
};

class SpacesWindowManager: public qs::wm::WindowManager {
	Q_OBJECT;

public:
	static SpacesWindowManager* instance();

	qs::wm::ScreenProjection* projectionForScreen(QScreen* screen);

private:
	SpacesWindowManager();
	void synchronizeWindowsets();

	QHash<ipc::HyprlandWorkspace*, SpaceWindowset*> mWindowsets;
	QHash<QScreen*, qs::wm::ScreenProjection*> mProjections;
};

void installSpacesWindowManager();

}

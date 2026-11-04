#include "hyprland.hpp"
#include <algorithm>
#include <memory>
#include <utility>

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../toplevels.hpp"
#include "spaces_backend.hpp"

namespace qs::hyprland::ipc {

namespace {

Q_LOGGING_CATEGORY(logHyprland, "quickshell.hyprland.macos", QtWarningMsg);

constexpr auto TOPLEVEL_REFRESH_INTERVAL_MILLISECONDS = 1500;
const QString SHARED_SPACES_DISPLAY_IDENTIFIER = QStringLiteral("Main");

QVariantMap workspaceReference(HyprlandWorkspace* workspace) {
	if (!workspace) return {{QStringLiteral("id"), -1}, {QStringLiteral("name"), QString()}};
	return {
	    {QStringLiteral("id"), workspace->bindableId().value()},
	    {QStringLiteral("name"), workspace->bindableName().value()},
	};
}

template <typename Object>
void synchronizeModel(ObjectModel<Object>& model, const QList<Object*>& desiredObjects) {
	for (auto index = model.valueList().size() - 1; index >= 0; --index) {
		if (!desiredObjects.contains(model.valueList().at(index))) model.removeAt(index);
	}

	for (auto* object: desiredObjects) {
		if (!model.valueList().contains(object)) model.insertObject(object);
	}
}

}

HyprlandIpcEvent::HyprlandIpcEvent(QString name, QString data, QObject* parent)
    : QObject(parent)
    , mName(std::move(name))
    , mData(std::move(data)) {}

QVector<QString> HyprlandIpcEvent::parse(qint32 argumentCount) const {
	QVector<QString> arguments;
	if (argumentCount <= 0) return arguments;

	auto remaining = QStringView(this->mData);
	for (qint32 argumentIndex = 0; argumentIndex < argumentCount - 1; ++argumentIndex) {
		auto separatorIndex = remaining.indexOf(u',');
		if (separatorIndex == -1) break;
		arguments.append(remaining.first(separatorIndex).toString());
		remaining = remaining.sliced(separatorIndex + 1);
	}
	arguments.append(remaining.toString());
	return arguments;
}

void HyprlandMonitor::update(qint32 id, const macos::ScreenDescription& screen) {
	this->mDisplayIdentifier = screen.displayIdentifier;

	Qt::beginPropertyUpdateGroup();
	this->bId = id;
	this->bName = screen.name;
	this->bDescription = screen.name;
	this->bX = screen.frame.x();
	this->bY = screen.frame.y();
	this->bWidth = static_cast<qint32>(screen.frame.width() * screen.scale);
	this->bHeight = static_cast<qint32>(screen.frame.height() * screen.scale);
	this->bScale = screen.scale;
	Qt::endPropertyUpdateGroup();

	this->rebuildIpcObject();
}

void HyprlandMonitor::setActiveWorkspace(HyprlandWorkspace* workspace) {
	this->bActiveWorkspace = workspace;
	this->rebuildIpcObject();
}

void HyprlandMonitor::rebuildIpcObject() {
	QVariantMap ipcObject = {
	    {QStringLiteral("id"), this->bId.value()},
	    {QStringLiteral("name"), this->bName.value()},
	    {QStringLiteral("description"), this->bDescription.value()},
	    {QStringLiteral("make"), QStringLiteral("Apple")},
	    {QStringLiteral("model"), this->bName.value()},
	    {QStringLiteral("x"), this->bX.value()},
	    {QStringLiteral("y"), this->bY.value()},
	    {QStringLiteral("width"), this->bWidth.value()},
	    {QStringLiteral("height"), this->bHeight.value()},
	    {QStringLiteral("scale"), this->bScale.value()},
	    {QStringLiteral("focused"), this->bFocused.value()},
	    {QStringLiteral("activeWorkspace"), workspaceReference(this->bActiveWorkspace.value())},
	    {QStringLiteral("specialWorkspace"), QVariantMap {{QStringLiteral("id"), 0}, {QStringLiteral("name"), QString()}}},
	    {QStringLiteral("refreshRate"), 60.0},
	    {QStringLiteral("transform"), 0},
	    {QStringLiteral("disabled"), false},
	    {QStringLiteral("dpmsStatus"), true},
	    {QStringLiteral("vrr"), false},
	    {QStringLiteral("activelyTearing"), false},
	    {QStringLiteral("reserved"), QVariantList {0, 0, 0, 0}},
	};
	if (ipcObject == this->mLastIpcObject) return;
	this->mLastIpcObject = ipcObject;
	emit this->lastIpcObjectChanged();
}

void HyprlandWorkspace::update(
    qint32 id,
    quint64 spaceId,
    int positionOnDisplay,
    HyprlandMonitor* monitor
) {
	this->mSpaceId = spaceId;
	this->mPositionOnDisplay = positionOnDisplay;

	Qt::beginPropertyUpdateGroup();
	this->bId = id;
	this->bName = QString::number(id);
	this->bAddress = QStringLiteral("0x%1").arg(spaceId, 0, 16);
	this->bMonitor = monitor;
	Qt::endPropertyUpdateGroup();
}

void HyprlandWorkspace::rebuildIpcObject(int windowCount) {
	auto* monitor = this->bMonitor.value();
	QVariantMap ipcObject = {
	    {QStringLiteral("id"), this->bId.value()},
	    {QStringLiteral("name"), this->bName.value()},
	    {QStringLiteral("monitor"), monitor ? monitor->bindableName().value() : QString()},
	    {QStringLiteral("monitorID"), monitor ? monitor->bindableId().value() : -1},
	    {QStringLiteral("windows"), windowCount},
	    {QStringLiteral("hasfullscreen"), this->bHasFullscreen.value()},
	    {QStringLiteral("ispersistent"), false},
	};
	if (ipcObject == this->mLastIpcObject) return;
	this->mLastIpcObject = ipcObject;
	emit this->lastIpcObjectChanged();
}

void HyprlandWorkspace::activate() { HyprlandState::instance()->activateWorkspace(this); }

HyprlandToplevel::HyprlandToplevel(quint32 windowId, QObject* parent)
    : QObject(parent)
    , mWindowId(windowId) {}

HyprlandToplevel* HyprlandToplevel::qmlAttachedProperties(QObject* object) {
	if (auto* toplevel = qobject_cast<qs::wayland::toplevel::Toplevel*>(object)) {
		return HyprlandState::instance()->toplevelForWindow(toplevel->windowId());
	}
	return qobject_cast<HyprlandToplevel*>(object);
}

QString HyprlandToplevel::address() const { return QString::number(this->mWindowId, 16); }

void HyprlandToplevel::update(
    const macos::WindowDescription& window,
    HyprlandWorkspace* workspace,
    bool activated
) {
	auto* monitor = workspace ? workspace->bindableMonitor().value() : nullptr;
	auto applicationClass =
	    window.bundleIdentifier.isEmpty() ? window.applicationName : window.bundleIdentifier;
	auto title = window.title.isEmpty() ? window.applicationName : window.title;

	Qt::beginPropertyUpdateGroup();
	this->bTitle = title;
	this->bActivated = activated;
	this->bWorkspace = workspace;
	this->bMonitor = monitor;
	this->bLastIpcObject = QVariantMap {
	    {QStringLiteral("address"), QStringLiteral("0x") + this->address()},
	    {QStringLiteral("title"), title},
	    {QStringLiteral("initialTitle"), title},
	    {QStringLiteral("class"), applicationClass},
	    {QStringLiteral("initialClass"), applicationClass},
	    {QStringLiteral("pid"), window.processId},
	    {QStringLiteral("workspace"), workspaceReference(workspace)},
	    {QStringLiteral("monitor"), monitor ? monitor->bindableId().value() : -1},
	    {QStringLiteral("at"), QVariantList {window.frame.x(), window.frame.y()}},
	    {QStringLiteral("size"), QVariantList {window.frame.width(), window.frame.height()}},
	    {QStringLiteral("floating"), false},
	    {QStringLiteral("fullscreen"), 0},
	    {QStringLiteral("mapped"), true},
	    {QStringLiteral("hidden"), false},
	};
	Qt::endPropertyUpdateGroup();
}

HyprlandState* HyprlandState::instance() {
	static auto* sharedInstance = new HyprlandState();
	return sharedInstance;
}

HyprlandState::HyprlandState() {
	this->mSystemEvents = std::make_unique<macos::SystemEventObserver>(
	    [this]() { this->refreshEverything(); },
	    [this]() { this->refreshToplevels(); }
	);

	QObject::connect(
	    &this->mToplevelRefreshTimer,
	    &QTimer::timeout,
	    this,
	    &HyprlandState::refreshToplevels
	);
	this->mToplevelRefreshTimer.start(TOPLEVEL_REFRESH_INTERVAL_MILLISECONDS);

	this->refreshEverything();
}

void HyprlandState::refreshEverything() {
	this->refreshMonitors();
	this->refreshWorkspaces();
	this->refreshToplevels();
}

void HyprlandState::emitRawEvent(const QString& name, const QString& data) {
	QMetaObject::invokeMethod(
	    this,
	    [this, name, data] {
		    auto* event = new HyprlandIpcEvent(name, data, this);
		    emit this->rawEvent(event);
		    event->deleteLater();
	    },
	    Qt::QueuedConnection
	);
}

void HyprlandState::refreshMonitors() {
	auto screens = macos::readScreens();
	QList<HyprlandMonitor*> currentMonitors;

	for (qsizetype screenIndex = 0; screenIndex < screens.size(); ++screenIndex) {
		const auto& screen = screens.at(screenIndex);

		auto* monitor = this->mMonitorsByDisplay.value(screen.displayIdentifier);
		if (!monitor) {
			monitor = new HyprlandMonitor(this);
			this->mMonitorsByDisplay.insert(screen.displayIdentifier, monitor);
			this->emitRawEvent(QStringLiteral("monitoradded"), screen.name);
		}

		monitor->update(static_cast<qint32>(screenIndex), screen);
		currentMonitors.append(monitor);
	}

	for (auto iterator = this->mMonitorsByDisplay.begin(); iterator != this->mMonitorsByDisplay.end();) {
		if (currentMonitors.contains(iterator.value())) {
			++iterator;
			continue;
		}

		auto* removedMonitor = iterator.value();
		this->emitRawEvent(QStringLiteral("monitorremoved"), removedMonitor->bindableName().value());
		iterator = this->mMonitorsByDisplay.erase(iterator);
		this->mMonitors.removeObject(removedMonitor);
		removedMonitor->deleteLater();
	}

	synchronizeModel(this->mMonitors, currentMonitors);
}

void HyprlandState::refreshWorkspaces() {
	auto displays = macos::readDisplaySpaces();
	auto focusedDisplay = macos::focusedDisplayIdentifier();

	auto monitorForDisplay = [&](const QString& displayIdentifier) -> HyprlandMonitor* {
		if (auto* monitor = this->mMonitorsByDisplay.value(displayIdentifier)) return monitor;
		if (auto* focusedMonitor = this->mMonitorsByDisplay.value(focusedDisplay)) return focusedMonitor;
		return this->mMonitors.valueList().isEmpty() ? nullptr : this->mMonitors.valueList().first();
	};

	auto* previousFocusedWorkspace = this->bFocusedWorkspace.value();
	auto* previousFocusedMonitor = this->bFocusedMonitor.value();
	auto sharedSpaces =
	    displays.size() == 1 && displays.first().displayIdentifier == SHARED_SPACES_DISPLAY_IDENTIFIER;

	QList<HyprlandWorkspace*> currentWorkspaces;
	HyprlandWorkspace* focusedWorkspace = nullptr;
	qint32 nextWorkspaceId = 1;

	for (const auto& display: displays) {
		auto* monitor = monitorForDisplay(display.displayIdentifier);
		HyprlandWorkspace* displayActiveWorkspace = nullptr;

		for (qsizetype position = 0; position < display.desktopSpaceIds.size(); ++position) {
			auto spaceId = display.desktopSpaceIds.at(position);
			auto workspaceId = nextWorkspaceId++;

			auto* workspace = this->mWorkspacesById.value(workspaceId);
			if (!workspace) {
				workspace = new HyprlandWorkspace(this);
				this->mWorkspacesById.insert(workspaceId, workspace);
				this->emitRawEvent(QStringLiteral("createworkspace"), QString::number(workspaceId));
			}

			workspace->update(workspaceId, spaceId, static_cast<int>(position), monitor);
			workspace->setActive(spaceId == display.currentSpaceId);
			if (spaceId == display.currentSpaceId) displayActiveWorkspace = workspace;
			currentWorkspaces.append(workspace);
		}

		if (monitor && displayActiveWorkspace) monitor->setActiveWorkspace(displayActiveWorkspace);

		auto displayIsFocused = sharedSpaces || display.displayIdentifier == focusedDisplay;
		if (displayIsFocused && !focusedWorkspace) focusedWorkspace = displayActiveWorkspace;
	}

	for (auto iterator = this->mWorkspacesById.begin(); iterator != this->mWorkspacesById.end();) {
		if (currentWorkspaces.contains(iterator.value())) {
			++iterator;
			continue;
		}

		auto* removedWorkspace = iterator.value();
		this->emitRawEvent(QStringLiteral("destroyworkspace"), QString::number(iterator.key()));
		iterator = this->mWorkspacesById.erase(iterator);
		this->mWorkspaces.removeObject(removedWorkspace);
		removedWorkspace->deleteLater();
	}

	std::ranges::sort(currentWorkspaces, [](HyprlandWorkspace* left, HyprlandWorkspace* right) {
		return left->bindableId().value() < right->bindableId().value();
	});
	synchronizeModel(this->mWorkspaces, currentWorkspaces);

	if (!focusedWorkspace) focusedWorkspace = previousFocusedWorkspace;
	auto* focusedMonitor = this->mMonitorsByDisplay.value(focusedDisplay);
	if (!focusedMonitor && focusedWorkspace) focusedMonitor = focusedWorkspace->bindableMonitor().value();

	for (auto* workspace: currentWorkspaces) workspace->setFocused(workspace == focusedWorkspace);
	for (auto* monitor: this->mMonitors.valueList()) monitor->setFocused(monitor == focusedMonitor);

	this->bFocusedWorkspace = focusedWorkspace;
	this->bFocusedMonitor = focusedMonitor;

	if (focusedMonitor != previousFocusedMonitor && focusedMonitor) {
		this->emitRawEvent(
		    QStringLiteral("focusedmon"),
		    focusedMonitor->bindableName().value() + u','
		        + (focusedWorkspace ? focusedWorkspace->bindableName().value() : QString())
		);
	}

	if (focusedWorkspace != previousFocusedWorkspace && focusedWorkspace) {
		auto workspaceName = focusedWorkspace->bindableName().value();
		this->emitRawEvent(QStringLiteral("workspace"), workspaceName);
		this->emitRawEvent(
		    QStringLiteral("workspacev2"),
		    QString::number(focusedWorkspace->bindableId().value()) + u',' + workspaceName
		);
	}
}

HyprlandWorkspace* HyprlandState::workspaceForSpace(quint64 spaceId) const {
	for (auto* workspace: this->mWorkspaces.valueList()) {
		if (workspace->spaceId() == spaceId) return workspace;
	}
	return nullptr;
}

void HyprlandState::refreshToplevels() {
	this->mToplevelReader.request(
	    this,
	    [] { return std::pair(macos::readWindows(), macos::frontmostWindowId()); },
	    [this](const std::pair<QList<macos::WindowDescription>, quint32>& snapshot) {
		    this->applyToplevels(snapshot.first, snapshot.second);
	    }
	);
}

void HyprlandState::applyToplevels(const QList<macos::WindowDescription>& windows, quint32 frontWindowId) {
	auto* previousActiveToplevel = this->bActiveToplevel.value();

	QList<HyprlandToplevel*> currentToplevels;
	QHash<HyprlandWorkspace*, QList<HyprlandToplevel*>> toplevelsByWorkspace;
	HyprlandToplevel* activeToplevel = nullptr;

	for (const auto& window: windows) {
		HyprlandWorkspace* workspace = nullptr;
		for (auto spaceId: window.spaceIds) {
			workspace = this->workspaceForSpace(spaceId);
			if (workspace) break;
		}

		auto* toplevel = this->mToplevelsByWindow.value(window.windowId);
		if (!toplevel) {
			toplevel = new HyprlandToplevel(window.windowId, this);
			this->mToplevelsByWindow.insert(window.windowId, toplevel);
			this->emitRawEvent(
			    QStringLiteral("openwindow"),
			    toplevel->address() + u',' + (workspace ? workspace->bindableName().value() : QString())
			        + u',' + window.applicationName + u',' + window.title
			);
		}

		auto isActive = window.windowId == frontWindowId;
		toplevel->update(window, workspace, isActive);
		if (isActive) activeToplevel = toplevel;

		currentToplevels.append(toplevel);
		if (workspace) toplevelsByWorkspace[workspace].append(toplevel);
	}

	for (auto iterator = this->mToplevelsByWindow.begin(); iterator != this->mToplevelsByWindow.end();) {
		if (currentToplevels.contains(iterator.value())) {
			++iterator;
			continue;
		}

		auto* removedToplevel = iterator.value();
		this->emitRawEvent(QStringLiteral("closewindow"), removedToplevel->address());
		iterator = this->mToplevelsByWindow.erase(iterator);
		this->mToplevels.removeObject(removedToplevel);
		for (auto* workspace: this->mWorkspaces.valueList()) {
			workspace->toplevels()->removeObject(removedToplevel);
		}
		removedToplevel->deleteLater();
	}

	synchronizeModel(this->mToplevels, currentToplevels);

	for (auto* workspace: this->mWorkspaces.valueList()) {
		auto workspaceToplevels = toplevelsByWorkspace.value(workspace);
		synchronizeModel(*workspace->toplevels(), workspaceToplevels);
		workspace->rebuildIpcObject(static_cast<int>(workspaceToplevels.size()));
	}

	this->bActiveToplevel = activeToplevel;

	if (activeToplevel != previousActiveToplevel) {
		auto ipcObject = activeToplevel ? activeToplevel->bindableLastIpcObject().value() : QVariantMap();
		this->emitRawEvent(
		    QStringLiteral("activewindow"),
		    ipcObject.value(QStringLiteral("class")).toString() + u','
		        + ipcObject.value(QStringLiteral("title")).toString()
		);
		this->emitRawEvent(
		    QStringLiteral("activewindowv2"),
		    activeToplevel ? activeToplevel->address() : QString()
		);
	}
}

void HyprlandState::activateWorkspace(HyprlandWorkspace* workspace) {
	if (!workspace) return;

	auto* monitor = workspace->bindableMonitor().value();
	auto* currentWorkspace = monitor ? monitor->bindableActiveWorkspace().value() : nullptr;
	auto steps = currentWorkspace ? workspace->positionOnDisplay() - currentWorkspace->positionOnDisplay() : 0;

	if (!macos::switchToDesktop(workspace->bindableId().value(), steps)) {
		qCWarning(logHyprland) << "Switching workspaces needs Accessibility permission for MacShell.";
	}
}

void HyprlandState::switchWorkspaceRelative(int offset) {
	auto* focusedWorkspace = this->bFocusedWorkspace.value();
	if (!focusedWorkspace || this->mWorkspaces.valueList().isEmpty()) return;

	auto workspaceCount = static_cast<int>(this->mWorkspaces.valueList().size());
	auto currentIndex = focusedWorkspace->bindableId().value() - 1;
	auto targetIndex = ((currentIndex + offset) % workspaceCount + workspaceCount) % workspaceCount;
	this->activateWorkspace(this->mWorkspaces.valueList().at(targetIndex));
}

void HyprlandState::dispatch(const QString& request) {
	auto trimmedRequest = request.trimmed();
	auto separatorIndex = trimmedRequest.indexOf(u' ');
	auto command = separatorIndex == -1 ? trimmedRequest : trimmedRequest.first(separatorIndex);
	auto argument = separatorIndex == -1 ? QString() : trimmedRequest.sliced(separatorIndex + 1).trimmed();

	if (command == QStringLiteral("exec")) {
		macos::launchDetached(argument);
		return;
	}

	if (command == QStringLiteral("workspace") || command == QStringLiteral("focusworkspaceoncurrentmonitor")) {
		auto relativeArgument = argument;
		for (const auto& prefix: {QStringLiteral("e"), QStringLiteral("r"), QStringLiteral("m")}) {
			if (relativeArgument.startsWith(prefix + u'+') || relativeArgument.startsWith(prefix + u'-')) {
				relativeArgument = relativeArgument.sliced(1);
				break;
			}
		}

		auto isNumber = false;
		auto number = relativeArgument.toInt(&isNumber);
		if (!isNumber) {
			qCWarning(logHyprland) << "Unsupported workspace target on macOS:" << argument;
			return;
		}

		if (relativeArgument.startsWith(u'+') || relativeArgument.startsWith(u'-')) {
			this->switchWorkspaceRelative(number);
		} else {
			this->activateWorkspace(this->mWorkspacesById.value(number));
		}
		return;
	}

	qCWarning(logHyprland) << "Unsupported Hyprland dispatcher on macOS:" << command;
}

HyprlandMonitor* HyprlandState::monitorForName(const QString& name) const {
	for (auto* monitor: this->mMonitors.valueList()) {
		if (monitor->bindableName().value() == name) return monitor;
	}
	return nullptr;
}

HyprlandIpcQml::HyprlandIpcQml(QObject* parent): QObject(parent) {
	auto* state = HyprlandState::instance();
	QObject::connect(state, &HyprlandState::rawEvent, this, &HyprlandIpcQml::rawEvent);
	QObject::connect(state, &HyprlandState::focusedMonitorChanged, this, &HyprlandIpcQml::focusedMonitorChanged);
	QObject::connect(state, &HyprlandState::focusedWorkspaceChanged, this, &HyprlandIpcQml::focusedWorkspaceChanged);
	QObject::connect(state, &HyprlandState::activeToplevelChanged, this, &HyprlandIpcQml::activeToplevelChanged);
}

void HyprlandIpcQml::dispatch(const QString& request) { HyprlandState::instance()->dispatch(request); }

HyprlandMonitor* HyprlandIpcQml::monitorFor(QuickshellScreenInfo* screen) {
	return screen ? HyprlandState::instance()->monitorForName(screen->name()) : nullptr;
}

void HyprlandIpcQml::refreshMonitors() { HyprlandState::instance()->refreshMonitors(); }
void HyprlandIpcQml::refreshWorkspaces() { HyprlandState::instance()->refreshWorkspaces(); }
void HyprlandIpcQml::refreshToplevels() { HyprlandState::instance()->refreshToplevels(); }

ObjectModel<HyprlandMonitor>* HyprlandIpcQml::monitors() { return HyprlandState::instance()->monitors(); }

ObjectModel<HyprlandWorkspace>* HyprlandIpcQml::workspaces() {
	return HyprlandState::instance()->workspaces();
}

ObjectModel<HyprlandToplevel>* HyprlandIpcQml::toplevels() {
	return HyprlandState::instance()->toplevels();
}

QBindable<HyprlandMonitor*> HyprlandIpcQml::bindableFocusedMonitor() {
	return HyprlandState::instance()->bindableFocusedMonitor();
}

QBindable<HyprlandWorkspace*> HyprlandIpcQml::bindableFocusedWorkspace() {
	return HyprlandState::instance()->bindableFocusedWorkspace();
}

QBindable<HyprlandToplevel*> HyprlandIpcQml::bindableActiveToplevel() {
	return HyprlandState::instance()->bindableActiveToplevel();
}

}

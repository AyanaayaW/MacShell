#include "i3.hpp"
#include <utility>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qregularexpression.h>
#include <qset.h>
#include <qstring.h>
#include <qvariant.h>

#include "../hyprland/hyprland.hpp"

namespace qs::i3::ipc {

namespace {

Q_LOGGING_CATEGORY(logI3, "quickshell.i3.macos", QtWarningMsg);

using hyprland::ipc::HyprlandMonitor;
using hyprland::ipc::HyprlandState;
using hyprland::ipc::HyprlandWorkspace;

QVariantMap rectangleOf(HyprlandMonitor* monitor) {
	if (!monitor) return {};
	return {
	    {QStringLiteral("x"), monitor->bindableX().value()},
	    {QStringLiteral("y"), monitor->bindableY().value()},
	    {QStringLiteral("width"), monitor->bindableWidth().value()},
	    {QStringLiteral("height"), monitor->bindableHeight().value()},
	};
}

}

I3IpcEvent::I3IpcEvent(QString type, QString data, QObject* parent)
    : QObject(parent)
    , mType(std::move(type))
    , mData(std::move(data)) {}

I3Workspace::I3Workspace(HyprlandWorkspace* source, QObject* parent): QObject(parent), mSource(source) {}

void I3Workspace::synchronize(I3Monitor* monitor) {
	auto* sourceMonitor = this->mSource->bindableMonitor().value();

	Qt::beginPropertyUpdateGroup();
	this->bId = this->mSource->bindableId().value();
	this->bName = this->mSource->bindableName().value();
	this->bNumber = this->mSource->bindableId().value();
	this->bActive = this->mSource->bindableActive().value();
	this->bFocused = this->mSource->bindableFocused().value();
	this->bMonitor = monitor;
	Qt::endPropertyUpdateGroup();

	this->mLastIpcObject = {
	    {QStringLiteral("id"), this->bId.value()},
	    {QStringLiteral("num"), this->bNumber.value()},
	    {QStringLiteral("name"), this->bName.value()},
	    {QStringLiteral("visible"), this->bActive.value()},
	    {QStringLiteral("focused"), this->bFocused.value()},
	    {QStringLiteral("urgent"), false},
	    {QStringLiteral("output"), sourceMonitor ? sourceMonitor->bindableName().value() : QString()},
	    {QStringLiteral("rect"), rectangleOf(sourceMonitor)},
	};
	emit this->lastIpcObjectChanged();
}

void I3Workspace::activate() { this->mSource->activate(); }

I3Monitor::I3Monitor(HyprlandMonitor* source, QObject* parent): QObject(parent), mSource(source) {}

void I3Monitor::synchronize(I3Workspace* activeWorkspace) {
	Qt::beginPropertyUpdateGroup();
	this->bId = this->mSource->bindableId().value();
	this->bName = this->mSource->bindableName().value();
	this->bX = this->mSource->bindableX().value();
	this->bY = this->mSource->bindableY().value();
	this->bWidth = this->mSource->bindableWidth().value();
	this->bHeight = this->mSource->bindableHeight().value();
	this->bScale = this->mSource->bindableScale().value();
	this->bFocused = this->mSource->bindableFocused().value();
	this->bActiveWorkspace = activeWorkspace;
	Qt::endPropertyUpdateGroup();

	this->mLastIpcObject = {
	    {QStringLiteral("id"), this->bId.value()},
	    {QStringLiteral("name"), this->bName.value()},
	    {QStringLiteral("active"), true},
	    {QStringLiteral("power"), true},
	    {QStringLiteral("focused"), this->bFocused.value()},
	    {QStringLiteral("scale"), this->bScale.value()},
	    {QStringLiteral("current_workspace"), activeWorkspace ? activeWorkspace->bindableName().value() : QString()},
	    {QStringLiteral("rect"), rectangleOf(this->mSource)},
	};
	emit this->lastIpcObjectChanged();
}

I3State* I3State::instance() {
	static auto* sharedInstance = new I3State();
	return sharedInstance;
}

I3State::I3State() {
	auto* hyprland = HyprlandState::instance();
	QObject::connect(hyprland->workspaces(), &UntypedObjectModel::valuesChanged, this, &I3State::synchronize);
	QObject::connect(hyprland->monitors(), &UntypedObjectModel::valuesChanged, this, &I3State::synchronize);
	QObject::connect(hyprland, &HyprlandState::focusedWorkspaceChanged, this, &I3State::synchronize);
	QObject::connect(hyprland, &HyprlandState::focusedMonitorChanged, this, &I3State::synchronize);
	this->synchronize();
}

void I3State::emitRawEvent(const QString& type, const QString& data) {
	auto* event = new I3IpcEvent(type, data, this);
	emit this->rawEvent(event);
	event->deleteLater();
}

void I3State::synchronize() {
	auto* hyprland = HyprlandState::instance();

	QSet<HyprlandMonitor*> presentMonitors;
	for (auto* sourceMonitor: hyprland->monitors()->valueList()) {
		presentMonitors.insert(sourceMonitor);
		if (!this->mMonitorsBySource.contains(sourceMonitor)) {
			auto* monitor = new I3Monitor(sourceMonitor, this);
			this->mMonitorsBySource.insert(sourceMonitor, monitor);
			this->mMonitors.insertObject(monitor);
		}
	}

	QSet<HyprlandWorkspace*> presentWorkspaces;
	for (auto* sourceWorkspace: hyprland->workspaces()->valueList()) {
		presentWorkspaces.insert(sourceWorkspace);
		if (!this->mWorkspacesBySource.contains(sourceWorkspace)) {
			auto* workspace = new I3Workspace(sourceWorkspace, this);
			this->mWorkspacesBySource.insert(sourceWorkspace, workspace);
			this->mWorkspaces.insertObject(workspace);
			QObject::connect(sourceWorkspace, &HyprlandWorkspace::activeChanged, this, &I3State::synchronize);
		}
	}

	for (auto iterator = this->mWorkspacesBySource.begin(); iterator != this->mWorkspacesBySource.end();) {
		if (presentWorkspaces.contains(iterator.key())) {
			++iterator;
			continue;
		}
		this->mWorkspaces.removeObject(iterator.value());
		iterator.value()->deleteLater();
		iterator = this->mWorkspacesBySource.erase(iterator);
	}

	for (auto iterator = this->mMonitorsBySource.begin(); iterator != this->mMonitorsBySource.end();) {
		if (presentMonitors.contains(iterator.key())) {
			++iterator;
			continue;
		}
		this->mMonitors.removeObject(iterator.value());
		iterator.value()->deleteLater();
		iterator = this->mMonitorsBySource.erase(iterator);
	}

	for (auto* workspace: this->mWorkspaces.valueList()) {
		workspace->synchronize(this->mMonitorsBySource.value(workspace->source()->bindableMonitor().value()));
	}
	for (auto* monitor: this->mMonitors.valueList()) {
		monitor->synchronize(this->mWorkspacesBySource.value(monitor->source()->bindableActiveWorkspace().value()));
	}

	auto* previousFocusedWorkspace = this->bFocusedWorkspace.value();
	this->bFocusedWorkspace = this->mWorkspacesBySource.value(hyprland->bindableFocusedWorkspace().value());
	this->bFocusedMonitor = this->mMonitorsBySource.value(hyprland->bindableFocusedMonitor().value());

	if (this->bFocusedWorkspace.value() != previousFocusedWorkspace && this->bFocusedWorkspace.value()) {
		this->emitRawEvent(QStringLiteral("workspace"), QStringLiteral("{\"change\":\"focus\"}"));
	}
}

void I3State::dispatch(const QString& request) {
	static const QRegularExpression workspacePattern(
	    QStringLiteral("^workspace\\s+(?:number\\s+)?\"?([^\"]+)\"?$")
	);

	auto match = workspacePattern.match(request.trimmed());
	if (!match.hasMatch()) {
		qCWarning(logI3) << "Unsupported sway command on macOS:" << request;
		return;
	}

	auto target = match.captured(1).trimmed();
	if (auto* workspace = this->workspaceForName(target)) {
		workspace->activate();
	} else {
		HyprlandState::instance()->dispatch(QStringLiteral("workspace ") + target);
	}
}

I3Workspace* I3State::workspaceForName(const QString& name) const {
	for (auto* workspace: this->mWorkspaces.valueList()) {
		if (workspace->bindableName().value() == name) return workspace;
	}
	return nullptr;
}

I3Monitor* I3State::monitorForName(const QString& name) const {
	for (auto* monitor: this->mMonitors.valueList()) {
		if (monitor->bindableName().value() == name) return monitor;
	}
	return nullptr;
}

I3IpcListener::I3IpcListener(QObject* parent): QObject(parent) {
	QObject::connect(I3State::instance(), &I3State::rawEvent, this, [this](I3IpcEvent* event) {
		if (this->mSubscriptions.isEmpty() || this->mSubscriptions.contains(event->type())) {
			emit this->ipcEvent(event);
		}
	});
}

void I3IpcListener::setSubscriptions(QList<QString> subscriptions) {
	if (subscriptions == this->mSubscriptions) return;
	this->mSubscriptions = std::move(subscriptions);
	emit this->subscriptionsChanged();
}

I3IpcQml::I3IpcQml(QObject* parent): QObject(parent) {
	auto* state = I3State::instance();
	QObject::connect(state, &I3State::rawEvent, this, &I3IpcQml::rawEvent);
	QObject::connect(state, &I3State::focusedWorkspaceChanged, this, &I3IpcQml::focusedWorkspaceChanged);
	QObject::connect(state, &I3State::focusedMonitorChanged, this, &I3IpcQml::focusedMonitorChanged);
}

void I3IpcQml::dispatch(const QString& request) { I3State::instance()->dispatch(request); }

void I3IpcQml::refreshMonitors() { HyprlandState::instance()->refreshMonitors(); }

void I3IpcQml::refreshWorkspaces() { HyprlandState::instance()->refreshWorkspaces(); }

I3Workspace* I3IpcQml::findWorkspaceByName(const QString& name) {
	return I3State::instance()->workspaceForName(name);
}

I3Monitor* I3IpcQml::findMonitorByName(const QString& name) { return I3State::instance()->monitorForName(name); }

}

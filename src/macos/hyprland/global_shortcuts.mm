#include "global_shortcuts.hpp"
#include <array>

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qregularexpression.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtextstream.h>

#include "hyprland.hpp"

#import <Carbon/Carbon.h>

namespace qs::hyprland::global_shortcuts {

namespace {

Q_LOGGING_CATEGORY(logShortcuts, "quickshell.hyprland.shortcuts.macos", QtWarningMsg);

constexpr OSType HOT_KEY_SIGNATURE = 'MSHL';
const QString GLOBAL_DISPATCHER = QStringLiteral("global");

const QHash<QString, quint32>& keyCodesByName() {
	static const QHash<QString, quint32> keyCodes {
	    {QStringLiteral("A"), kVK_ANSI_A}, {QStringLiteral("B"), kVK_ANSI_B}, {QStringLiteral("C"), kVK_ANSI_C},
	    {QStringLiteral("D"), kVK_ANSI_D}, {QStringLiteral("E"), kVK_ANSI_E}, {QStringLiteral("F"), kVK_ANSI_F},
	    {QStringLiteral("G"), kVK_ANSI_G}, {QStringLiteral("H"), kVK_ANSI_H}, {QStringLiteral("I"), kVK_ANSI_I},
	    {QStringLiteral("J"), kVK_ANSI_J}, {QStringLiteral("K"), kVK_ANSI_K}, {QStringLiteral("L"), kVK_ANSI_L},
	    {QStringLiteral("M"), kVK_ANSI_M}, {QStringLiteral("N"), kVK_ANSI_N}, {QStringLiteral("O"), kVK_ANSI_O},
	    {QStringLiteral("P"), kVK_ANSI_P}, {QStringLiteral("Q"), kVK_ANSI_Q}, {QStringLiteral("R"), kVK_ANSI_R},
	    {QStringLiteral("S"), kVK_ANSI_S}, {QStringLiteral("T"), kVK_ANSI_T}, {QStringLiteral("U"), kVK_ANSI_U},
	    {QStringLiteral("V"), kVK_ANSI_V}, {QStringLiteral("W"), kVK_ANSI_W}, {QStringLiteral("X"), kVK_ANSI_X},
	    {QStringLiteral("Y"), kVK_ANSI_Y}, {QStringLiteral("Z"), kVK_ANSI_Z},
	    {QStringLiteral("0"), kVK_ANSI_0}, {QStringLiteral("1"), kVK_ANSI_1}, {QStringLiteral("2"), kVK_ANSI_2},
	    {QStringLiteral("3"), kVK_ANSI_3}, {QStringLiteral("4"), kVK_ANSI_4}, {QStringLiteral("5"), kVK_ANSI_5},
	    {QStringLiteral("6"), kVK_ANSI_6}, {QStringLiteral("7"), kVK_ANSI_7}, {QStringLiteral("8"), kVK_ANSI_8},
	    {QStringLiteral("9"), kVK_ANSI_9},
	    {QStringLiteral("F1"), kVK_F1}, {QStringLiteral("F2"), kVK_F2}, {QStringLiteral("F3"), kVK_F3},
	    {QStringLiteral("F4"), kVK_F4}, {QStringLiteral("F5"), kVK_F5}, {QStringLiteral("F6"), kVK_F6},
	    {QStringLiteral("F7"), kVK_F7}, {QStringLiteral("F8"), kVK_F8}, {QStringLiteral("F9"), kVK_F9},
	    {QStringLiteral("F10"), kVK_F10}, {QStringLiteral("F11"), kVK_F11}, {QStringLiteral("F12"), kVK_F12},
	    {QStringLiteral("F13"), kVK_F13}, {QStringLiteral("F14"), kVK_F14}, {QStringLiteral("F15"), kVK_F15},
	    {QStringLiteral("F16"), kVK_F16}, {QStringLiteral("F17"), kVK_F17}, {QStringLiteral("F18"), kVK_F18},
	    {QStringLiteral("F19"), kVK_F19}, {QStringLiteral("F20"), kVK_F20},
	    {QStringLiteral("SPACE"), kVK_Space}, {QStringLiteral("RETURN"), kVK_Return},
	    {QStringLiteral("ENTER"), kVK_Return}, {QStringLiteral("ESCAPE"), kVK_Escape},
	    {QStringLiteral("TAB"), kVK_Tab}, {QStringLiteral("BACKSPACE"), kVK_Delete},
	    {QStringLiteral("DELETE"), kVK_ForwardDelete}, {QStringLiteral("LEFT"), kVK_LeftArrow},
	    {QStringLiteral("RIGHT"), kVK_RightArrow}, {QStringLiteral("UP"), kVK_UpArrow},
	    {QStringLiteral("DOWN"), kVK_DownArrow}, {QStringLiteral("HOME"), kVK_Home},
	    {QStringLiteral("END"), kVK_End}, {QStringLiteral("PAGE_UP"), kVK_PageUp},
	    {QStringLiteral("PRIOR"), kVK_PageUp}, {QStringLiteral("PAGE_DOWN"), kVK_PageDown},
	    {QStringLiteral("NEXT"), kVK_PageDown}, {QStringLiteral("MINUS"), kVK_ANSI_Minus},
	    {QStringLiteral("EQUAL"), kVK_ANSI_Equal}, {QStringLiteral("COMMA"), kVK_ANSI_Comma},
	    {QStringLiteral("PERIOD"), kVK_ANSI_Period}, {QStringLiteral("SLASH"), kVK_ANSI_Slash},
	    {QStringLiteral("SEMICOLON"), kVK_ANSI_Semicolon}, {QStringLiteral("APOSTROPHE"), kVK_ANSI_Quote},
	    {QStringLiteral("BRACKETLEFT"), kVK_ANSI_LeftBracket},
	    {QStringLiteral("BRACKETRIGHT"), kVK_ANSI_RightBracket},
	    {QStringLiteral("BACKSLASH"), kVK_ANSI_Backslash}, {QStringLiteral("GRAVE"), kVK_ANSI_Grave},
	};
	return keyCodes;
}

std::optional<quint32> modifierFor(const QString& name) {
	static const QHash<QString, quint32> modifiers {
	    {QStringLiteral("SUPER"), cmdKey},     {QStringLiteral("WIN"), cmdKey},
	    {QStringLiteral("LOGO"), cmdKey},      {QStringLiteral("MOD4"), cmdKey},
	    {QStringLiteral("META"), cmdKey},      {QStringLiteral("CMD"), cmdKey},
	    {QStringLiteral("COMMAND"), cmdKey},   {QStringLiteral("ALT"), optionKey},
	    {QStringLiteral("MOD1"), optionKey},   {QStringLiteral("OPTION"), optionKey},
	    {QStringLiteral("CTRL"), controlKey},  {QStringLiteral("CONTROL"), controlKey},
	    {QStringLiteral("SHIFT"), shiftKey},
	};

	auto iterator = modifiers.find(name.toUpper());
	if (iterator == modifiers.end()) return std::nullopt;
	return iterator.value();
}

QString substituteVariables(QString text, const QHash<QString, QString>& variables) {
	for (const auto& [name, value]: variables.asKeyValueRange()) {
		text.replace(u'$' + name, value);
	}
	return text;
}

QStringList bindingFiles() {
	auto configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
	if (configHome.isEmpty()) configHome = QDir::home().filePath(QStringLiteral(".config"));

	return {
	    QDir(configHome).filePath(QStringLiteral("hypr/hyprland.conf")),
	    QDir(configHome).filePath(QStringLiteral("macshell/shortcuts.conf")),
	};
}

bool allowsEveryDispatcher(const QString& path) {
	return path.endsWith(QStringLiteral("macshell/shortcuts.conf"));
}

QList<ShortcutBinding> parseBindingFile(const QString& path) {
	QList<ShortcutBinding> bindings;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return bindings;

	static const QRegularExpression variablePattern(QStringLiteral("^\\$([A-Za-z0-9_]+)\\s*=\\s*(.*)$"));
	static const QRegularExpression bindPattern(QStringLiteral("^bind[a-z]*\\s*=\\s*(.*)$"));

	QHash<QString, QString> variables;
	auto everyDispatcher = allowsEveryDispatcher(path);
	QTextStream stream(&file);

	while (!stream.atEnd()) {
		auto line = stream.readLine().trimmed();
		auto commentStart = line.indexOf(u'#');
		if (commentStart != -1) line = line.first(commentStart).trimmed();
		if (line.isEmpty()) continue;

		if (auto match = variablePattern.match(line); match.hasMatch()) {
			variables.insert(match.captured(1), match.captured(2).trimmed());
			continue;
		}

		auto match = bindPattern.match(line);
		if (!match.hasMatch()) continue;

		auto fields = substituteVariables(match.captured(1), variables).split(u',');
		if (fields.size() < 3) continue;

		auto dispatcher = fields.at(2).trimmed();
		if (!everyDispatcher && dispatcher != GLOBAL_DISPATCHER) continue;

		auto keys = parseKeyCombination(fields.at(0), fields.at(1));
		if (!keys) {
			qCWarning(logShortcuts) << "Unsupported key binding in" << path << ":" << line;
			continue;
		}

		auto argument = fields.mid(3).join(u',').trimmed();
		bindings.append({.keys = *keys, .dispatcher = dispatcher, .argument = argument});
	}

	return bindings;
}

OSStatus handleHotKeyEvent(
    [[maybe_unused]] EventHandlerCallRef handlerCall,
    EventRef event,
    [[maybe_unused]] void* userData
) {
	EventHotKeyID hotKeyId {};
	GetEventParameter(
	    event,
	    kEventParamDirectObject,
	    typeEventHotKeyID,
	    nullptr,
	    sizeof(hotKeyId),
	    nullptr,
	    &hotKeyId
	);

	auto pressed = GetEventKind(event) == kEventHotKeyPressed;
	HotKeyRegistry::instance()->handleHotKey(hotKeyId.id, pressed);
	return noErr;
}

}

std::optional<KeyCombination> parseKeyCombination(const QString& modifiers, const QString& key) {
	static const QRegularExpression modifierSeparators(QStringLiteral("[\\s_+]+"));

	KeyCombination combination;
	for (const auto& modifierName: modifiers.split(modifierSeparators, Qt::SkipEmptyParts)) {
		auto modifier = modifierFor(modifierName);
		if (!modifier) return std::nullopt;
		combination.modifiers |= *modifier;
	}

	auto keyCode = keyCodesByName().find(key.trimmed().toUpper());
	if (keyCode == keyCodesByName().end()) return std::nullopt;

	combination.keyCode = keyCode.value();
	return combination;
}

std::optional<KeyCombination> parseTriggerDescription(const QString& description) {
	auto parts = description.split(u'+', Qt::SkipEmptyParts);
	if (parts.size() < 2) return std::nullopt;

	auto key = parts.takeLast().trimmed();
	return parseKeyCombination(parts.join(u' '), key);
}

HotKeyRegistry* HotKeyRegistry::instance() {
	static auto* sharedInstance = new HotKeyRegistry();
	return sharedInstance;
}

HotKeyRegistry::HotKeyRegistry() {
	const std::array<EventTypeSpec, 2> eventTypes {{
	    {kEventClassKeyboard, kEventHotKeyPressed},
	    {kEventClassKeyboard, kEventHotKeyReleased},
	}};
	InstallApplicationEventHandler(
	    &handleHotKeyEvent,
	    static_cast<ItemCount>(eventTypes.size()),
	    eventTypes.data(),
	    nullptr,
	    nullptr
	);

	QObject::connect(&this->mFileWatcher, &QFileSystemWatcher::fileChanged, this, &HotKeyRegistry::reloadBindings);
	QObject::connect(&this->mFileWatcher, &QFileSystemWatcher::directoryChanged, this, &HotKeyRegistry::reloadBindings);
	this->reloadBindings();
}

void HotKeyRegistry::watchBindingFiles() {
	for (const auto& path: bindingFiles()) {
		if (QFileInfo::exists(path) && !this->mFileWatcher.files().contains(path)) {
			this->mFileWatcher.addPath(path);
		}

		auto directory = QFileInfo(path).absolutePath();
		if (QFileInfo::exists(directory) && !this->mFileWatcher.directories().contains(directory)) {
			this->mFileWatcher.addPath(directory);
		}
	}
}

void HotKeyRegistry::reloadBindings() {
	for (auto hotKeyId: this->mFileBindings.keys()) this->unregisterCombination(hotKeyId);
	this->mFileBindings.clear();

	for (const auto& path: bindingFiles()) {
		for (const auto& binding: parseBindingFile(path)) {
			auto hotKeyId = this->registerCombination(binding.keys);
			if (hotKeyId != 0) this->mFileBindings.insert(hotKeyId, binding);
		}
	}

	this->watchBindingFiles();
}

quint32 HotKeyRegistry::registerCombination(const KeyCombination& keys) {
	auto hotKeyId = this->mNextHotKeyId++;
	EventHotKeyRef reference = nullptr;
	EventHotKeyID identifier {HOT_KEY_SIGNATURE, hotKeyId};

	auto status = RegisterEventHotKey(
	    keys.keyCode,
	    keys.modifiers,
	    identifier,
	    GetApplicationEventTarget(),
	    0,
	    &reference
	);

	if (status != noErr) {
		qCWarning(logShortcuts) << "Could not register a global shortcut; it may already be in use.";
		return 0;
	}

	this->mHotKeyReferences.insert(hotKeyId, reference);
	return hotKeyId;
}

void HotKeyRegistry::unregisterCombination(quint32 hotKeyId) {
	if (auto* reference = this->mHotKeyReferences.take(hotKeyId)) {
		UnregisterEventHotKey(static_cast<EventHotKeyRef>(reference));
	}
}

bool HotKeyRegistry::hasGlobalBinding(const QString& qualifiedName) const {
	return std::ranges::any_of(this->mFileBindings, [&qualifiedName](const ShortcutBinding& binding) {
		return binding.dispatcher == GLOBAL_DISPATCHER && binding.argument == qualifiedName;
	});
}

void HotKeyRegistry::handleHotKey(quint32 hotKeyId, bool pressed) {
	emit this->combinationTriggered(hotKeyId, pressed);

	auto binding = this->mFileBindings.find(hotKeyId);
	if (binding == this->mFileBindings.end()) return;

	if (binding->dispatcher == GLOBAL_DISPATCHER) {
		emit this->globalShortcutTriggered(binding->argument, pressed);
	} else if (pressed) {
		ipc::HyprlandState::instance()->dispatch(binding->dispatcher + u' ' + binding->argument);
	}
}

GlobalShortcut::GlobalShortcut(QObject* parent): QObject(parent) {
	auto* registry = HotKeyRegistry::instance();

	QObject::connect(registry, &HotKeyRegistry::globalShortcutTriggered, this, [this](const QString& name, bool pressed) {
		if (name == this->qualifiedName()) this->setPressedState(pressed);
	});

	QObject::connect(registry, &HotKeyRegistry::combinationTriggered, this, [this](quint32 hotKeyId, bool pressed) {
		if (hotKeyId != 0 && hotKeyId == this->mOwnHotKeyId) this->setPressedState(pressed);
	});
}

GlobalShortcut::~GlobalShortcut() {
	if (this->mOwnHotKeyId != 0) HotKeyRegistry::instance()->unregisterCombination(this->mOwnHotKeyId);
}

void GlobalShortcut::setPressedState(bool pressed) {
	if (pressed == this->mPressed) return;
	this->mPressed = pressed;
	emit this->pressedChanged();

	if (pressed) emit this->pressed();
	else emit this->released();
}

void GlobalShortcut::updateOwnHotKey() {
	auto* registry = HotKeyRegistry::instance();
	if (this->mOwnHotKeyId != 0) {
		registry->unregisterCombination(this->mOwnHotKeyId);
		this->mOwnHotKeyId = 0;
	}

	if (this->mName.isEmpty() || registry->hasGlobalBinding(this->qualifiedName())) return;

	if (auto keys = parseTriggerDescription(this->mTriggerDescription)) {
		this->mOwnHotKeyId = registry->registerCombination(*keys);
	}
}

void GlobalShortcut::setAppid(const QString& appid) {
	if (appid == this->mAppid) return;
	this->mAppid = appid;
	emit this->appidChanged();
	this->updateOwnHotKey();
}

void GlobalShortcut::setName(const QString& name) {
	if (name == this->mName) return;
	this->mName = name;
	emit this->nameChanged();
	this->updateOwnHotKey();
}

void GlobalShortcut::setDescription(const QString& description) {
	if (description == this->mDescription) return;
	this->mDescription = description;
	emit this->descriptionChanged();
}

void GlobalShortcut::setTriggerDescription(const QString& triggerDescription) {
	if (triggerDescription == this->mTriggerDescription) return;
	this->mTriggerDescription = triggerDescription;
	emit this->triggerDescriptionChanged();
	this->updateOwnHotKey();
}

}

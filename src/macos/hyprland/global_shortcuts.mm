#include "global_shortcuts.hpp"
#include <array>
#include <utility>

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qregularexpression.h>
#include <qset.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtenvironmentvariables.h>
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

quint32& superModifier() {
	static quint32 modifier = optionKey;
	return modifier;
}

std::optional<quint32> modifierFor(const QString& name) {
	static const QHash<QString, quint32> modifiers {
	    {QStringLiteral("CMD"), cmdKey},       {QStringLiteral("COMMAND"), cmdKey},
	    {QStringLiteral("ALT"), optionKey},    {QStringLiteral("MOD1"), optionKey},
	    {QStringLiteral("OPTION"), optionKey}, {QStringLiteral("CTRL"), controlKey},
	    {QStringLiteral("CONTROL"), controlKey}, {QStringLiteral("SHIFT"), shiftKey},
	};
	static const QStringList superNames {
	    QStringLiteral("SUPER"), QStringLiteral("WIN"), QStringLiteral("LOGO"),
	    QStringLiteral("MOD4"), QStringLiteral("META"),
	};

	auto upperName = name.toUpper();
	if (superNames.contains(upperName)) return superModifier();
	auto iterator = modifiers.find(upperName);
	if (iterator == modifiers.end()) return std::nullopt;
	return iterator.value();
}

QString substituteVariables(QString text, const QHash<QString, QString>& variables) {
	for (const auto& [name, value]: variables.asKeyValueRange()) {
		text.replace(u'$' + name, value);
	}
	return text;
}

QString configHomeDirectory() {
	auto configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
	return configHome.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : configHome;
}

QStringList bindingFiles() {
	return {
	    QDir(configHomeDirectory()).filePath(QStringLiteral("hypr/hyprland.conf")),
	    QDir(configHomeDirectory()).filePath(QStringLiteral("macshell/shortcuts.conf")),
	};
}

QString expandPath(QString path, const QString& relativeTo) {
	if (path.startsWith(u'~')) path = QDir::homePath() + path.sliced(1);
	if (QDir::isRelativePath(path)) path = QDir(relativeTo).filePath(path);
	return QDir::cleanPath(path);
}

QStringList matchingFiles(const QString& pattern) {
	QFileInfo patternInfo(pattern);
	if (!pattern.contains(u'*') && !pattern.contains(u'?')) return {pattern};
	QDir directory(patternInfo.absolutePath());
	QStringList files;
	for (const auto& entry: directory.entryInfoList({patternInfo.fileName()}, QDir::Files, QDir::Name)) {
		files.append(entry.filePath());
	}
	return files;
}

constexpr int MAXIMUM_SOURCE_DEPTH = 16;

struct HyprlandConfigContents {
	QList<ShortcutBinding> bindings;
	QStringList autostartCommands;
	QList<std::pair<QString, QString>> environment;
};

void collectBindings(
    const QString& path,
    QHash<QString, QString>& variables,
    QSet<QString>& visitedFiles,
    HyprlandConfigContents& contents,
    int depth
) {
	auto canonicalPath = QFileInfo(path).canonicalFilePath();
	if (canonicalPath.isEmpty() || visitedFiles.contains(canonicalPath) || depth > MAXIMUM_SOURCE_DEPTH) return;
	visitedFiles.insert(canonicalPath);

	QFile file(canonicalPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

	static const QRegularExpression variablePattern(QStringLiteral("^\\$([A-Za-z0-9_]+)\\s*=\\s*(.*)$"));
	static const QRegularExpression sourcePattern(QStringLiteral("^source\\s*=\\s*(.+)$"));
	static const QRegularExpression bindPattern(QStringLiteral("^bind([a-z]*)\\s*=\\s*(.*)$"));
	static const QRegularExpression autostartPattern(QStringLiteral("^exec-once\\s*=\\s*(.+)$"));
	static const QRegularExpression environmentPattern(QStringLiteral("^env\\s*=\\s*(.+)$"));

	QTextStream stream(&file);
	while (!stream.atEnd()) {
		auto line = stream.readLine().trimmed();
		auto commentStart = line.indexOf(QStringLiteral(" #"));
		if (line.startsWith(u'#')) continue;
		if (commentStart != -1) line = line.first(commentStart).trimmed();
		if (line.isEmpty()) continue;

		if (auto match = variablePattern.match(line); match.hasMatch()) {
			variables.insert(match.captured(1), substituteVariables(match.captured(2).trimmed(), variables));
			continue;
		}

		if (auto match = sourcePattern.match(line); match.hasMatch()) {
			auto target = expandPath(substituteVariables(match.captured(1).trimmed(), variables), QFileInfo(canonicalPath).absolutePath());
			for (const auto& sourcedFile: matchingFiles(target)) {
				collectBindings(sourcedFile, variables, visitedFiles, contents, depth + 1);
			}
			continue;
		}

		if (auto match = autostartPattern.match(line); match.hasMatch()) {
			contents.autostartCommands.append(substituteVariables(match.captured(1).trimmed(), variables));
			continue;
		}

		if (auto match = environmentPattern.match(line); match.hasMatch()) {
			auto assignment = substituteVariables(match.captured(1), variables);
			auto separator = assignment.indexOf(u',');
			if (separator > 0) {
				contents.environment.append({assignment.first(separator).trimmed(), assignment.sliced(separator + 1).trimmed()});
			}
			continue;
		}

		auto match = bindPattern.match(line);
		if (!match.hasMatch()) continue;

		auto flags = match.captured(1);
		if (flags.contains(u'm')) continue;

		auto fields = substituteVariables(match.captured(2), variables).split(u',');
		auto dispatcherIndex = flags.contains(u'd') ? 3 : 2;
		if (fields.size() <= dispatcherIndex) continue;

		auto keyName = fields.at(1).trimmed();
		if (keyName.isEmpty() || keyName.contains(QStringLiteral("mouse"), Qt::CaseInsensitive)
		    || keyName.startsWith(QStringLiteral("Super_"), Qt::CaseInsensitive)
		    || keyName.startsWith(QStringLiteral("XF86"), Qt::CaseInsensitive))
		{
			continue;
		}

		auto keys = parseKeyCombination(fields.at(0), keyName);
		if (!keys) {
			qCInfo(logShortcuts) << "Skipping key binding macOS cannot register:" << line;
			continue;
		}

		contents.bindings.append({
		    .keys = *keys,
		    .dispatcher = fields.at(dispatcherIndex).trimmed(),
		    .argument = fields.mid(dispatcherIndex + 1).join(u',').trimmed(),
		    .triggersOnRelease = flags.contains(u'r'),
		});
	}
}

HyprlandConfigContents readHyprlandConfig(const QString& path) {
	HyprlandConfigContents contents;
	QHash<QString, QString> variables;
	QSet<QString> visitedFiles;
	collectBindings(path, variables, visitedFiles, contents, 0);
	return contents;
}

QList<ShortcutBinding> parseBindingFile(const QString& path) { return readHyprlandConfig(path).bindings; }

bool launchesQuickshell(const QString& command) {
	static const QRegularExpression quickshellCommand(
	    QStringLiteral("(^|[\\s/;&|])(qs|quickshell|caelestia\\s+shell)(\\s|$)")
	);
	return quickshellCommand.match(command).hasMatch();
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

void setSuperModifier(quint32 carbonModifier) { superModifier() = carbonModifier; }

void runHyprlandAutostart() {
	auto configPath = QDir(configHomeDirectory()).filePath(QStringLiteral("hypr/hyprland.conf"));
	auto contents = readHyprlandConfig(configPath);

	for (const auto& [name, value]: contents.environment) {
		qputenv(name.toLocal8Bit().constData(), value.toLocal8Bit());
	}

	for (const auto& command: contents.autostartCommands) {
		if (launchesQuickshell(command)) continue;
		qCInfo(logShortcuts) << "Starting" << command << "from hyprland.conf";
		ipc::HyprlandState::instance()->dispatch(QStringLiteral("exec ") + command);
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

	static const QHash<int, QString> xkbKeyNames = [] {
		QHash<int, QString> names {
		    {9, QStringLiteral("ESCAPE")},  {20, QStringLiteral("MINUS")},  {21, QStringLiteral("EQUAL")},
		    {22, QStringLiteral("BACKSPACE")}, {23, QStringLiteral("TAB")}, {34, QStringLiteral("BRACKETLEFT")},
		    {35, QStringLiteral("BRACKETRIGHT")}, {36, QStringLiteral("RETURN")}, {47, QStringLiteral("SEMICOLON")},
		    {48, QStringLiteral("APOSTROPHE")}, {49, QStringLiteral("GRAVE")}, {51, QStringLiteral("BACKSLASH")},
		    {59, QStringLiteral("COMMA")},  {60, QStringLiteral("PERIOD")}, {61, QStringLiteral("SLASH")},
		    {65, QStringLiteral("SPACE")},
		};
		const QString digitRow = QStringLiteral("1234567890");
		for (int index = 0; index < digitRow.size(); ++index) names.insert(10 + index, digitRow.at(index));
		const std::array<std::pair<int, QString>, 3> letterRows {{
		    {24, QStringLiteral("QWERTYUIOP")},
		    {38, QStringLiteral("ASDFGHJKL")},
		    {52, QStringLiteral("ZXCVBNM")},
		}};
		for (const auto& [firstCode, letters]: letterRows) {
			for (int index = 0; index < letters.size(); ++index) names.insert(firstCode + index, letters.at(index));
		}
		return names;
	}();
	static const QHash<QString, QString> keyAliases {
	    {QStringLiteral("ESC"), QStringLiteral("ESCAPE")},
	    {QStringLiteral("KP_ENTER"), QStringLiteral("RETURN")},
	    {QStringLiteral("PLUS"), QStringLiteral("EQUAL")},
	    {QStringLiteral("UNDERSCORE"), QStringLiteral("MINUS")},
	    {QStringLiteral("QUESTION"), QStringLiteral("SLASH")},
	};

	auto keyName = key.trimmed().toUpper();
	if (keyName.startsWith(QStringLiteral("CODE:"))) keyName = xkbKeyNames.value(keyName.sliced(5).toInt());
	keyName = keyAliases.value(keyName, keyName);

	auto keyCode = keyCodesByName().find(keyName);
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
			qCDebug(logShortcuts) << "Bound" << binding.dispatcher << binding.argument << "from" << path;
		}
	}
	qCInfo(logShortcuts) << "Registered" << this->mFileBindings.size() << "key bindings";

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
	} else if (pressed != binding->triggersOnRelease) {
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

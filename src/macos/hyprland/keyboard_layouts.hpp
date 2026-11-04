#pragma once

#include <functional>
#include <optional>

#include <qlist.h>
#include <qstring.h>

namespace qs::hyprland::macos {

struct KeyboardLayout {
	QString sourceId;
	QString code;
	QString name;
};

QList<KeyboardLayout> enabledKeyboardLayouts();
std::optional<KeyboardLayout> currentKeyboardLayout();
bool selectKeyboardLayout(const QString& selector);
bool capsLockEnabled();
void watchKeyboardLayout(std::function<void()> onChange);
QByteArray xkbRulesListing();

}

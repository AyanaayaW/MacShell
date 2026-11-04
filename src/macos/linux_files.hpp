#pragma once

#include <optional>

#include <qbytearray.h>
#include <qstring.h>

namespace qs::macos {

std::optional<QByteArray> linuxPseudoFile(const QString& path);

}

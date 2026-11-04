#include "font_aliases.hpp"

#include <qdiriterator.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qfont.h>
#include <qfontdatabase.h>
#include <qhash.h>
#include <qregularexpression.h>
#include <qset.h>
#include <qstring.h>

namespace qs::macos {

namespace {

constexpr qint64 MAXIMUM_SCANNED_FILE_BYTES = 512 * 1024;
constexpr int MAXIMUM_SCANNED_FILES = 4000;

QString normalizedFamilyKey(const QString& family) {
	QString key;
	for (auto character: family) {
		if (character.isLetterOrNumber()) key.append(character.toLower());
	}
	return key;
}

QHash<QString, QString> installedFamiliesByKey() {
	QHash<QString, QString> families;
	for (const auto& family: QFontDatabase::families()) {
		families.insert(normalizedFamilyKey(family), family);
	}
	return families;
}

}

void aliasConfigFontFamilies(const QString& configRoot) {
	static const QRegularExpression quotedName(QStringLiteral("[\"']([A-Za-z][A-Za-z0-9 ._-]{2,48})[\"']"));
	static QSet<QString> aliasedNames;

	auto installedFamilies = installedFamiliesByKey();
	QSet<QString> installedNames;
	for (const auto& family: installedFamilies) installedNames.insert(family);

	QDirIterator files(
	    configRoot,
	    {QStringLiteral("*.qml"), QStringLiteral("*.js"), QStringLiteral("*.json")},
	    QDir::Files,
	    QDirIterator::Subdirectories
	);

	int scannedFiles = 0;
	while (files.hasNext() && scannedFiles < MAXIMUM_SCANNED_FILES) {
		auto path = files.next();
		if (QFileInfo(path).size() > MAXIMUM_SCANNED_FILE_BYTES) continue;
		++scannedFiles;

		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

		auto matches = quotedName.globalMatch(QString::fromUtf8(file.readAll()));
		while (matches.hasNext()) {
			auto requestedName = matches.next().captured(1);
			if (installedNames.contains(requestedName) || aliasedNames.contains(requestedName)) continue;

			auto installedFamily = installedFamilies.value(normalizedFamilyKey(requestedName));
			if (installedFamily.isEmpty()) continue;

			QFont::insertSubstitution(requestedName, installedFamily);
			aliasedNames.insert(requestedName);
		}
	}
}

}

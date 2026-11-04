#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::dwl {

class DwlIpcOutput: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString name READ name CONSTANT);
	QML_ELEMENT;
	QML_UNCREATABLE("DwlIpcOutputs cannot be created.");

public:
	[[nodiscard]] static QString name() { return {}; }
};

class DwlIpcQml: public QObject {
	Q_OBJECT;
	Q_PROPERTY(quint32 tagCount READ tagCount NOTIFY tagCountChanged);
	Q_PROPERTY(QStringList layouts READ layouts NOTIFY layoutsChanged);
	Q_PROPERTY(QList<qs::dwl::DwlIpcOutput*> outputs READ outputs NOTIFY outputsChanged);
	Q_PROPERTY(bool available READ available NOTIFY availableChanged);
	QML_NAMED_ELEMENT(DwlIpc);
	QML_SINGLETON;

public:
	explicit DwlIpcQml(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static quint32 tagCount() { return 0; }
	[[nodiscard]] static QStringList layouts() { return {}; }
	[[nodiscard]] static QList<DwlIpcOutput*> outputs() { return {}; }
	[[nodiscard]] static bool available() { return false; }
	[[nodiscard]] Q_INVOKABLE static qs::dwl::DwlIpcOutput* outputForName([[maybe_unused]] const QString& name) {
		return nullptr;
	}

signals:
	void tagCountChanged();
	void layoutsChanged();
	void outputsChanged();
	void availableChanged();
};

}

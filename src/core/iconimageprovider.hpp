#pragma once

#include <functional>

#include <qicon.h>
#include <qpixmap.h>
#include <qquickimageprovider.h>

class IconImageProvider: public QQuickImageProvider {
public:
	explicit IconImageProvider(): QQuickImageProvider(QQuickImageProvider::Pixmap) {}

	QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;

	static QPixmap missingPixmap(const QSize& size);

	static QIcon themeIcon(const QString& name);
	static bool hasThemeIcon(const QString& name);
	static void setPlatformIconResolver(std::function<QIcon(const QString&)> resolver);

	static QString requestString(
	    const QString& icon,
	    const QString& path = QString(),
	    const QString& fallback = QString()
	);
};

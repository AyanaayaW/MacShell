#include "icons.hpp"
#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

#include <qfileinfo.h>
#include <qhash.h>
#include <qicon.h>
#include <qiconengine.h>
#include <qimage.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qregularexpression.h>
#include <qsize.h>
#include <qstring.h>

#import <AppKit/AppKit.h>

namespace qs::macos {

namespace {

using ImageRenderer = std::function<QImage(const QSize& pixelSize)>;

class RenderedIconEngine: public QIconEngine {
public:
	explicit RenderedIconEngine(ImageRenderer renderer): renderer(std::move(renderer)) {}

	void paint(
	    QPainter* painter,
	    const QRect& rect,
	    [[maybe_unused]] QIcon::Mode mode,
	    [[maybe_unused]] QIcon::State state
	) override {
		auto devicePixelRatio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
		auto image = this->renderer(rect.size() * devicePixelRatio);
		image.setDevicePixelRatio(devicePixelRatio);
		painter->drawImage(rect, image);
	}

	QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
		return this->scaledPixmap(size, mode, state, 1.0);
	}

	QPixmap scaledPixmap(
	    const QSize& size,
	    [[maybe_unused]] QIcon::Mode mode,
	    [[maybe_unused]] QIcon::State state,
	    qreal scale
	) override {
		auto pixmap = QPixmap::fromImage(this->renderer(size * scale));
		pixmap.setDevicePixelRatio(scale);
		return pixmap;
	}

	[[nodiscard]] QIconEngine* clone() const override { return new RenderedIconEngine(this->renderer); }

	[[nodiscard]] bool isNull() override { return false; }

private:
	ImageRenderer renderer;
};

QImage renderImage(NSImage* image, const QSize& pixelSize, NSColor* tintColor) {
	auto width = std::max(pixelSize.width(), 1);
	auto height = std::max(pixelSize.height(), 1);

	QImage target(width, height, QImage::Format_ARGB32_Premultiplied);
	target.fill(Qt::transparent);

	CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGContextRef context = CGBitmapContextCreate(
	    target.bits(),
	    width,
	    height,
	    8,
	    target.bytesPerLine(),
	    colorSpace,
	    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host
	);
	CGColorSpaceRelease(colorSpace);
	if (!context) return target;

	auto imageSize = image.size;
	auto scale = std::min(width / imageSize.width, height / imageSize.height);
	auto drawnWidth = imageSize.width * scale;
	auto drawnHeight = imageSize.height * scale;
	auto drawRect = NSMakeRect((width - drawnWidth) / 2, (height - drawnHeight) / 2, drawnWidth, drawnHeight);

	[NSGraphicsContext saveGraphicsState];
	NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
	[image drawInRect:drawRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];

	if (tintColor) {
		CGContextSetBlendMode(context, kCGBlendModeSourceAtop);
		CGContextSetFillColorWithColor(context, tintColor.CGColor);
		CGContextFillRect(context, CGRectMake(0, 0, width, height));
	}
	[NSGraphicsContext restoreGraphicsState];

	CGContextRelease(context);
	return target;
}

NSColor* symbolTintColor() {
	__block NSColor* resolvedColor = NSColor.whiteColor;
	[NSApp.effectiveAppearance performAsCurrentDrawingAppearance:^{
		resolvedColor = [NSColor.labelColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
	}];
	return resolvedColor;
}

const QHash<QString, QString>& symbolNamesByIconName() {
	static const QHash<QString, QString> symbolNames {
	    {QStringLiteral("audio-volume-muted"), QStringLiteral("speaker.slash.fill")},
	    {QStringLiteral("audio-volume-low"), QStringLiteral("speaker.wave.1.fill")},
	    {QStringLiteral("audio-volume-medium"), QStringLiteral("speaker.wave.2.fill")},
	    {QStringLiteral("audio-volume-high"), QStringLiteral("speaker.wave.3.fill")},
	    {QStringLiteral("audio-volume-overamplified"), QStringLiteral("speaker.wave.3.fill")},
	    {QStringLiteral("audio-input-microphone"), QStringLiteral("mic.fill")},
	    {QStringLiteral("audio-input-microphone-muted"), QStringLiteral("mic.slash.fill")},
	    {QStringLiteral("microphone-sensitivity-muted"), QStringLiteral("mic.slash.fill")},
	    {QStringLiteral("microphone-sensitivity-low"), QStringLiteral("mic.fill")},
	    {QStringLiteral("microphone-sensitivity-medium"), QStringLiteral("mic.fill")},
	    {QStringLiteral("microphone-sensitivity-high"), QStringLiteral("mic.fill")},
	    {QStringLiteral("audio-headphones"), QStringLiteral("headphones")},
	    {QStringLiteral("audio-headset"), QStringLiteral("headphones")},
	    {QStringLiteral("audio-headset-bluetooth"), QStringLiteral("headphones")},
	    {QStringLiteral("audio-speakers"), QStringLiteral("hifispeaker.fill")},
	    {QStringLiteral("audio-card"), QStringLiteral("hifispeaker.fill")},
	    {QStringLiteral("network-wireless"), QStringLiteral("wifi")},
	    {QStringLiteral("network-wireless-connected"), QStringLiteral("wifi")},
	    {QStringLiteral("network-wireless-acquiring"), QStringLiteral("wifi")},
	    {QStringLiteral("network-wireless-signal-excellent"), QStringLiteral("wifi")},
	    {QStringLiteral("network-wireless-signal-good"), QStringLiteral("wifi")},
	    {QStringLiteral("network-wireless-signal-ok"), QStringLiteral("wifi")},
	    {QStringLiteral("network-wireless-signal-weak"), QStringLiteral("wifi")},
	    {QStringLiteral("network-wireless-signal-none"), QStringLiteral("wifi.exclamationmark")},
	    {QStringLiteral("network-wireless-offline"), QStringLiteral("wifi.slash")},
	    {QStringLiteral("network-wireless-disabled"), QStringLiteral("wifi.slash")},
	    {QStringLiteral("network-wireless-disconnected"), QStringLiteral("wifi.slash")},
	    {QStringLiteral("network-wired"), QStringLiteral("cable.connector")},
	    {QStringLiteral("network-wired-disconnected"), QStringLiteral("network.slash")},
	    {QStringLiteral("network-offline"), QStringLiteral("network.slash")},
	    {QStringLiteral("network-vpn"), QStringLiteral("lock.shield.fill")},
	    {QStringLiteral("bluetooth"), QStringLiteral("antenna.radiowaves.left.and.right")},
	    {QStringLiteral("bluetooth-active"), QStringLiteral("antenna.radiowaves.left.and.right")},
	    {QStringLiteral("bluetooth-disabled"), QStringLiteral("antenna.radiowaves.left.and.right.slash")},
	    {QStringLiteral("bluetooth-disconnected"), QStringLiteral("antenna.radiowaves.left.and.right.slash")},
	    {QStringLiteral("display-brightness"), QStringLiteral("sun.max.fill")},
	    {QStringLiteral("night-light"), QStringLiteral("moon.fill")},
	    {QStringLiteral("notification"), QStringLiteral("bell.fill")},
	    {QStringLiteral("notifications"), QStringLiteral("bell.fill")},
	    {QStringLiteral("preferences-system-notifications"), QStringLiteral("bell.fill")},
	    {QStringLiteral("notifications-disabled"), QStringLiteral("bell.slash.fill")},
	    {QStringLiteral("system-lock-screen"), QStringLiteral("lock.fill")},
	    {QStringLiteral("system-reboot"), QStringLiteral("arrow.clockwise")},
	    {QStringLiteral("system-log-out"), QStringLiteral("rectangle.portrait.and.arrow.right")},
	    {QStringLiteral("system-suspend"), QStringLiteral("moon.zzz.fill")},
	    {QStringLiteral("system-hibernate"), QStringLiteral("moon.zzz.fill")},
	    {QStringLiteral("system-shutdown"), QStringLiteral("power")},
	    {QStringLiteral("system-search"), QStringLiteral("magnifyingglass")},
	    {QStringLiteral("preferences-system"), QStringLiteral("gearshape.fill")},
	    {QStringLiteral("input-mouse"), QStringLiteral("computermouse.fill")},
	    {QStringLiteral("input-gaming"), QStringLiteral("gamecontroller.fill")},
	    {QStringLiteral("input-tablet"), QStringLiteral("pencil.tip")},
	    {QStringLiteral("input-keyboard"), QStringLiteral("keyboard.fill")},
	    {QStringLiteral("input-touchpad"), QStringLiteral("rectangle.and.hand.point.up.left.fill")},
	    {QStringLiteral("ac-adapter"), QStringLiteral("powerplug.fill")},
	    {QStringLiteral("media-playback-start"), QStringLiteral("play.fill")},
	    {QStringLiteral("media-playback-pause"), QStringLiteral("pause.fill")},
	    {QStringLiteral("media-playback-stop"), QStringLiteral("stop.fill")},
	    {QStringLiteral("media-skip-forward"), QStringLiteral("forward.end.fill")},
	    {QStringLiteral("media-skip-backward"), QStringLiteral("backward.end.fill")},
	    {QStringLiteral("media-seek-forward"), QStringLiteral("forward.fill")},
	    {QStringLiteral("media-seek-backward"), QStringLiteral("backward.fill")},
	    {QStringLiteral("media-playlist-shuffle"), QStringLiteral("shuffle")},
	    {QStringLiteral("media-playlist-repeat"), QStringLiteral("repeat")},
	    {QStringLiteral("weather-clear"), QStringLiteral("sun.max.fill")},
	    {QStringLiteral("weather-clear-night"), QStringLiteral("moon.stars.fill")},
	    {QStringLiteral("weather-few-clouds"), QStringLiteral("cloud.sun.fill")},
	    {QStringLiteral("weather-few-clouds-night"), QStringLiteral("cloud.moon.fill")},
	    {QStringLiteral("weather-overcast"), QStringLiteral("cloud.fill")},
	    {QStringLiteral("weather-showers"), QStringLiteral("cloud.rain.fill")},
	    {QStringLiteral("weather-showers-scattered"), QStringLiteral("cloud.drizzle.fill")},
	    {QStringLiteral("weather-snow"), QStringLiteral("cloud.snow.fill")},
	    {QStringLiteral("weather-storm"), QStringLiteral("cloud.bolt.rain.fill")},
	    {QStringLiteral("weather-fog"), QStringLiteral("cloud.fog.fill")},
	    {QStringLiteral("user-trash"), QStringLiteral("trash.fill")},
	    {QStringLiteral("edit-delete"), QStringLiteral("trash")},
	    {QStringLiteral("edit-copy"), QStringLiteral("doc.on.doc")},
	    {QStringLiteral("edit-paste"), QStringLiteral("doc.on.clipboard")},
	    {QStringLiteral("edit-clear"), QStringLiteral("xmark.circle.fill")},
	    {QStringLiteral("edit-find"), QStringLiteral("magnifyingglass")},
	    {QStringLiteral("window-close"), QStringLiteral("xmark")},
	    {QStringLiteral("view-refresh"), QStringLiteral("arrow.clockwise")},
	    {QStringLiteral("list-add"), QStringLiteral("plus")},
	    {QStringLiteral("list-remove"), QStringLiteral("minus")},
	    {QStringLiteral("go-home"), QStringLiteral("house.fill")},
	    {QStringLiteral("object-select"), QStringLiteral("checkmark")},
	    {QStringLiteral("emblem-ok"), QStringLiteral("checkmark.circle.fill")},
	    {QStringLiteral("dialog-information"), QStringLiteral("info.circle.fill")},
	    {QStringLiteral("dialog-warning"), QStringLiteral("exclamationmark.triangle.fill")},
	    {QStringLiteral("dialog-error"), QStringLiteral("xmark.octagon.fill")},
	    {QStringLiteral("dialog-question"), QStringLiteral("questionmark.circle.fill")},
	    {QStringLiteral("camera-web"), QStringLiteral("camera.fill")},
	    {QStringLiteral("video-display"), QStringLiteral("display")},
	    {QStringLiteral("computer"), QStringLiteral("desktopcomputer")},
	    {QStringLiteral("phone"), QStringLiteral("iphone")},
	    {QStringLiteral("printer"), QStringLiteral("printer.fill")},
	    {QStringLiteral("folder"), QStringLiteral("folder.fill")},
	    {QStringLiteral("battery-missing"), QStringLiteral("battery.0percent")},
	};
	return symbolNames;
}

QString batterySymbolName(const QString& iconName) {
	static const QRegularExpression levelPattern(
	    QStringLiteral("^battery-level-(\\d+)(-charging|-charged)?$")
	);
	static const QHash<QString, int> namedLevels {
	    {QStringLiteral("full"), 100},
	    {QStringLiteral("good"), 70},
	    {QStringLiteral("low"), 30},
	    {QStringLiteral("caution"), 10},
	    {QStringLiteral("empty"), 0},
	};

	int level = -1;
	auto isCharging = false;

	if (auto match = levelPattern.match(iconName); match.hasMatch()) {
		level = match.captured(1).toInt();
		isCharging = match.captured(2) == QStringLiteral("-charging");
	} else if (iconName.startsWith(QStringLiteral("battery-"))) {
		auto descriptor = iconName.sliced(8);
		isCharging = descriptor.endsWith(QStringLiteral("-charging"));
		if (isCharging) descriptor.chop(9);
		level = namedLevels.value(descriptor, -1);
	}

	if (level < 0) return {};
	if (isCharging) return QStringLiteral("battery.100percent.bolt");
	if (level <= 10) return QStringLiteral("battery.0percent");
	if (level <= 37) return QStringLiteral("battery.25percent");
	if (level <= 62) return QStringLiteral("battery.50percent");
	if (level <= 87) return QStringLiteral("battery.75percent");
	return QStringLiteral("battery.100percent");
}

std::optional<QIcon> symbolIcon(const QString& iconName) {
	auto baseName = iconName;
	if (baseName.endsWith(QStringLiteral("-symbolic"))) baseName.chop(9);

	auto symbolName = symbolNamesByIconName().value(baseName);
	if (symbolName.isEmpty()) symbolName = batterySymbolName(baseName);
	if (symbolName.isEmpty()) return std::nullopt;

	NSString* nativeSymbolName = symbolName.toNSString();
	if (![NSImage imageWithSystemSymbolName:nativeSymbolName accessibilityDescription:nil]) {
		return std::nullopt;
	}

	return QIcon(new RenderedIconEngine([nativeSymbolName](const QSize& pixelSize) {
		auto pointSize = static_cast<CGFloat>(std::min(pixelSize.width(), pixelSize.height())) * 0.8;
		NSImageSymbolConfiguration* configuration =
		    [NSImageSymbolConfiguration configurationWithPointSize:pointSize weight:NSFontWeightRegular];
		NSImage* image = [[NSImage imageWithSystemSymbolName:nativeSymbolName accessibilityDescription:nil]
		    imageWithSymbolConfiguration:configuration];
		return renderImage(image, pixelSize, symbolTintColor());
	}));
}

const QHash<QString, QString>& applicationPathsByKey() {
	static const QHash<QString, QString> applicationPaths = []() {
		QHash<QString, QString> paths;
		NSFileManager* fileManager = NSFileManager.defaultManager;

		NSArray<NSString*>* searchDirectories = @[
			@"/Applications",
			@"/Applications/Utilities",
			@"/System/Applications",
			@"/System/Applications/Utilities",
			@"/System/Library/CoreServices",
			[NSHomeDirectory() stringByAppendingPathComponent:@"Applications"],
		];

		auto addKey = [&paths](NSString* key, NSString* path) {
			if (key.length == 0) return;
			auto normalizedKey = QString::fromNSString(key).toLower();
			if (!paths.contains(normalizedKey)) paths.insert(normalizedKey, QString::fromNSString(path));
		};

		for (NSString* directory in searchDirectories) {
			for (NSString* entry in [fileManager contentsOfDirectoryAtPath:directory error:nil]) {
				if (![entry.pathExtension isEqualToString:@"app"]) continue;

				NSString* path = [directory stringByAppendingPathComponent:entry];
				NSString* displayName = entry.stringByDeletingPathExtension;
				NSDictionary* information = [NSBundle bundleWithPath:path].infoDictionary;
				NSString* bundleIdentifier = information[@"CFBundleIdentifier"];

				addKey(displayName, path);
				addKey([displayName stringByReplacingOccurrencesOfString:@" " withString:@"-"], path);
				addKey(bundleIdentifier, path);
				addKey(bundleIdentifier.pathExtension, path);
				addKey(information[@"CFBundleExecutable"], path);
				addKey(information[@"CFBundleName"], path);
			}
		}

		return paths;
	}();
	return applicationPaths;
}

std::optional<QIcon> applicationIcon(const QString& iconName) {
	auto key = iconName.toLower();
	if (key.endsWith(QStringLiteral(".desktop"))) key.chop(8);

	auto path = applicationPathsByKey().value(key);
	if (path.isEmpty() && iconName.startsWith(u'/') && QFileInfo::exists(iconName)) path = iconName;
	if (path.isEmpty()) return std::nullopt;

	NSString* nativePath = path.toNSString();
	return QIcon(new RenderedIconEngine([nativePath](const QSize& pixelSize) {
		NSImage* image = [NSWorkspace.sharedWorkspace iconForFile:nativePath];
		return renderImage(image, pixelSize, nil);
	}));
}

}

QIcon resolveIcon(const QString& name) {
	if (name.isEmpty()) return {};

	if (name.endsWith(QStringLiteral("-symbolic"))) {
		auto plainIcon = QIcon::fromTheme(name.chopped(9));
		if (!plainIcon.isNull()) return plainIcon;
	}

	if (auto icon = symbolIcon(name)) return *icon;
	if (auto icon = applicationIcon(name)) return *icon;
	return {};
}

}

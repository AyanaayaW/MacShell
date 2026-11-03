#include "screencopy.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qquickwindow.h>
#include <qsgimagenode.h>
#include <qsgtexture.h>
#include <qsize.h>

#include "../core/qmlscreen.hpp"
#include "../window/proxywindow.hpp"
#include "toplevels.hpp"

#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

using qs::wayland::screencopy::ScreencopyView;

namespace {

Q_LOGGING_CATEGORY(logScreencopy, "quickshell.wayland.screencopy.macos", QtWarningMsg);

constexpr int LIVE_FRAMES_PER_SECOND = 30;

struct CaptureTarget {
	std::optional<CGDirectDisplayID> displayId;
	std::optional<CGWindowID> windowId;
};

std::optional<CGDirectDisplayID> displayIdForScreenName(const QString& screenName) {
	for (NSScreen* screen in NSScreen.screens) {
		if (QString::fromNSString(screen.localizedName) == screenName) {
			return [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
		}
	}
	return std::nullopt;
}

std::optional<CaptureTarget> resolveCaptureTarget(QObject* source) {
	if (!source) return std::nullopt;

	if (auto* screenInfo = qobject_cast<QuickshellScreenInfo*>(source)) {
		if (auto displayId = displayIdForScreenName(screenInfo->name())) {
			return CaptureTarget {.displayId = displayId, .windowId = std::nullopt};
		}
		return std::nullopt;
	}

	if (auto* toplevel = qobject_cast<qs::wayland::toplevel::Toplevel*>(source)) {
		return CaptureTarget {.displayId = std::nullopt, .windowId = toplevel->windowId()};
	}

	if (auto* proxyWindow = ProxyWindowBase::forObject(source)) {
		auto* backingWindow = proxyWindow->backingWindow();
		if (backingWindow && backingWindow->handle()) {
			auto* view = (__bridge NSView*)reinterpret_cast<void*>(backingWindow->winId());
			return CaptureTarget {
			    .displayId = std::nullopt,
			    .windowId = static_cast<CGWindowID>(view.window.windowNumber),
			};
		}
		return std::nullopt;
	}

	auto address = source->property("address").toString();
	if (address.startsWith(QStringLiteral("0x"))) address = address.sliced(2);
	auto parsedAddress = false;
	auto windowId = address.toUInt(&parsedAddress, 16);
	if (parsedAddress && windowId != 0) {
		return CaptureTarget {.displayId = std::nullopt, .windowId = static_cast<CGWindowID>(windowId)};
	}

	return std::nullopt;
}

QImage imageFromPixelBuffer(CVPixelBufferRef pixelBuffer) {
	CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

	auto width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
	auto height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));
	auto bytesPerRow = static_cast<qsizetype>(CVPixelBufferGetBytesPerRow(pixelBuffer));
	const auto* baseAddress = static_cast<const uchar*>(CVPixelBufferGetBaseAddress(pixelBuffer));

	QImage image = QImage(baseAddress, width, height, bytesPerRow, QImage::Format_ARGB32_Premultiplied).copy();

	CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
	return image;
}

QImage imageFromCGImage(CGImageRef cgImage) {
	auto width = static_cast<int>(CGImageGetWidth(cgImage));
	auto height = static_cast<int>(CGImageGetHeight(cgImage));

	QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);

	CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGContextRef context = CGBitmapContextCreate(
	    image.bits(),
	    static_cast<size_t>(width),
	    static_cast<size_t>(height),
	    8,
	    static_cast<size_t>(image.bytesPerLine()),
	    colorSpace,
	    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host
	);
	CGColorSpaceRelease(colorSpace);
	if (!context) return {};

	CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
	CGContextRelease(context);
	return image;
}

QSize fitWithin(const QSize& source, const QSizeF& constraint, qreal scale) {
	if (constraint.width() <= 0 || constraint.height() <= 0 || source.isEmpty()) return source;

	auto target = QSizeF(source).scaled(constraint * scale, Qt::KeepAspectRatio);
	return QSize(std::max(1, static_cast<int>(std::round(target.width()))), std::max(1, static_cast<int>(std::round(target.height()))));
}

}

@interface MacShellCaptureSession: NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, strong) SCStream* stream;
@property(atomic) BOOL invalidated;
@end

@implementation MacShellCaptureSession {
	ScreencopyView* view;
}

- (instancetype)initWithView:(ScreencopyView*)targetView {
	self = [super init];
	if (self) self->view = targetView;
	return self;
}

- (void)deliver:(const QImage&)image {
	if (!self.invalidated) self->view->deliverFrame(image);
}

- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
	if (type != SCStreamOutputTypeScreen || self.invalidated) return;

	CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
	if (attachments && CFArrayGetCount(attachments) > 0) {
		NSDictionary* frameInfo = (__bridge NSDictionary*)CFArrayGetValueAtIndex(attachments, 0);
		NSNumber* status = frameInfo[SCStreamFrameInfoStatus];
		if (status && status.integerValue != SCFrameStatusComplete) return;
	}

	CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
	if (imageBuffer) [self deliver:imageFromPixelBuffer(imageBuffer)];
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
	if (self.invalidated) return;
	QMetaObject::invokeMethod(self->view, [targetView = self->view]() { emit targetView->stopped(); }, Qt::QueuedConnection);
}

- (void)stop {
	self.invalidated = YES;
	[self.stream stopCaptureWithCompletionHandler:nil];
	self.stream = nil;
}

@end

namespace qs::wayland::screencopy {

ScreencopyView::ScreencopyView(QQuickItem* parent): QQuickItem(parent) {
	this->setFlag(QQuickItem::ItemHasContents);
	QObject::connect(this, &ScreencopyView::constraintSizeChanged, this, [this]() {
		this->updateImplicitSize();
		this->restartCapture();
	});
}

ScreencopyView::~ScreencopyView() { this->stopCapture(); }

void ScreencopyView::setCaptureSource(QObject* captureSource) {
	if (captureSource == this->mCaptureSource) return;
	this->mCaptureSource = captureSource;
	emit this->captureSourceChanged();
	this->restartCapture();
}

void ScreencopyView::setPaintCursors(bool paintCursors) {
	if (paintCursors == this->mPaintCursors) return;
	this->mPaintCursors = paintCursors;
	emit this->paintCursorsChanged();
	this->restartCapture();
}

void ScreencopyView::setLive(bool live) {
	if (live == this->mLive) return;
	this->mLive = live;
	emit this->liveChanged();
	this->restartCapture();
}

void ScreencopyView::stopCapture() {
	if (!this->captureSession) return;
	MacShellCaptureSession* session = (__bridge_transfer MacShellCaptureSession*)this->captureSession;
	[session stop];
	this->captureSession = nullptr;
}

void ScreencopyView::restartCapture() {
	this->stopCapture();

	auto target = resolveCaptureTarget(this->mCaptureSource);
	if (!target) {
		this->bHasContent = false;
		return;
	}

	MacShellCaptureSession* session = [[MacShellCaptureSession alloc] initWithView:this];
	this->captureSession = (__bridge_retained void*)session;

	auto live = this->mLive;
	auto paintCursor = this->mPaintCursors;
	auto constraint = this->bConstraintSize.value();
	QPointer<ScreencopyView> viewGuard = this;

	[SCShareableContent getShareableContentExcludingDesktopWindows:NO
	                                           onScreenWindowsOnly:NO
	                                             completionHandler:^(SCShareableContent* content, NSError* error) {
		if (error || !content || session.invalidated) {
			if (error) qCWarning(logScreencopy) << "Screen capture unavailable:" << QString::fromNSString(error.localizedDescription);
			return;
		}

		SCContentFilter* filter = nil;
		QSize sourcePixelSize;
		qreal scale = NSScreen.mainScreen.backingScaleFactor;

		if (target->displayId) {
			for (SCDisplay* display in content.displays) {
				if (display.displayID != *target->displayId) continue;
				filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
				sourcePixelSize = QSize(
				    static_cast<int>(CGDisplayPixelsWide(display.displayID) * scale),
				    static_cast<int>(CGDisplayPixelsHigh(display.displayID) * scale)
				);
			}
		} else if (target->windowId) {
			for (SCWindow* window in content.windows) {
				if (window.windowID != *target->windowId) continue;
				filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
				sourcePixelSize = QSize(
				    static_cast<int>(window.frame.size.width * scale),
				    static_cast<int>(window.frame.size.height * scale)
				);
			}
		}

		if (!filter || sourcePixelSize.isEmpty()) return;

		auto captureSize = fitWithin(sourcePixelSize, constraint, scale);
		SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
		configuration.width = static_cast<size_t>(captureSize.width());
		configuration.height = static_cast<size_t>(captureSize.height());
		configuration.pixelFormat = kCVPixelFormatType_32BGRA;
		configuration.showsCursor = paintCursor;
		configuration.minimumFrameInterval = CMTimeMake(1, LIVE_FRAMES_PER_SECOND);

		QMetaObject::invokeMethod(
		    viewGuard.data(),
		    [viewGuard, sourcePixelSize]() {
			    if (!viewGuard) return;
			    viewGuard->bSourceSize = sourcePixelSize;
			    viewGuard->updateImplicitSize();
		    },
		    Qt::QueuedConnection
		);

		if (live) {
			session.stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:session];
			dispatch_queue_t queue = dispatch_queue_create("MacShell.Screencopy", DISPATCH_QUEUE_SERIAL);
			NSError* outputError = nil;
			[session.stream addStreamOutput:session type:SCStreamOutputTypeScreen sampleHandlerQueue:queue error:&outputError];
			[session.stream startCaptureWithCompletionHandler:^(NSError* startError) {
				if (startError) qCWarning(logScreencopy) << "Could not start live capture:" << QString::fromNSString(startError.localizedDescription);
			}];
		} else {
			[SCScreenshotManager captureImageWithFilter:filter
			                              configuration:configuration
			                          completionHandler:^(CGImageRef image, NSError* captureError) {
				                          if (captureError || !image) return;
				                          [session deliver:imageFromCGImage(image)];
			                          }];
		}
	}];
}

void ScreencopyView::captureFrame() {
	if (this->mLive) return;
	this->restartCapture();
}

void ScreencopyView::deliverFrame(const QImage& frame) {
	{
		std::lock_guard lock(this->frameMutex);
		this->pendingFrame = frame;
	}
	QMetaObject::invokeMethod(this, &ScreencopyView::applyLatestFrame, Qt::QueuedConnection);
}

void ScreencopyView::applyLatestFrame() {
	{
		std::lock_guard lock(this->frameMutex);
		if (this->pendingFrame.isNull()) return;
		this->displayedFrame = std::move(this->pendingFrame);
		this->pendingFrame = QImage();
	}

	this->textureNeedsUpdate = true;
	this->bHasContent = true;
	this->update();
}

void ScreencopyView::updateImplicitSize() {
	auto sourceSize = QSizeF(this->bSourceSize.value());
	auto* currentWindow = this->window();
	auto devicePixelRatio = currentWindow ? currentWindow->devicePixelRatio() : 1.0;
	auto logicalSize = sourceSize / devicePixelRatio;

	auto constraint = this->bConstraintSize.value();
	if (constraint.width() > 0 && constraint.height() > 0) {
		logicalSize = logicalSize.scaled(constraint, Qt::KeepAspectRatio);
	}

	this->setImplicitSize(logicalSize.width(), logicalSize.height());
}

QSGNode* ScreencopyView::updatePaintNode(QSGNode* oldNode, [[maybe_unused]] UpdatePaintNodeData* data) {
	auto* imageNode = static_cast<QSGImageNode*>(oldNode);
	if (this->displayedFrame.isNull()) {
		delete imageNode;
		return nullptr;
	}

	if (!imageNode) {
		imageNode = this->window()->createImageNode();
		imageNode->setOwnsTexture(true);
		this->textureNeedsUpdate = true;
	}

	if (this->textureNeedsUpdate) {
		imageNode->setTexture(this->window()->createTextureFromImage(this->displayedFrame));
		this->textureNeedsUpdate = false;
	}

	auto frameSize = QSizeF(this->displayedFrame.size());
	auto targetRect = QRectF(QPointF(0, 0), frameSize.scaled(this->size(), Qt::KeepAspectRatio));
	targetRect.moveCenter(this->boundingRect().center());
	imageNode->setRect(targetRect);
	imageNode->setFiltering(QSGTexture::Linear);
	return imageNode;
}

}

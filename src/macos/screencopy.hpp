#pragma once

#include <mutex>

#include <qimage.h>
#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qquickitem.h>
#include <qsize.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::wayland::screencopy {

class ScreencopyView: public QQuickItem {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(QObject* captureSource READ captureSource WRITE setCaptureSource NOTIFY captureSourceChanged);
	Q_PROPERTY(bool paintCursor READ paintCursors WRITE setPaintCursors NOTIFY paintCursorsChanged);
	Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged);
	Q_PROPERTY(bool hasContent READ default NOTIFY hasContentChanged BINDABLE bindableHasContent);
	Q_PROPERTY(QSize sourceSize READ default NOTIFY sourceSizeChanged BINDABLE bindableSourceSize);
	Q_PROPERTY(QSizeF constraintSize READ default WRITE default NOTIFY constraintSizeChanged BINDABLE bindableConstraintSize);

public:
	explicit ScreencopyView(QQuickItem* parent = nullptr);
	~ScreencopyView() override;
	Q_DISABLE_COPY_MOVE(ScreencopyView);

	Q_INVOKABLE void captureFrame();

	[[nodiscard]] QObject* captureSource() const { return this->mCaptureSource; }
	void setCaptureSource(QObject* captureSource);
	[[nodiscard]] bool paintCursors() const { return this->mPaintCursors; }
	void setPaintCursors(bool paintCursors);
	[[nodiscard]] bool live() const { return this->mLive; }
	void setLive(bool live);

	[[nodiscard]] QBindable<bool> bindableHasContent() { return &this->bHasContent; }
	[[nodiscard]] QBindable<QSize> bindableSourceSize() { return &this->bSourceSize; }
	[[nodiscard]] QBindable<QSizeF> bindableConstraintSize() { return &this->bConstraintSize; }

	void deliverFrame(const QImage& frame);

signals:
	void stopped();
	void captureSourceChanged();
	void paintCursorsChanged();
	void liveChanged();
	void hasContentChanged();
	void sourceSizeChanged();
	void constraintSizeChanged();

protected:
	QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
	void restartCapture();
	void stopCapture();
	void applyLatestFrame();
	void updateImplicitSize();

	QPointer<QObject> mCaptureSource;
	bool mPaintCursors = false;
	bool mLive = false;
	void* captureSession = nullptr;

	std::mutex frameMutex;
	QImage pendingFrame;
	QImage displayedFrame;
	bool textureNeedsUpdate = false;

	Q_OBJECT_BINDABLE_PROPERTY(ScreencopyView, bool, bHasContent, &ScreencopyView::hasContentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(ScreencopyView, QSize, bSourceSize, &ScreencopyView::sourceSizeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(ScreencopyView, QSizeF, bConstraintSize, &ScreencopyView::constraintSizeChanged);
};

}

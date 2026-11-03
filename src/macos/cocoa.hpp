#pragma once

#include <qimage.h>
#include <qwindowdefs.h>

namespace qs::macos {

void configurePanel(WId viewHandle, int layer);
void focusPanel(WId viewHandle);
void configureLockWindow(WId viewHandle);
void setBackgroundBlur(WId viewHandle, const QImage& maskImage);
void removeBackgroundBlur(WId viewHandle);
void beginLockdown();
void endLockdown();
bool isLockdownActive();
void setAccessoryApp();
void hideSystemUi();
void restoreSystemUi();

}

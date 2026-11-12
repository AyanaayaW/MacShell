#pragma once

#include <qimage.h>
#include <qwindowdefs.h>

namespace qs::macos {

void configurePanel(WId viewHandle, int layer);
void focusPanel(WId viewHandle);
void unfocusPanel(WId viewHandle);
void configureLockWindow(WId viewHandle);
void setWindowBlur(WId viewHandle, int radius);
void setBackgroundBlur(WId viewHandle, const QImage& maskImage);
void removeBackgroundBlur(WId viewHandle);
void beginLockdown();
void endLockdown();
bool isLockdownActive();
void showFallbackLockShield();
void setAccessoryApp();
void hideSystemUi();
void restoreSystemUi();

}

#include "ui/NotificationManager.h"

#include "main.h"
#include <Arduino.h>
#include <climits>

NotificationManager *notificationManager = nullptr;

#if HAS_SCREEN

NotificationManager::NotificationManager() : concurrency::OSThread("NotificationOverlay") {}

void NotificationManager::show(const std::string &title, const std::string &body, uint32_t durationMs)
{
    if (!screen)
        return;

    title_ = title;
    body_ = body;

    if (title_.empty() && !body_.empty())
        title_ = body_;

    if (body_.empty() && !title_.empty())
        body_ = title_;

    if (durationMs == 0)
        durationMs = 4000;

    hideAt_ = millis() + durationMs;

    screen->startAlert(drawAlert);
    setIntervalFromNow(0);
}

void NotificationManager::clear()
{
    hideAt_ = 0;
    title_.clear();
    body_.clear();
    if (screen)
        screen->endAlert();
}

int32_t NotificationManager::runOnce()
{
    if (!hideAt_)
        return INT32_MAX;

    uint32_t now = millis();
    if ((int32_t)(hideAt_ - now) <= 0) {
        clear();
        return INT32_MAX;
    }

    uint32_t wait = hideAt_ - now;
    setIntervalFromNow(wait);
    return wait;
}

void NotificationManager::drawAlert(OLEDDisplay *display, OLEDDisplayUiState *, int16_t x, int16_t y)
{
    if (!notificationManager)
        return;

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x + 6, y + 4, notificationManager->title_.c_str());

    display->setFont(FONT_SMALL);
    display->drawStringMaxWidth(x + 6, y + 26, display->width() - 12, notificationManager->body_.c_str());
}

#endif

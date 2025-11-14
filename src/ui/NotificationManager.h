#pragma once

#include "configuration.h"
#include "concurrency/OSThread.h"
#include <string>

class OLEDDisplay;
struct OLEDDisplayUiState;

#if HAS_SCREEN
class NotificationManager : public concurrency::OSThread
{
  public:
    NotificationManager();

    void show(const std::string &title, const std::string &body, uint32_t durationMs = 5000);
    void clear();
    bool isActive() const { return hideAt_ != 0; }

  protected:
    int32_t runOnce() override;

  private:
    static void drawAlert(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    std::string title_;
    std::string body_;
    uint32_t hideAt_ = 0;
};
#else
class NotificationManager
{
  public:
    void show(const std::string &, const std::string &, uint32_t = 0) {}
    void clear() {}
    bool isActive() const { return false; }
};
#endif

extern NotificationManager *notificationManager;

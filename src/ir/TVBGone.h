#pragma once

#include "configuration.h"

#if defined(ARDUINO_ARCH_ESP32)
#include "concurrency/OSThread.h"
#include <Arduino.h>

class TvBGone : public concurrency::OSThread {
  public:
    TvBGone();
    void start();
    void stop();
    bool isActive() const { return active; }

  protected:
    int32_t runOnce() override;

  private:
    static constexpr uint8_t kIrChannel = 0;
    static constexpr uint8_t kIrPin = 16;
    static constexpr uint8_t kCarrierDutyHigh = 192;
    static constexpr unsigned kCarrierResolution = 8;

    void transmitSequence();
    void transmitCodeElement(uint16_t ontime, uint16_t offtime, bool pwm);
    void configureCarrier(uint32_t frequency);
    void stopCarrier();

    bool active = false;
    bool running = false;
};
#else
class TvBGone {
  public:
    void start() {}
    void stop() {}
    bool isActive() const { return false; }
};
#endif

extern TvBGone *tvBGone;

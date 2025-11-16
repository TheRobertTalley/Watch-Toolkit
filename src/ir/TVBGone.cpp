#include "ir/TVBGone.h"

#if defined(ARDUINO_ARCH_ESP32)
#include "ir/tvcodes.h"
#include <Arduino.h>
#include <pgmspace.h>

TvBGone *tvBGone = nullptr;

namespace
{
constexpr uint32_t kMillisecondsBetweenCodes = 60;
constexpr uint32_t kCodePairScale = 10;

size_t codeEntryCount()
{
    return (sizeof(codes_eu) / sizeof(codes_eu[0])) / 3;
}
} // namespace

TvBGone::TvBGone() : concurrency::OSThread("TvBGone")
{
    pinMode(kIrPin, OUTPUT);
    digitalWrite(kIrPin, LOW);
    ledcAttachPin(kIrPin, kIrChannel);
    setInterval(UINT32_MAX);
}

void TvBGone::start()
{
    if (active)
        return;
    running = false;
    active = true;
    setInterval(0);
}

void TvBGone::stop()
{
    if (!active)
        return;
    active = false;
    running = false;
    stopCarrier();
    setInterval(UINT32_MAX);
}

int32_t TvBGone::runOnce()
{
    if (!active || running)
        return UINT32_MAX;
    running = true;
    transmitSequence();
    stop();
    return UINT32_MAX;
}

void TvBGone::configureCarrier(uint32_t frequency)
{
    if (frequency == 0) {
        stopCarrier();
        return;
    }
    ledcSetup(kIrChannel, frequency, kCarrierResolution);
    ledcAttachPin(kIrPin, kIrChannel);
}

void TvBGone::stopCarrier()
{
    ledcWrite(kIrChannel, 0);
}

void TvBGone::transmitCodeElement(uint16_t ontime, uint16_t offtime, bool pwm)
{
    if (pwm) {
        ledcWrite(kIrChannel, kCarrierDutyHigh);
    }
    delayMicroseconds(static_cast<uint32_t>(ontime) * kCodePairScale);
    ledcWrite(kIrChannel, 0);
    delayMicroseconds(static_cast<uint32_t>(offtime) * kCodePairScale);
}

void TvBGone::transmitSequence()
{
    const int *codes = codes_eu;
    const unsigned short *codes2 = codes2_eu;
    size_t entryCount = codeEntryCount();

    for (size_t i = 0; i < entryCount && active; ++i) {
        const int *codeEntry = codes + (i * 3);
        uint32_t frequency = pgm_read_dword(codeEntry);
        uint32_t codeLength = pgm_read_dword(codeEntry + 1);
        uint32_t codeOffset = pgm_read_dword(codeEntry + 2);

        if (frequency)
            configureCarrier(frequency);
        else
            stopCarrier();

        const unsigned short *sequence = codes2 + codeOffset;
        for (uint32_t position = 0; position < codeLength && active; position += 2) {
            uint16_t ontime = pgm_read_word(sequence + position);
            uint16_t offtime = pgm_read_word(sequence + position + 1);
            transmitCodeElement(ontime, offtime, frequency != 0);
        }

        stopCarrier();
        delay(kMillisecondsBetweenCodes);
        delay(kMillisecondsBetweenCodes);
    }
}

#else
TvBGone *tvBGone = nullptr;
#endif

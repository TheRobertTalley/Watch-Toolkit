#pragma once

#include <stdint.h>

namespace audio
{

/**
 * Provides a minimal interface for generating tones on the watch hardware.
 * For the T-Watch S3 this uses the I2S-connected Max98357A amplifier.
 * On other hardware it falls back to the Arduino tone/noTone helpers.
 */
void toneOutputPlay(uint32_t frequencyHz);
void toneOutputStop();

} // namespace audio


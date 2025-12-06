"""
MicroPython demo for the LilyGo T-Watch S3.

Pin mapping is taken from variants/t-watch-s3/variant.h in this repo.
Dependencies (install once while the watch is connected):
    mpremote mip install "github:russhughes/st7789py_mpy"

Copy this file to the watch (for example with `mpremote fs cp micropython/twatch_s3_demo.py :main.py`)
and reset. It lights the backlight, draws a title screen, and paints coloured blocks wherever
you touch the panel.
"""

from machine import I2C, PWM, SPI, Pin
import framebuf
import time
import st7789

# Display geometry
WIDTH = 240
HEIGHT = 240
ROTATION = 2  # Matches the Meshtastic/LovyanGFX rotation used in variant.h

# ST7789 pins (SPI3 host)
TFT_CS = 12
TFT_DC = 38
TFT_SCK = 18
TFT_MOSI = 13
TFT_BL = 45

# FT6x36 capacitive touch on I2C1
TOUCH_SDA = 39
TOUCH_SCL = 40
TOUCH_INT = 16
TOUCH_ADDR = 0x38

BACKLIGHT_PERCENT = 80


def rgb565(r, g, b):
    """Convert 0-255 RGB to 16-bit."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def clamp(val, low, high):
    return max(low, min(high, val))


class FT6x36:
    def __init__(self, i2c):
        self.i2c = i2c

    def read(self):
        """Return (x, y) in panel coordinates or None when idle."""
        try:
            data = self.i2c.readfrom_mem(TOUCH_ADDR, 0x02, 5)
        except OSError:
            return None

        if (data[0] & 0x0F) == 0:
            return None

        x = ((data[1] & 0x0F) << 8) | data[2]
        y = ((data[3] & 0x0F) << 8) | data[4]
        return x, y


def map_touch(pt):
    """Rotate raw touch coordinates to match the display rotation."""
    if pt is None:
        return None

    x, y = pt
    x = clamp(x, 0, WIDTH - 1)
    y = clamp(y, 0, HEIGHT - 1)

    if ROTATION == 0:
        return x, y
    if ROTATION == 1:
        return y, WIDTH - x - 1
    if ROTATION == 2:
        return WIDTH - x - 1, HEIGHT - y - 1
    if ROTATION == 3:
        return HEIGHT - y - 1, x
    return x, y


def set_backlight(pwm, percent):
    percent = clamp(percent, 0, 100)
    pwm.duty_u16(int((percent / 100) * 65535))


def draw_dot(fb, x, y, colour):
    size = 12
    half = size // 2
    fb.fill_rect(
        clamp(x - half, 0, WIDTH - size),
        clamp(y - half, 0, HEIGHT - size),
        size,
        size,
        colour,
    )


def main():
    spi = SPI(
        2,
        baudrate=40_000_000,
        polarity=1,
        phase=1,
        sck=Pin(TFT_SCK),
        mosi=Pin(TFT_MOSI),
        miso=Pin(4),  # Unused by the panel but needed for a complete config
    )

    tft = st7789.ST7789(
        spi,
        WIDTH,
        HEIGHT,
        cs=Pin(TFT_CS, Pin.OUT, value=1),
        dc=Pin(TFT_DC, Pin.OUT),
        reset=None,
        rotation=ROTATION,
    )
    tft.init()

    backlight_pwm = PWM(Pin(TFT_BL, Pin.OUT))
    backlight_pwm.freq(5000)
    set_backlight(backlight_pwm, BACKLIGHT_PERCENT)

    i2c = I2C(1, scl=Pin(TOUCH_SCL), sda=Pin(TOUCH_SDA), freq=400_000)
    touch_irq = Pin(TOUCH_INT, Pin.IN, Pin.PULL_UP)
    touch = FT6x36(i2c)

    buffer = bytearray(WIDTH * HEIGHT * 2)
    fb = framebuf.FrameBuffer(buffer, WIDTH, HEIGHT, framebuf.RGB565)

    backgrounds = [
        rgb565(12, 34, 68),
        rgb565(0, 86, 122),
        rgb565(22, 54, 79),
        rgb565(68, 32, 80),
    ]
    paint = [
        rgb565(255, 196, 61),
        rgb565(255, 105, 97),
        rgb565(108, 235, 173),
        rgb565(168, 206, 255),
    ]

    bg_idx = 0
    paint_idx = 0

    def render_title():
        fb.fill(backgrounds[bg_idx])
        fb.text("T-Watch S3", 70, 16, rgb565(255, 255, 255))
        fb.text("MicroPython demo", 32, 32, rgb565(255, 255, 255))
        fb.text("Tap to paint blocks", 32, 56, rgb565(240, 210, 120))
        fb.text("Touch IRQ on IO16", 48, 208, rgb565(200, 200, 200))
        fb.text("Backlight {}%".format(BACKLIGHT_PERCENT), 60, 224, rgb565(200, 200, 200))
        tft.blit_buffer(buffer, 0, 0, WIDTH, HEIGHT)

    render_title()
    last_refresh = time.ticks_ms()

    while True:
        pt = None
        if touch_irq.value() == 0:
            pt = map_touch(touch.read())
        else:
            # Still poll occasionally in case the IRQ edge was missed.
            pt = map_touch(touch.read())

        if pt:
            colour = paint[paint_idx]
            paint_idx = (paint_idx + 1) % len(paint)
            draw_dot(fb, pt[0], pt[1], colour)
            tft.blit_buffer(buffer, 0, 0, WIDTH, HEIGHT)

        if time.ticks_diff(time.ticks_ms(), last_refresh) > 7000:
            bg_idx = (bg_idx + 1) % len(backgrounds)
            render_title()
            last_refresh = time.ticks_ms()

        time.sleep_ms(15)


if __name__ == "__main__":
    main()

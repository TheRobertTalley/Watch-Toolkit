#pragma once

#include <GpioLogic.h>
#include <OLEDDisplay.h>
#if HAS_TFT
#include <LovyanGFX.hpp>
#endif

/**
 * An adapter class that allows using the LovyanGFX library as if it was an OLEDDisplay implementation.
 *
 * Remaining TODO:
 * optimize display() to only draw changed pixels (see other OLED subclasses for examples)
 * Use the fast NRF52 SPI API rather than the slow standard arduino version
 *
 * turn radio back on - currently with both on spi bus is fucked? or are we leaving chip select asserted?
 */
class TFTDisplay : public OLEDDisplay
{
  public:
    /* constructor
    FIXME - the parameters are not used, just a temporary hack to keep working like the old displays
    */
    TFTDisplay(uint8_t, int, int, OLEDDISPLAY_GEOMETRY, HW_I2C);

    // Write the buffer to the display memory
    virtual void display() override { display(false); };
    virtual void display(bool fromBlank);

    // Turn the display upside down
    virtual void flipScreenVertically();

    // Touch screen (static handlers)
    static bool hasTouch(void);
    static bool getTouch(int16_t *x, int16_t *y);

    // Functions for changing display brightness
    void setDisplayBrightness(uint8_t);

    /**
     * shim to make the abstraction happy
     *
     */
    void setDetected(uint8_t detected);

    /// Override the color used for lit pixels (default comes from TFT_MESH)
    void setMeshColor(uint16_t color);
    uint16_t getMeshColor() const { return meshColor; }
#if HAS_TFT
    void drawColorString(int16_t x, int16_t y, const String &text, uint16_t color, uint16_t bg = 0);
    void fillRectColor(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawXBitmapColor(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color);
#else
    void drawColorString(int16_t, int16_t, const String &, uint16_t, uint16_t = 0) {}
    void fillRectColor(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
    void drawXBitmapColor(int16_t, int16_t, const uint8_t *, int16_t, int16_t, uint16_t) {}
#endif

    /**
     * This is normally managed entirely by TFTDisplay, but some rare applications (heltec tracker) might need to replace the
     * default GPIO behavior with something a bit more complex.
     *
     * We (cruftily) make it static so that variant.cpp can access it without needing a ptr to the TFTDisplay instance.
     */
    static GpioPin *backlightEnable;

  protected:
    static uint16_t meshColor;

    // the header size of the buffer used, e.g. for the SPI command header
    virtual int getBufferOffset(void) override { return 0; }

    // Send a command to the display (low level function)
    virtual void sendCommand(uint8_t com) override;

    // Connect to the display
    virtual bool connect() override;
};

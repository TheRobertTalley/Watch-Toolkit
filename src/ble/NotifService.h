#pragma once

#include <NimBLEDevice.h>
#include <functional>
#include <string>

class NotifServiceRxCallback;

/**
 * Gadgetbridge/Bangle.js compatible notification channel implemented on top of
 * the Nordic UART Service. Incoming JSON lines are decoded and forwarded to a
 * user supplied handler.
 */
class NotifService
{
  public:
    struct NotificationPayload {
        std::string app;
        std::string title;
        std::string body;
        bool clear = false;
    };

    using Handler = std::function<void(const NotificationPayload &payload)>;

    static NotifService &instance();

    void begin(NimBLEServer *server, Handler handler);
    void sendToPhone(const std::string &line);

  private:
    friend class NotifServiceRxCallback;

    void handleIncomingChunk(const std::string &chunk);
    void parseAndDispatch(const std::string &line);

    Handler handler_;
    NimBLECharacteristic *tx_ = nullptr;
    std::string partialLine_;
};

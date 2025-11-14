#include "ble/NotifService.h"

#include <ArduinoJson.h>
#include <cstring>
#include <utility>

namespace
{
constexpr char NUS_SERVICE_UUID[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char NUS_RX_CHAR_UUID[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char NUS_TX_CHAR_UUID[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr size_t MAX_LINE_BYTES = 768;
} // namespace

class NotifServiceRxCallback : public NimBLECharacteristicCallbacks
{
  public:
    explicit NotifServiceRxCallback(NotifService &service) : svc(service) {}

    void onWrite(NimBLECharacteristic *characteristic) override
    {
        svc.handleIncomingChunk(characteristic->getValue());
    }

  private:
    NotifService &svc;
};

NotifService &NotifService::instance()
{
    static NotifService svc;
    return svc;
}

void NotifService::begin(NimBLEServer *server, Handler handler)
{
    handler_ = std::move(handler);

    NimBLEService *svc = server->createService(NUS_SERVICE_UUID);

    tx_ = svc->createCharacteristic(NUS_TX_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic *rx =
        svc->createCharacteristic(NUS_RX_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(new NotifServiceRxCallback(*this));

    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
}

void NotifService::handleIncomingChunk(const std::string &chunk)
{
    if (chunk.empty())
        return;

    for (char ch : chunk) {
        if (ch == '\n' || ch == '\r') {
            if (!partialLine_.empty()) {
                parseAndDispatch(partialLine_);
                partialLine_.clear();
            }
        } else {
            if (partialLine_.size() >= MAX_LINE_BYTES) {
                // Drop oversized payloads instead of letting them grow forever
                partialLine_.clear();
            } else {
                partialLine_.push_back(ch);
            }
        }
    }
}

void NotifService::parseAndDispatch(const std::string &line)
{
    if (!handler_ || line.empty())
        return;

    StaticJsonDocument<MAX_LINE_BYTES> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err)
        return;

    const char *type = doc["t"];
    if (!type)
        return;

    NotificationPayload payload;

    if (strcmp(type, "notify-") == 0) {
        payload.clear = true;
        handler_(payload);
        return;
    }

    if (strcmp(type, "notify") != 0 && strcmp(type, "notify~") != 0)
        return;

    if (const char *app = doc["app"])
        payload.app = app;
    if (const char *title = doc["title"])
        payload.title = title;

    if (const char *body = doc["body"])
        payload.body = body;
    else if (const char *msg = doc["msg"])
        payload.body = msg;

    handler_(payload);
}

void NotifService::sendToPhone(const std::string &line)
{
    if (!tx_ || line.empty())
        return;
    tx_->setValue(line);
    tx_->notify();
}

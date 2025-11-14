#include "marauder/StationTracker.h"

#if HAS_WIFI && defined(ARCH_ESP32)

#include "configuration.h"
#include "DebugConfiguration.h"
#include "mesh/wifi/WiFiAPClient.h"

#include <WiFi.h>
#include <algorithm>
#include <cstdint>
#include <esp_wifi.h>

namespace marauder
{

namespace
{
constexpr uint8_t kFirstChannel = 1;
constexpr uint8_t kLastChannel = 13;
constexpr uint64_t kChannelHopIntervalUs = 400000; // 400 ms

struct RawMacHeader
{
    uint16_t frameCtrl;
    uint16_t duration;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    uint16_t seqCtrl;
} __attribute__((packed));
}

StationTracker &StationTracker::instance()
{
    static StationTracker tracker;
    return tracker;
}

StationTracker::StationTracker() = default;

void StationTracker::start()
{
    if (running)
        return;
    ensureWifiReady();
    aps.clear();
    stations.clear();
    selectedStation = SIZE_MAX;
    wifi_promiscuous_filter_t filter = {};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&StationTracker::promiscuousCb);
    esp_err_t err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK)
        LOG_WARN("Station tracker failed to start promiscuous mode (%d)", err);
    startChannelHopTimer();
    running = true;
    LOG_INFO("Station tracker started");
}

void StationTracker::stop()
{
    if (!running)
        return;
    stopChannelHopTimer();
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    running = false;
    if (restoreWifi) {
        if (isWifiAvailable()) {
            initWifi();
        } else if (previousWifiMode != WIFI_MODE_NULL) {
            esp_wifi_set_mode(previousWifiMode);
            esp_wifi_start();
        }
        restoreWifi = false;
    }
    LOG_INFO("Station tracker stopped");
}

void StationTracker::clear()
{
    aps.clear();
    stations.clear();
    selectedStation = SIZE_MAX;
}

void StationTracker::selectStation(size_t stationIndex)
{
    if (stationIndex >= stations.size()) {
        selectedStation = SIZE_MAX;
        return;
    }
    selectedStation = stationIndex;
    LOG_INFO("Selected station index %u", static_cast<unsigned>(stationIndex));
}

const TrackedStation *StationTracker::getSelectedStation() const
{
    if (selectedStation >= stations.size())
        return nullptr;
    return &stations[selectedStation];
}

void StationTracker::promiscuousCb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    auto *tracker = &StationTracker::instance();
    tracker->handlePacket(static_cast<wifi_promiscuous_pkt_t *>(buf), type);
}

void StationTracker::handlePacket(const wifi_promiscuous_pkt_t *packet, wifi_promiscuous_pkt_type_t type)
{
    if (!packet)
        return;

    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT)
        return;

    const RawMacHeader *hdr = reinterpret_cast<const RawMacHeader *>(packet->payload);
    if (!hdr)
        return;

    std::array<uint8_t, 6> bssid{};
    std::array<uint8_t, 6> station{};
    std::copy(std::begin(hdr->addr3), std::end(hdr->addr3), bssid.begin());
    std::copy(std::begin(hdr->addr2), std::end(hdr->addr2), station.begin());

    auto nowAp = bssid;
    auto apIt = std::find_if(aps.begin(), aps.end(), [&](const TrackedAccessPoint &ap) {
        return ap.bssid == nowAp;
    });
    if (apIt == aps.end()) {
        TrackedAccessPoint ap;
        ap.bssid = nowAp;
        ap.channel = packet->rx_ctrl.channel;
        aps.push_back(ap);
        apIt = std::prev(aps.end());
    }

    auto nowSta = station;
    auto staIt = std::find_if(stations.begin(), stations.end(), [&](const TrackedStation &sta) {
        return sta.mac == nowSta && sta.apBssid == nowAp;
    });
    if (staIt == stations.end()) {
        TrackedStation sta;
        sta.mac = nowSta;
        sta.apBssid = nowAp;
        sta.channel = packet->rx_ctrl.channel;
        sta.rssi = packet->rx_ctrl.rssi;
        stations.push_back(sta);
        apIt->stationIndices.push_back(stations.size() - 1);
    } else {
        staIt->rssi = packet->rx_ctrl.rssi;
        staIt->channel = packet->rx_ctrl.channel;
    }
}

void StationTracker::ensureWifiReady()
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        previousWifiMode = mode;
        restoreWifi = (mode != WIFI_MODE_NULL);
    } else {
        previousWifiMode = WIFI_MODE_NULL;
        restoreWifi = false;
    }

    WiFi.disconnect(true, true);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t initErr = esp_wifi_init(&cfg);
    if (initErr != ESP_OK && initErr != ESP_ERR_WIFI_INIT_STATE)
        LOG_WARN("Station tracker WiFi init failed (%d)", initErr);

    esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
        LOG_WARN("Station tracker WiFi start failed (%d)", err);

    currentChannel = kFirstChannel;
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

void StationTracker::startChannelHopTimer()
{
    if (!hopTimer) {
        esp_timer_create_args_t args = {};
        args.callback = &StationTracker::channelHopTimer;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "station_hop";
        if (esp_timer_create(&args, &hopTimer) != ESP_OK) {
            LOG_WARN("Station tracker failed to create hop timer");
            hopTimer = nullptr;
            return;
        }
    }
    esp_err_t startErr = esp_timer_start_periodic(hopTimer, kChannelHopIntervalUs);
    if (startErr == ESP_ERR_INVALID_STATE) {
        esp_timer_stop(hopTimer);
        startErr = esp_timer_start_periodic(hopTimer, kChannelHopIntervalUs);
    }
    if (startErr != ESP_OK) {
        LOG_WARN("Station tracker hop timer failed to start (%d)", startErr);
    }
}

void StationTracker::stopChannelHopTimer()
{
    if (!hopTimer)
        return;
    esp_timer_stop(hopTimer);
}

void StationTracker::channelHopTimer(void *arg)
{
    auto *tracker = static_cast<StationTracker *>(arg);
    if (tracker)
        tracker->hopChannel();
}

void StationTracker::hopChannel()
{
    if (!running)
        return;
    currentChannel = (currentChannel >= kLastChannel) ? kFirstChannel : currentChannel + 1;
    esp_err_t err = esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT)
        LOG_DEBUG("Station tracker channel hop failed (%d)", err);
}

} // namespace marauder

#endif

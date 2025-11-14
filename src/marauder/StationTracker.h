#pragma once

#include "configuration.h"

#if HAS_WIFI && defined(ARCH_ESP32)

#include <array>
#include <vector>

#include <esp_timer.h>
#include <esp_wifi.h>

namespace marauder
{

struct TrackedStation
{
    std::array<uint8_t, 6> mac{};
    std::array<uint8_t, 6> apBssid{};
    int32_t rssi = 0;
    uint8_t channel = 0;
    bool selected = false;
};

struct TrackedAccessPoint
{
    std::array<uint8_t, 6> bssid{};
    std::string ssid;
    uint8_t channel = 0;
    bool selected = false;
    std::vector<size_t> stationIndices;
};

class StationTracker
{
  public:
    static StationTracker &instance();

    void start();
    void stop();
    bool isRunning() const { return running; }

    void clear();
    const std::vector<TrackedAccessPoint> &getAccessPoints() const { return aps; }
    const std::vector<TrackedStation> &getStations() const { return stations; }

    void selectStation(size_t stationIndex);
    bool hasSelection() const { return selectedStation < stations.size(); }
    const TrackedStation *getSelectedStation() const;
    size_t selectedStationIndex() const { return selectedStation; }

  private:
    StationTracker();

    static void promiscuousCb(void *buf, wifi_promiscuous_pkt_type_t type);
    void handlePacket(const wifi_promiscuous_pkt_t *packet, wifi_promiscuous_pkt_type_t type);
    void ensureWifiReady();
    void startChannelHopTimer();
    void stopChannelHopTimer();
    static void channelHopTimer(void *arg);
    void hopChannel();

    bool running = false;
    std::vector<TrackedAccessPoint> aps;
    std::vector<TrackedStation> stations;
    size_t selectedStation = SIZE_MAX;
    bool restoreWifi = false;
    wifi_mode_t previousWifiMode = WIFI_MODE_NULL;
    esp_timer_handle_t hopTimer = nullptr;
    uint8_t currentChannel = 1;
};

} // namespace marauder

#endif

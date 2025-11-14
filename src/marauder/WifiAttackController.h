#pragma once

#include "configuration.h"
#include "marauder/WifiAttackCatalog.h"

#if HAS_WIFI && defined(ARCH_ESP32)

#include "concurrency/OSThread.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace marauder
{

class WifiAttackController : public concurrency::OSThread
{
  public:
    static WifiAttackController &instance();

    bool toggle(WifiAttackType type);
    bool start(WifiAttackType type);
    void stop();
    void setPreferredTarget(const std::array<uint8_t, 6> &bssid, uint8_t channel);
    void setPreferredStation(const std::array<uint8_t, 6> &bssid,
                             const std::array<uint8_t, 6> &station,
                             uint8_t channel);

    bool isRunning() const { return active; }
    WifiAttackType currentAttack() const { return currentMode; }

  private:
    WifiAttackController();

    int32_t runOnce() override;

    bool prepareEnvironment(WifiAttackType type);
    bool ensureAccessPointSnapshot(uint32_t maxAgeMs);
    bool refreshAccessPoints();
    bool prepareTransceiver();
    bool isAttackImplemented(WifiAttackType type) const;
    void restoreWifiState();
    void teardownAttackState();

    void handleAttackTick(uint32_t nowMs);
    void handleBeaconList();
    void handleBeaconRandom();
    void handleFunnyBeacon();
    void handleRickRoll();
    void handleProbeFlood();
    void handleDeauthFlood();
    void handleApClone();
    void handleDeauthTargeted();
    void handleBadMsgTargeted();
    void handleAssocSleepTargeted();
    void handleNotSupported();

    void setChannel(uint8_t channel);
    void rotateBeaconCursor(size_t total);
    void updateRateStats(uint32_t nowMs);

    struct AccessPointInfo;

    bool active = false;
    WifiAttackType currentMode = WifiAttackType::BeaconList;

    wifi_mode_t previousWifiMode = WIFI_MODE_NULL;
    bool restartClientWifi = false;

    uint32_t lastScanMs = 0;
    uint32_t lastTickMs = 0;
    uint32_t lastRateLogMs = 0;
    uint32_t packetsSent = 0;
    size_t rollingApIndex = 0;

    std::vector<AccessPointInfo> accessPoints;
    std::unordered_map<uint64_t, size_t> apIndexByKey;
    bool hasPreferredTarget = false;
    std::array<uint8_t, 6> preferredBssid{};
    uint8_t preferredChannel = 0;
    bool hasPreferredStation = false;
    std::array<uint8_t, 6> preferredStation{};
};

} // namespace marauder

#else

namespace marauder
{

class WifiAttackController
{
  public:
    static WifiAttackController &instance()
    {
        static WifiAttackController dummy;
        return dummy;
    }

    bool toggle(WifiAttackType) { return false; }
    bool start(WifiAttackType) { return false; }
    void stop() {}

    bool isRunning() const { return false; }
    WifiAttackType currentAttack() const { return WifiAttackType::BeaconList; }

  private:
    WifiAttackController() = default;
};

} // namespace marauder

#endif

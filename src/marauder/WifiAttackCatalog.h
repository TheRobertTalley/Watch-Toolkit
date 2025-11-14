#pragma once

#include <cstddef>
#include <cstdint>

namespace marauder
{

/**
 * These attack types mirror the legacy ESP32 Marauder WiFi attack menu.
 * The declaration lives in its own header so both the UI layer and the
 * controller can reference a single source of truth for ordering/naming.
 */
enum class WifiAttackType : uint8_t
{
    BeaconList = 0,
    BeaconRandom,
    FunnyBeacon,
    RickRollBeacon,
    ProbeFlood,
    DeauthFlood,
    ApCloneSpam,
    DeauthTargeted,
    BadMsgBroadcast,
    BadMsgTargeted,
    AssocSleepBroadcast,
    AssocSleepTargeted,
    Count
};

constexpr size_t kWifiAttackItemCount = static_cast<size_t>(WifiAttackType::Count);

constexpr const char *kWifiAttackLabels[kWifiAttackItemCount] = {
    "Beacon Spam List",
    "Beacon Spam Random",
    "Funny SSID Beacon",
    "Rick Roll Beacon",
    "Probe Req Flood",
    "Deauth Flood",
    "AP Clone Spam",
    "Deauth Targeted",
    "Bad Msg",
    "Bad Msg Targeted",
    "Assoc Sleep",
    "Assoc Sleep Targeted"};

inline const char *wifiAttackLabel(WifiAttackType type)
{
    auto idx = static_cast<size_t>(type);
    return (idx < kWifiAttackItemCount) ? kWifiAttackLabels[idx] : "";
}

inline const char *wifiAttackLabel(size_t index)
{
    return (index < kWifiAttackItemCount) ? kWifiAttackLabels[index] : "";
}

inline WifiAttackType attackTypeFromIndex(size_t index)
{
    if (index >= kWifiAttackItemCount)
        index = 0;
    return static_cast<WifiAttackType>(index);
}

inline size_t attackIndex(WifiAttackType type)
{
    return static_cast<size_t>(type);
}

} // namespace marauder

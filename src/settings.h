#pragma once
#include <Arduino.h>
#include <cstdint>

// NVS-backed runtime configuration. All fields are editable from /settings
// without reflashing, because several of them (keep-alive payload/interval,
// CAN bitrate) are reverse-engineered best-guesses that need field tuning.
struct Settings {
    // WiFi
    String wifiSsid;
    String wifiPassword;

    // CAN
    uint32_t canBitrate = 500000; // confirmed "CAN-bus BMS (500kbit/s)" profile

    // Keep-alive (0x305 heartbeat emulating the missing GX/inverter).
    // Defaults OFF — see plan notes: timing/payload are not officially
    // published, only community-observed. Everything here is tunable live.
    bool keepaliveEnabled = false;
    uint32_t keepaliveCanId = 0x305;
    uint32_t keepaliveIntervalMs = 500;
    uint8_t keepalivePayload[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t keepalivePayloadLen = 0;

    // Web auth (optional, basic protection for the OTA/settings pages)
    String adminPassword; // empty = no auth required
};

namespace SettingsStore {
void begin();
Settings &get();
void save();
void resetToDefaults();
} // namespace SettingsStore

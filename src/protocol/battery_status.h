#pragma once
#include <cstdint>

// Decoded battery state. Fields come from the Victron "CAN-bus BMS" protocol
// (0x351/0x355/0x356/0x35A/0x35E/0x35F) that BMZ ESS packs are confirmed to
// speak — NOT yet verified against this specific BMZ 618031 over the wire.
struct BatteryStatus {
    // 0x351 — charge/discharge limits, as advertised by the battery
    float chargeVoltageLimit = 0;    // V
    float chargeCurrentLimit = 0;    // A
    float dischargeCurrentLimit = 0; // A
    float dischargeVoltageLimit = 0; // V
    bool haveLimits = false;
    uint32_t limitsAgeMs = 0;

    // 0x355 — state of charge / health
    uint16_t soc = 0;  // %
    uint16_t soh = 0;  // %
    bool haveSoc = false;
    uint32_t socAgeMs = 0;

    // 0x356 — pack voltage/current/temperature
    float packVoltage = 0;    // V
    float packCurrent = 0;    // A
    float packTemperature = 0; // degC
    bool havePack = false;
    uint32_t packAgeMs = 0;

    // 0x35A — alarms/warnings (raw bit flags, 4 bytes each)
    uint32_t alarms = 0;
    uint32_t warnings = 0;
    bool haveAlarms = false;
    uint32_t alarmsAgeMs = 0;

    // 0x35E / 0x35F — identification strings
    char manufacturerName[9] = {0};
    char bmsName[9] = {0};

    uint32_t lastFrameMs = 0;   // last time ANY battery frame was seen
    uint32_t framesSeen = 0;    // total decoded frames (diagnostic counter)
};

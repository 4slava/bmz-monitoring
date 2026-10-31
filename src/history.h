#pragma once
#include <Arduino.h>
#include <cstdint>
#include "protocol/battery_status.h"

// Periodic history sampler: a RAM ring buffer (always available, feeds the
// live graph immediately) that is also appended to LittleFS so recent
// history survives a reboot. Flash usage is bounded via two alternating
// capped files (see history.cpp) rather than one ever-growing log.
//
// Timestamps are Unix epoch seconds (via NTP, set up in wifi_setup.cpp).
// Before NTP sync completes, timestamps will be near-zero — acceptable,
// the graph just looks odd until the clock settles after boot.
struct HistorySample {
    uint32_t epochSec = 0;
    float packVoltage = 0;
    float packPowerW = 0; // charge (+) / discharge (-) power, watts
    int16_t soc = -1;     // -1 = unknown
    float packTemperature = 0;
};

namespace History {
void begin();                 // loads persisted samples into the RAM ring, starts sampler task
void sampleNow(const BatteryStatus &status); // called periodically from main loop/task

// Copies up to maxCount most recent samples (oldest first) into out.
// Returns how many were copied.
size_t getRecent(HistorySample *out, size_t maxCount);
} // namespace History

#pragma once
#include <Arduino.h>
#include <cstdint>
#include "protocol/battery_status.h"

// Persists samples to LittleFS (two alternating, capped files — see
// history.cpp) rather than keeping a RAM copy: at 200KB/file this already
// holds several days of history (far more than any sparkline can usefully
// draw), so reads just stride through the files on demand instead of
// needing a second large buffer in RAM.
//
// Timestamps are Unix epoch seconds (via NTP, set up in wifi_setup.cpp).
// Before NTP sync completes, timestamps will be near-zero — acceptable,
// the graph just looks odd until the clock settles after boot.
struct HistorySample {
    uint32_t epochSec = 0;
    float packVoltage = 0;
    float packPowerW = 0; // charge (+) / discharge (-) power, watts
    int16_t soc = -1;     // -1 = unknown (also used while a BMS restart has
                           // made the reported value untrustworthy, see
                           // NodeStatus::socValid)
    float packTemperature = 0;
};

namespace History {
void begin();                 // mounts LittleFS, figures out which file to keep appending to
void sampleNow(const BatteryStatus &status); // called periodically from main loop/task

// Fills up to maxOut samples, oldest-first, evenly spread across everything
// currently persisted (which may be many times maxOut) so a fixed-size
// response still covers the full retained time range. Returns how many were
// written.
size_t getDownsampled(HistorySample *out, size_t maxOut);
} // namespace History

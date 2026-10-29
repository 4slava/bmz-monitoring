#pragma once
#include <Arduino.h>
#include <cstdint>
#include <functional>

// Thin wrapper over the ESP-IDF TWAI (CAN 2.0) driver.
// TX = GPIO20 (CTX on the WCMCU-230), RX = GPIO19 (CRX).
// Runs in NORMAL mode (not listen-only): the battery requires us to transmit
// a periodic keep-alive (see keepalive.h) since no inverter/GX device is
// present on this bus, so we must be able to ACK/transmit, not just listen.
//
// The BMZ 618031's comms section is otherwise unpowered (confirmed: 0V on
// the battery's CAN transceiver chip with the pack idle) — it requires a
// separate active-high "Enable" line on the battery connector (the blue
// wire / cable pin 4 on this unit; empirically confirmed by driving it to
// 3.3V) before it wakes up and starts talking CAN at all. begin() drives
// this from GPIO18 so it's part of the documented boot sequence rather
// than a loose wire to the 3.3V rail.

struct CanFrame {
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {0};
    bool extended = false;
    bool rx = true; // true = received, false = transmitted by us
    uint32_t timestampMs = 0;
};

// Ring buffer of recent frames for the /can debug view. Fixed capacity,
// overwrites oldest. Not thread-safe beyond what a single producer /
// single consumer with a sequence counter gives us.
class CanFrameLog {
public:
    static constexpr size_t kCapacity = 300;

    void push(const CanFrame &f);
    // Copies frames newer than `sinceSeq` into out (up to maxCount), returns
    // how many were copied and updates sinceSeq to the log's latest seq.
    size_t readSince(uint32_t &sinceSeq, CanFrame *out, size_t maxCount) const;
    uint32_t latestSeq() const { return seq_; }

private:
    CanFrame buf_[kCapacity];
    uint32_t seq_ = 0; // total frames ever pushed
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

namespace CanBus {

using FrameCallback = std::function<void(const CanFrame &)>;

// Initializes the TWAI driver at the given bitrate (default 500 kbit/s —
// the confirmed "CAN-bus BMS (500kbit/s)" profile) and starts the RX task.
bool begin(uint32_t bitrateBps = 500000);

// Registers a callback invoked (from the RX task context) for every
// received frame, in addition to it being logged to the frame log.
void onFrame(FrameCallback cb);

bool send(uint32_t id, const uint8_t *data, uint8_t len, bool extended = false);

CanFrameLog &frameLog();

// Diagnostics
uint32_t rxCount();
uint32_t txCount();
uint32_t rxErrorCount();   // rx_error_counter + arb_lost_count + bus_error_count
uint32_t txErrorCount();   // tx_error_counter + tx_failed_count — what actually
                           // climbs when we transmit alone with nobody to ACK
const char *busStateStr(); // "RUNNING" / "BUS_OFF" / "RECOVERING" / "STOPPED"
bool isAlive(); // true if a frame was received within the last 5s

} // namespace CanBus

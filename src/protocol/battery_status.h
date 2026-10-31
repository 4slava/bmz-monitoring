#pragma once
#include <cstdint>
#include <cstddef>

// Decoded per-module state for the BMZ 618031's actual CAN protocol —
// reverse-engineered by the community (not an official BMZ datasheet), and
// confirmed to match this pack's real traffic (IDs 0xC1/0x101/0x141/0x181/
// 0x1C1/0x301/0x381/0x3C1/0x6C1/0x201-0x2C1 all observed on the bus).
//
// IDs are node-relative: base = id & ~0x3F, node = id & 0x3F. Each physical
// battery module on the bus has its own node number (1, 2, ...) added to
// each message-type base, so e.g. node 1's heartbeat is 0xC1 and node 2's
// is 0xC2. Byte layouts for several fields are the reporter's best-effort
// reconstruction, not confirmed against an official datasheet — treat
// scaling on temperature/voltage fields as approximate until cross-checked
// against the /can raw view.
constexpr size_t kMaxNodes = 4; // generous; only node IDs actually seen on
                                 // the bus end up "present"

struct NodeStatus {
    bool present = false;
    uint32_t lastFrameMs = 0;

    // 0x201/0x241/0x281/0x2C1 — cell voltages 1-16 (4 per frame), volts.
    // Pack voltage is derived as the sum of these rather than guessed from
    // extra bytes, since the source's "+ packVoltage" note on 0x2C1 is
    // ambiguous about byte layout.
    float cellVoltage[16] = {0};
    bool haveCellVoltages = false;
    float packVoltage() const {
        float sum = 0;
        for (float v : cellVoltage) sum += v;
        return sum;
    }

    // 0x141 — min/max cell voltage (bytes 0-1 / 2-3, assumed mV) + BMS
    // status flags (bytes 4-5, confirmed location, meaning undecoded)
    float minCellVoltage = 0;
    float maxCellVoltage = 0;
    uint16_t statusFlags = 0;
    bool haveMinMax = false;

    // 0x1C1 — bytes 2-3, x0.1 scale. Source labeled this "pack current (A)",
    // but cross-checked against a real charging session (EcoFlow Delta 2
    // Max at 1300W split across its internal battery + these 2 packs): the
    // decoded values (~282W / ~274W) matched the user's own ~270W/pack
    // estimate almost exactly, while any amps interpretation was far too
    // high for the hardware involved. This is charge/discharge power in
    // watts, not current. Bytes 4 and 5 both look like SOC candidates;
    // exposing both since it's not yet settled which (if either) is
    // authoritative.
    float packPowerW = 0;   // W, +charge/-discharge (sign not yet confirmed)
    int16_t socByte4 = -1;  // %, from byte 4
    int16_t socByte5 = -1;  // %, from byte 5
    bool havePackPower = false;

    // 0x181 — 3 channels of board temperature (NTC 1-3)
    float boardTemp[3] = {0};
    bool haveBoardTemp = false;

    // 0x301 — 6 channels of cell temperature, 1 byte each
    int8_t cellTemp[6] = {0};
    bool haveCellTemp = false;

    // 0x101 — bytes 0-3 fault bits, byte 5 main relay state (1=closed)
    bool relayClosed = false;
    uint32_t faultBits = 0;
    bool haveRelay = false;

    // 0xC1 — heartbeat ("system alive", byte0==1)
    bool heartbeat = false;
    uint32_t heartbeatAgeMs = 0;

    // 0x381 — current limit (bytes 0-1, x0.1A) + an unknown config param
    // (bytes 2-3, confirmed NOT to be capacity despite resembling it)
    float currentLimit = 0; // A
    uint16_t unknownConfig = 0;
    bool haveCurrentLimit = false;

    // 0x3C1 — presumed cell balancing bitmask, cells 1-16 (bytes 0-3) +
    // an undecoded extra value (bytes 4-5)
    uint32_t balanceBitmask = 0;
    uint16_t balanceExtra = 0;
    bool haveBalance = false;

    // 0x6C1 — serial number / firmware version (raw bytes) + readiness
    // phase (byte 4: 0 = warming up, nonzero = ready)
    uint8_t identityRaw[8] = {0};
    uint8_t readyPhase = 0;
    bool haveIdentity = false;
};

struct BatteryStatus {
    NodeStatus nodes[kMaxNodes]; // indexed by (node id - 1)
    uint32_t framesSeen = 0;     // total decoded frames (diagnostic counter)
    uint32_t lastFrameMs = 0;    // last time ANY recognized frame was seen

    int firstPresentIndex() const {
        for (size_t i = 0; i < kMaxNodes; i++) {
            if (nodes[i].present) return (int)i;
        }
        return -1;
    }
};

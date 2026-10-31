#include "bmz_can.h"
#include <Arduino.h>
#include <cstring>

namespace BmzCan {
namespace {

constexpr uint32_t kMaskNode = 0x3F;
constexpr uint32_t kMaskBase = ~kMaskNode;

constexpr uint32_t kBaseHeartbeat = 0xC0;
constexpr uint32_t kBaseHeartbeat2 = 0x80; // purpose unconfirmed
constexpr uint32_t kBaseRelayFaults = 0x100;
constexpr uint32_t kBaseMinMaxFlags = 0x140;
constexpr uint32_t kBaseBoardTemp = 0x180;
constexpr uint32_t kBasePowerSoc = 0x1C0;
constexpr uint32_t kBaseCellV1_4 = 0x200;
constexpr uint32_t kBaseCellV5_8 = 0x240;
constexpr uint32_t kBaseCellV9_12 = 0x280;
constexpr uint32_t kBaseCellV13_16 = 0x2C0;
constexpr uint32_t kBaseCellTemp = 0x300;
constexpr uint32_t kBaseCurrentLimit = 0x380;
constexpr uint32_t kBaseBalance = 0x3C0;
constexpr uint32_t kBaseIdentity = 0x6C0;

uint16_t u16le(const uint8_t *d) { return (uint16_t)d[0] | ((uint16_t)d[1] << 8); }
int16_t i16le(const uint8_t *d) { return (int16_t)u16le(d); }
uint32_t u32le(const uint8_t *d) {
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

bool baseKnown(uint32_t base) {
    switch (base) {
        case kBaseHeartbeat:
        case kBaseHeartbeat2:
        case kBaseRelayFaults:
        case kBaseMinMaxFlags:
        case kBaseBoardTemp:
        case kBasePowerSoc:
        case kBaseCellV1_4:
        case kBaseCellV5_8:
        case kBaseCellV9_12:
        case kBaseCellV13_16:
        case kBaseCellTemp:
        case kBaseCurrentLimit:
        case kBaseBalance:
        case kBaseIdentity:
            return true;
        default:
            return false;
    }
}

void decodeCellVoltages(NodeStatus &n, const CanFrame &f, int startIdx) {
    if (f.dlc < 8) return;
    for (int i = 0; i < 4; i++) {
        n.cellVoltage[startIdx + i] = u16le(f.data + i * 2) / 1000.0f;
    }
    n.haveCellVoltages = true;
}

} // namespace

bool isProtocolFrame(uint32_t id) {
    uint32_t node = id & kMaskNode;
    if (node < 1 || node > kMaxNodes) return false;
    return baseKnown(id & kMaskBase);
}

void decodeFrame(const CanFrame &f, BatteryStatus &status) {
    uint32_t node = f.id & kMaskNode;
    uint32_t base = f.id & kMaskBase;
    if (node < 1 || node > kMaxNodes || !baseKnown(base)) return;

    NodeStatus &n = status.nodes[node - 1];
    n.present = true;
    n.lastFrameMs = f.timestampMs;
    status.lastFrameMs = f.timestampMs;
    status.framesSeen++;

    switch (base) {
        case kBaseHeartbeat:
            if (f.dlc >= 1) {
                n.heartbeat = (f.data[0] == 1);
                n.heartbeatAgeMs = f.timestampMs;
            }
            break;

        case kBaseHeartbeat2:
            // Purpose unconfirmed — presence alone already updated
            // lastFrameMs/present above; nothing further decoded.
            break;

        case kBaseRelayFaults:
            if (f.dlc >= 6) {
                n.faultBits = u32le(f.data);
                n.relayClosed = (f.data[5] == 1);
                n.haveRelay = true;
            }
            break;

        case kBaseMinMaxFlags:
            if (f.dlc >= 6) {
                n.minCellVoltage = u16le(f.data) / 1000.0f;
                n.maxCellVoltage = u16le(f.data + 2) / 1000.0f;
                n.statusFlags = u16le(f.data + 4);
                n.haveMinMax = true;
            }
            break;

        case kBaseBoardTemp:
            if (f.dlc >= 6) {
                for (int i = 0; i < 3; i++) {
                    n.boardTemp[i] = i16le(f.data + i * 2) * 0.1f;
                }
                n.haveBoardTemp = true;
            }
            break;

        case kBasePowerSoc:
            if (f.dlc >= 6) {
                n.packPowerW = i16le(f.data + 2) * 0.1f;
                n.havePackPower = true;
                n.socByte4 = f.data[4];
                n.socByte5 = f.data[5];
            }
            break;

        case kBaseCellV1_4:   decodeCellVoltages(n, f, 0);  break;
        case kBaseCellV5_8:   decodeCellVoltages(n, f, 4);  break;
        case kBaseCellV9_12:  decodeCellVoltages(n, f, 8);  break;
        case kBaseCellV13_16: decodeCellVoltages(n, f, 12); break;

        case kBaseCellTemp:
            if (f.dlc >= 6) {
                for (int i = 0; i < 6; i++) {
                    n.cellTemp[i] = (int8_t)f.data[i];
                }
                n.haveCellTemp = true;
            }
            break;

        case kBaseCurrentLimit:
            if (f.dlc >= 4) {
                n.currentLimit = i16le(f.data) * 0.1f;
                n.unknownConfig = u16le(f.data + 2);
                n.haveCurrentLimit = true;
            }
            break;

        case kBaseBalance:
            if (f.dlc >= 6) {
                n.balanceBitmask = u32le(f.data);
                n.balanceExtra = u16le(f.data + 4);
                n.haveBalance = true;
            }
            break;

        case kBaseIdentity:
            memcpy(n.identityRaw, f.data, min<uint8_t>(f.dlc, 8));
            if (f.dlc >= 5) {
                n.readyPhase = f.data[4];
            }
            n.haveIdentity = true;
            break;
    }
}

} // namespace BmzCan

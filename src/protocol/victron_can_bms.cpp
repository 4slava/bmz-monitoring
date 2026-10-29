#include "victron_can_bms.h"
#include <Arduino.h>
#include <cstring>

namespace VictronCanBms {
namespace {
int16_t le16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
uint16_t ule16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t ule32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
} // namespace

bool isProtocolFrame(uint32_t id) {
    switch (id) {
        case kIdLimits:
        case kIdSoc:
        case kIdPack:
        case kIdAlarms:
        case kIdManufacturer:
        case kIdBmsName:
            return true;
        default:
            return false;
    }
}

void decodeFrame(const CanFrame &f, BatteryStatus &status) {
    status.lastFrameMs = f.timestampMs;

    switch (f.id) {
        case kIdLimits:
            if (f.dlc >= 8) {
                status.chargeVoltageLimit = le16(&f.data[0]) * 0.1f;
                status.chargeCurrentLimit = le16(&f.data[2]) * 0.1f;
                status.dischargeCurrentLimit = le16(&f.data[4]) * 0.1f;
                status.dischargeVoltageLimit = le16(&f.data[6]) * 0.1f;
                status.haveLimits = true;
                status.limitsAgeMs = f.timestampMs;
                status.framesSeen++;
            }
            break;

        case kIdSoc:
            if (f.dlc >= 4) {
                status.soc = ule16(&f.data[0]);
                status.soh = ule16(&f.data[2]);
                status.haveSoc = true;
                status.socAgeMs = f.timestampMs;
                status.framesSeen++;
            }
            break;

        case kIdPack:
            if (f.dlc >= 6) {
                status.packVoltage = le16(&f.data[0]) * 0.01f;
                status.packCurrent = le16(&f.data[2]) * 0.1f;
                status.packTemperature = le16(&f.data[4]) * 0.1f;
                status.havePack = true;
                status.packAgeMs = f.timestampMs;
                status.framesSeen++;
            }
            break;

        case kIdAlarms:
            if (f.dlc >= 8) {
                // Raw 32-bit alarm / 32-bit warning bit flags. Exact bit
                // assignment varies between vendors implementing this
                // protocol and isn't confirmed for BMZ — surfaced raw in
                // the UI rather than guessing a bit table.
                status.alarms = ule32(&f.data[0]);
                status.warnings = ule32(&f.data[4]);
                status.haveAlarms = true;
                status.alarmsAgeMs = f.timestampMs;
                status.framesSeen++;
            }
            break;

        case kIdManufacturer:
            memset(status.manufacturerName, 0, sizeof(status.manufacturerName));
            memcpy(status.manufacturerName, f.data, min<uint8_t>(f.dlc, 8));
            status.framesSeen++;
            break;

        case kIdBmsName:
            memset(status.bmsName, 0, sizeof(status.bmsName));
            memcpy(status.bmsName, f.data, min<uint8_t>(f.dlc, 8));
            status.framesSeen++;
            break;

        default:
            break;
    }
}

} // namespace VictronCanBms

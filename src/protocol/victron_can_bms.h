#pragma once
#include "battery_status.h"
#include "../can/twai_bus.h"

// Decoder for the Victron "CAN-bus BMS" protocol family (500 kbit/s, 11-bit
// standard IDs, little-endian), confirmed via Victron's own BMZ-ESS
// compatibility notes to be what BMZ ESS packs speak. Field scaling below
// follows the commonly-published/open-source implementations of this
// protocol (e.g. dbus-serialbattery, various DIY-BMS Victron drivers) —
// NOT an official BMZ datasheet, so treat exact scaling as "standard until
// proven otherwise against real traffic" (see /can raw view).
namespace VictronCanBms {

constexpr uint32_t kIdLimits = 0x351;       // charge/discharge voltage & current limits
constexpr uint32_t kIdSoc = 0x355;          // SOC / SOH
constexpr uint32_t kIdPack = 0x356;         // pack voltage / current / temperature
constexpr uint32_t kIdAlarms = 0x35A;       // alarms / warnings (raw bit flags)
constexpr uint32_t kIdManufacturer = 0x35E; // manufacturer name (ASCII)
constexpr uint32_t kIdBmsName = 0x35F;      // BMS name (ASCII)

// Returns true if this frame ID belongs to this protocol (useful for
// routing in the main CAN callback).
bool isProtocolFrame(uint32_t id);

// Applies a single received frame to the given status struct, updating
// only the fields that frame's ID covers. Safe to call on every RX frame.
void decodeFrame(const CanFrame &f, BatteryStatus &status);

} // namespace VictronCanBms

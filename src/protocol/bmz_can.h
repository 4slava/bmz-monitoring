#pragma once
#include "battery_status.h"
#include "../can/twai_bus.h"

// Decoder for the BMZ 618031's actual CAN protocol, reverse-engineered by
// the community (not an official datasheet) and confirmed against this
// pack's real traffic. See battery_status.h for the node-addressing scheme
// and per-field confidence notes.
namespace BmzCan {

// Returns true if this frame ID belongs to a known message type (any node).
bool isProtocolFrame(uint32_t id);

// Applies a single received frame to the given status struct, updating
// only the node/fields that frame's ID covers. Safe to call on every RX
// frame.
void decodeFrame(const CanFrame &f, BatteryStatus &status);

} // namespace BmzCan

#pragma once
#include <cstdint>

// Periodic heartbeat emulating the GX-device "keep-alive" that BMZ ESS
// batteries require to stay awake (confirmed behavior: battery shuts down
// ~10s without it). The exact CAN ID/payload/interval are NOT an officially
// published spec — only community-observed (0x305, sent by real GX devices
// every ~200ms-1s). Treat this as experimental and field-verify via the
// /can raw frame view: watch whether the battery starts/keeps broadcasting
// 0x351/0x355/0x356/0x35A after enabling this.
//
// Disabled by default (see Settings::keepaliveEnabled). All of ID/interval/
// payload are runtime-configurable from /settings so they can be tuned
// without reflashing.
namespace Keepalive {
void begin();  // call once from setup(); starts a FreeRTOS timer task
void onSettingsChanged(); // call after Settings are edited at runtime
} // namespace Keepalive

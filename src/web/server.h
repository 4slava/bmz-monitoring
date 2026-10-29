#pragma once
#include "../protocol/battery_status.h"

namespace WebServer {
// status is read (never written) by the web handlers.
void begin(BatteryStatus &status);
} // namespace WebServer

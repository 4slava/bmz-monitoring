#pragma once
#include "../protocol/battery_status.h"

// 0.91" SSD1306 128x32 I2C OLED. SDA=GPIO6, SCL=GPIO7.
// Cycles through a few status pages; call update() periodically (e.g. every
// 500ms) from a low-priority task or the main loop.
namespace Oled {
void begin();
void update(const BatteryStatus &status);
} // namespace Oled

#pragma once
#include <Arduino.h>

// Joins WiFi using saved credentials (Settings). If none are saved, or the
// join fails, falls back to a local AP ("BMZ-Monitor-Setup") so the web
// portal is reachable to configure WiFi for the first time.
namespace WifiSetup {
void begin();
bool isStaConnected();
bool isApMode();
String ipAddress();
} // namespace WifiSetup

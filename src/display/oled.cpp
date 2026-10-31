#include "oled.h"
#include "../wifi_setup.h"
#include "../can/twai_bus.h"
#include <U8g2lib.h>
#include <Wire.h>

namespace Oled {
namespace {
constexpr int kSdaPin = 6;
constexpr int kSclPin = 7;

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

uint32_t g_lastPageSwitchMs = 0;
uint8_t g_page = 0;
constexpr uint32_t kPageMs = 3000;
constexpr uint8_t kPageCount = 3;

void drawBatteryPage(const BatteryStatus &s) {
    u8g2.setFont(u8g2_font_6x10_tf);
    int idx = s.firstPresentIndex();

    char line1[32];
    if (idx >= 0 && s.nodes[idx].haveCellVoltages) {
        const NodeStatus &n = s.nodes[idx];
        snprintf(line1, sizeof(line1), "%.2fV %.0fW", n.packVoltage(), n.packPowerW);
    } else {
        snprintf(line1, sizeof(line1), "no pack data");
    }
    u8g2.drawStr(0, 10, line1);

    char line2[32];
    if (idx >= 0 && s.nodes[idx].socByte5 >= 0) {
        snprintf(line2, sizeof(line2), "node%d SOC %d%%", idx + 1, s.nodes[idx].socByte5);
    } else {
        snprintf(line2, sizeof(line2), "SOC --");
    }
    u8g2.drawStr(0, 22, line2);

    char line3[32];
    if (idx >= 0 && s.nodes[idx].haveBoardTemp) {
        const NodeStatus &n = s.nodes[idx];
        snprintf(line3, sizeof(line3), "Temp %.1fC", n.boardTemp[0]);
    } else {
        snprintf(line3, sizeof(line3), "Temp --");
    }
    u8g2.drawStr(0, 32, line3);
}

void drawNetworkPage() {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 10, WifiSetup::isApMode() ? "WiFi: AP (setup)" : "WiFi: connected");
    char line[32];
    snprintf(line, sizeof(line), "IP %s", WifiSetup::ipAddress().c_str());
    u8g2.drawStr(0, 22, line);
    u8g2.drawStr(0, 32, WifiSetup::isApMode() ? "SSID: BMZ-Monitor-Setup" : "");
}

void drawCanPage(const BatteryStatus &s) {
    u8g2.setFont(u8g2_font_6x10_tf);
    char line1[32];
    snprintf(line1, sizeof(line1), "CAN rx:%lu tx:%lu", (unsigned long)CanBus::rxCount(),
             (unsigned long)CanBus::txCount());
    u8g2.drawStr(0, 10, line1);

    char line2[32];
    snprintf(line2, sizeof(line2), "err:%lu  %s", (unsigned long)CanBus::rxErrorCount(),
             CanBus::isAlive() ? "alive" : "SILENT");
    u8g2.drawStr(0, 22, line2);

    char line3[32];
    snprintf(line3, sizeof(line3), "frames decoded %lu", (unsigned long)s.framesSeen);
    u8g2.drawStr(0, 32, line3);
}

} // namespace

void begin() {
    Wire.setPins(kSdaPin, kSclPin);
    Wire.begin();
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 16, "BMZ monitor booting...");
    u8g2.sendBuffer();
}

void update(const BatteryStatus &status) {
    uint32_t now = millis();
    if (now - g_lastPageSwitchMs > kPageMs) {
        g_page = (g_page + 1) % kPageCount;
        g_lastPageSwitchMs = now;
    }

    u8g2.clearBuffer();
    switch (g_page) {
        case 0: drawBatteryPage(status); break;
        case 1: drawNetworkPage(); break;
        case 2: drawCanPage(status); break;
    }
    u8g2.sendBuffer();
}

} // namespace Oled

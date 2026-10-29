#include <Arduino.h>
#include "settings.h"
#include "wifi_setup.h"
#include "can/twai_bus.h"
#include "can/keepalive.h"
#include "protocol/battery_status.h"
#include "protocol/victron_can_bms.h"
#include "history.h"
#include "display/oled.h"
#include "web/server.h"

BatteryStatus g_status;

namespace {
uint32_t g_lastOledMs = 0;
uint32_t g_lastHistoryMs = 0;
constexpr uint32_t kOledPeriodMs = 500;
constexpr uint32_t kHistoryPeriodMs = 30000;

void onCanFrame(const CanFrame &f) {
    if (VictronCanBms::isProtocolFrame(f.id)) {
        VictronCanBms::decodeFrame(f, g_status);
    }
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    log_i("BMZ monitor booting...");

    SettingsStore::begin();
    Oled::begin();
    WifiSetup::begin();
    History::begin();

    if (!CanBus::begin(SettingsStore::get().canBitrate)) {
        log_e("CAN bus init failed — check wiring (CTX=GPIO20, CRX=GPIO19)");
    }
    CanBus::onFrame(onCanFrame);
    Keepalive::begin();

    WebServer::begin(g_status);

    log_i("Setup complete. IP: %s", WifiSetup::ipAddress().c_str());
}

void loop() {
    uint32_t now = millis();

    if (now - g_lastOledMs >= kOledPeriodMs) {
        g_lastOledMs = now;
        Oled::update(g_status);
    }

    if (now - g_lastHistoryMs >= kHistoryPeriodMs) {
        g_lastHistoryMs = now;
        History::sampleNow(g_status);
    }

    static uint32_t lastHeartbeat = 0;
    if (now - lastHeartbeat >= 2000) {
        lastHeartbeat = now;
        log_i("heartbeat t=%lu ip=%s canAlive=%d", (unsigned long)now,
              WifiSetup::ipAddress().c_str(), CanBus::isAlive());
    }

    delay(10);
}

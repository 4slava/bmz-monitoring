#include "wifi_setup.h"
#include "settings.h"
#include <WiFi.h>
#include <Arduino.h>

namespace WifiSetup {
namespace {
bool g_apMode = false;
const char *kApSsid = "BMZ-Monitor-Setup";
constexpr uint32_t kStaTimeoutMs = 15000;

void startAp() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid);
    g_apMode = true;
    log_w("WiFi: falling back to AP mode, SSID=%s, IP=%s", kApSsid,
          WiFi.softAPIP().toString().c_str());
}

void startNtp() {
    // UTC; the dashboard can apply a display offset client-side if needed.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}
} // namespace

void begin() {
    Settings &s = SettingsStore::get();
    if (s.wifiSsid.isEmpty()) {
        startAp();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPassword.c_str());
    log_i("WiFi: connecting to %s ...", s.wifiSsid.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kStaTimeoutMs) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        g_apMode = false;
        log_i("WiFi: connected, IP=%s", WiFi.localIP().toString().c_str());
        startNtp();
    } else {
        log_w("WiFi: STA connect to '%s' failed/timed out", s.wifiSsid.c_str());
        startAp();
    }
}

bool isStaConnected() { return !g_apMode && WiFi.status() == WL_CONNECTED; }
bool isApMode() { return g_apMode; }

String ipAddress() {
    return g_apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

} // namespace WifiSetup

#include "settings.h"
#include <Preferences.h>

namespace SettingsStore {
namespace {
Preferences prefs;
Settings s;
constexpr const char *kNamespace = "bmzmon";
} // namespace

void begin() {
    prefs.begin(kNamespace, false);
    s.wifiSsid = prefs.getString("wifiSsid", "");
    s.wifiPassword = prefs.getString("wifiPass", "");
    s.canBitrate = prefs.getULong("canBitrate", 500000);
    s.keepaliveEnabled = prefs.getBool("kaEnabled", false);
    s.keepaliveCanId = prefs.getULong("kaId", 0x305);
    s.keepaliveIntervalMs = prefs.getULong("kaIntervalMs", 500);
    s.keepalivePayloadLen = prefs.getUChar("kaLen", 0);
    size_t got = prefs.getBytes("kaPayload", s.keepalivePayload, sizeof(s.keepalivePayload));
    (void)got;
    s.adminPassword = prefs.getString("adminPass", "");
}

Settings &get() { return s; }

void save() {
    prefs.putString("wifiSsid", s.wifiSsid);
    prefs.putString("wifiPass", s.wifiPassword);
    prefs.putULong("canBitrate", s.canBitrate);
    prefs.putBool("kaEnabled", s.keepaliveEnabled);
    prefs.putULong("kaId", s.keepaliveCanId);
    prefs.putULong("kaIntervalMs", s.keepaliveIntervalMs);
    prefs.putUChar("kaLen", s.keepalivePayloadLen);
    prefs.putBytes("kaPayload", s.keepalivePayload, sizeof(s.keepalivePayload));
    prefs.putString("adminPass", s.adminPassword);
}

void resetToDefaults() {
    prefs.clear();
    s = Settings{};
}

} // namespace SettingsStore

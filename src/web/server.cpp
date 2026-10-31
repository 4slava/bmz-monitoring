#include "server.h"
#include "pages.h"
#include "../settings.h"
#include "../can/twai_bus.h"
#include "../debug/log_sink.h"
#include "../history.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>

namespace WebServer {
namespace {
AsyncWebServer server(80);
BatteryStatus *g_status = nullptr;

String renderPage(const char *progmemHtml) {
    String page = FPSTR(progmemHtml);
    page.replace("%STYLE%", FPSTR(Pages::kStyle));
    page.replace("%NAV%", FPSTR(Pages::kNav));
    return page;
}

bool requireAuthIfSet(AsyncWebServerRequest *request) {
    const String &pass = SettingsStore::get().adminPassword;
    if (pass.isEmpty()) return true;
    if (request->authenticate("admin", pass.c_str())) return true;
    request->requestAuthentication();
    return false;
}

void handleStatus(AsyncWebServerRequest *request) {
    JsonDocument doc;
    const BatteryStatus &s = *g_status;
    doc["buildTime"] = __DATE__ " " __TIME__;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["maxAllocHeap"] = ESP.getMaxAllocHeap();
    doc["minFreeHeap"] = ESP.getMinFreeHeap();
    doc["framesSeen"] = s.framesSeen;
    doc["canAlive"] = CanBus::isAlive();
    doc["canRx"] = CanBus::rxCount();
    doc["canTx"] = CanBus::txCount();
    doc["canErr"] = CanBus::rxErrorCount();
    doc["canTxErr"] = CanBus::txErrorCount();
    doc["canBusState"] = CanBus::busStateStr();
    doc["keepaliveEnabled"] = SettingsStore::get().keepaliveEnabled;

    JsonArray nodes = doc["nodes"].to<JsonArray>();
    for (size_t i = 0; i < kMaxNodes; i++) {
        const NodeStatus &n = s.nodes[i];
        if (!n.present) continue;
        JsonObject o = nodes.add<JsonObject>();
        o["id"] = i + 1;
        o["lastFrameMs"] = n.lastFrameMs;
        o["heartbeat"] = n.heartbeat;

        if (n.haveCellVoltages) {
            o["packVoltage"] = n.packVoltage();
            JsonArray cv = o["cellVoltage"].to<JsonArray>();
            for (float v : n.cellVoltage) cv.add(v);
        }
        if (n.haveMinMax) {
            o["minCellVoltage"] = n.minCellVoltage;
            o["maxCellVoltage"] = n.maxCellVoltage;
            o["statusFlags"] = n.statusFlags;
        }
        if (n.havePackPower) {
            o["packPowerW"] = n.packPowerW;
            o["socByte4"] = n.socByte4;
            o["socByte5"] = n.socByte5;
        }
        if (n.haveBoardTemp) {
            JsonArray bt = o["boardTemp"].to<JsonArray>();
            for (float t : n.boardTemp) bt.add(t);
        }
        if (n.haveCellTemp) {
            JsonArray ct = o["cellTemp"].to<JsonArray>();
            for (int8_t t : n.cellTemp) ct.add(t);
        }
        if (n.haveRelay) {
            o["relayClosed"] = n.relayClosed;
            o["faultBits"] = n.faultBits;
        }
        if (n.haveCurrentLimit) {
            o["currentLimit"] = n.currentLimit;
            o["unknownConfig"] = n.unknownConfig;
        }
        if (n.haveBalance) {
            o["balanceBitmask"] = n.balanceBitmask;
            o["balanceExtra"] = n.balanceExtra;
        }
        if (n.haveIdentity) {
            o["readyPhase"] = n.readyPhase;
        }
    }

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleHistory(AsyncWebServerRequest *request) {
    // Capped well below the full 2880-sample ring (24h @ 30s/sample): building
    // an ArduinoJson document for thousands of objects plus the resulting
    // ~170KB string doesn't fit in this device's RAM. A few hundred points is
    // already more than the dashboard's sparkline can usefully show.
    constexpr size_t kMaxReturned = 300;
    static HistorySample buf[kMaxReturned];
    size_t n = History::getRecent(buf, kMaxReturned);
    JsonDocument doc;
    JsonArray arr = doc["samples"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["t"] = buf[i].epochSec;
        o["v"] = buf[i].packVoltage;
        o["p"] = buf[i].packPowerW;
        o["soc"] = buf[i].soc;
        o["temp"] = buf[i].packTemperature;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleCanFrames(AsyncWebServerRequest *request) {
    uint32_t since = 0;
    if (request->hasParam("since")) {
        since = request->getParam("since")->value().toInt();
    }
    // Capped well below CanFrameLog::kCapacity (300): building an ArduinoJson
    // document for that many objects (each with a nested data field) doesn't
    // fit in this device's RAM. The /can page polls every 400ms, so a smaller
    // per-response cap just means catching up over a couple of extra polls
    // after a burst — fine for a live debug view, not an audit log.
    constexpr size_t kMaxReturned = 50;
    static CanFrame buf[kMaxReturned];
    size_t n = CanBus::frameLog().readSince(since, buf, kMaxReturned);

    JsonDocument doc;
    doc["seq"] = since;
    doc["rxCount"] = CanBus::rxCount();
    doc["txCount"] = CanBus::txCount();
    doc["rxErrorCount"] = CanBus::rxErrorCount();
    doc["txErrorCount"] = CanBus::txErrorCount();
    doc["busState"] = CanBus::busStateStr();
    JsonArray arr = doc["frames"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["t"] = buf[i].timestampMs;
        o["id"] = buf[i].id;
        o["dlc"] = buf[i].dlc;
        o["rx"] = buf[i].rx;
        char hex[17];
        for (int b = 0; b < buf[i].dlc; b++) {
            snprintf(hex + b * 2, 3, "%02x", buf[i].data[b]);
        }
        hex[buf[i].dlc * 2] = '\0';
        o["data"] = String(hex);
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleLogs(AsyncWebServerRequest *request) {
    uint32_t since = 0;
    if (request->hasParam("since")) {
        since = request->getParam("since")->value().toInt();
    }
    static LogLine buf[LogRingBuffer::kCapacity];
    size_t n = LogSink::buffer().readSince(since, buf, LogRingBuffer::kCapacity);

    JsonDocument doc;
    doc["seq"] = since;
    JsonArray arr = doc["lines"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["t"] = buf[i].timestampMs;
        o["m"] = buf[i].text;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleGetSettings(AsyncWebServerRequest *request) {
    if (!requireAuthIfSet(request)) return;
    Settings &s = SettingsStore::get();
    JsonDocument doc;
    doc["wifiSsid"] = s.wifiSsid;
    doc["canBitrate"] = s.canBitrate;
    doc["keepaliveEnabled"] = s.keepaliveEnabled;
    doc["keepaliveCanId"] = s.keepaliveCanId;
    doc["keepaliveIntervalMs"] = s.keepaliveIntervalMs;
    JsonArray payload = doc["keepalivePayload"].to<JsonArray>();
    for (int i = 0; i < s.keepalivePayloadLen; i++) payload.add(s.keepalivePayload[i]);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// Body is buffered by ESPAsyncWebServer's onBody-less JSON handler helper.
void handlePostSettings(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!requireAuthIfSet(request)) return;
    Settings &s = SettingsStore::get();
    JsonObject o = json.as<JsonObject>();

    s.wifiSsid = o["wifiSsid"] | s.wifiSsid;
    if (o["wifiPassword"].is<const char *>() && strlen(o["wifiPassword"]) > 0) {
        s.wifiPassword = o["wifiPassword"].as<const char *>();
    }
    s.canBitrate = o["canBitrate"] | s.canBitrate;
    s.keepaliveEnabled = o["keepaliveEnabled"] | s.keepaliveEnabled;
    s.keepaliveCanId = o["keepaliveCanId"] | s.keepaliveCanId;
    s.keepaliveIntervalMs = o["keepaliveIntervalMs"] | s.keepaliveIntervalMs;
    s.adminPassword = o["adminPassword"] | s.adminPassword;

    if (o["keepalivePayload"].is<JsonArray>()) {
        JsonArray payload = o["keepalivePayload"];
        uint8_t len = 0;
        for (JsonVariant v : payload) {
            if (len >= 8) break;
            s.keepalivePayload[len++] = (uint8_t)(v.as<int>() & 0xFF);
        }
        s.keepalivePayloadLen = len;
    }

    SettingsStore::save();
    request->send(200, "text/plain", "OK");
}

void handleRestart(AsyncWebServerRequest *request) {
    if (!requireAuthIfSet(request)) return;
    request->send(200, "text/plain", "Restarting");
    delay(200);
    ESP.restart();
}

void handleUpdateUpload(AsyncWebServerRequest *request, const String &filename, size_t index,
                         uint8_t *data, size_t len, bool final) {
    if (!index) {
        RLOGI("OTA: starting upload %s", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            RLOGE("OTA: Update.begin failed");
        }
    }
    if (Update.write(data, len) != len) {
        RLOGE("OTA: Update.write short write");
    }
    if (final) {
        if (Update.end(true)) {
            RLOGI("OTA: update complete (%u bytes), rebooting", (unsigned)(index + len));
        } else {
            RLOGE("OTA: Update.end failed, error=%u", Update.getError());
        }
    }
}

void handleUpdateFinish(AsyncWebServerRequest *request) {
    if (!requireAuthIfSet(request)) return;
    bool ok = !Update.hasError();
    AsyncWebServerResponse *response =
        request->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK" : "Update failed");
    response->addHeader("Connection", "close");
    request->send(response);
    if (ok) {
        delay(500);
        ESP.restart();
    }
}

} // namespace

void begin(BatteryStatus &status) {
    g_status = &status;

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(200, "text/html", renderPage(Pages::kDashboard));
    });
    server.on("/can", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(200, "text/html", renderPage(Pages::kCanDebug));
    });
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (!requireAuthIfSet(r)) return;
        r->send(200, "text/html", renderPage(Pages::kSettings));
    });
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (!requireAuthIfSet(r)) return;
        r->send(200, "text/html", renderPage(Pages::kUpdate));
    });
    server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(200, "text/html", renderPage(Pages::kLogs));
    });

    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/history", HTTP_GET, handleHistory);
    server.on("/api/can/frames", HTTP_GET, handleCanFrames);
    server.on("/api/logs", HTTP_GET, handleLogs);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/restart", HTTP_POST, handleRestart);

    AsyncCallbackJsonWebHandler *settingsHandler = new AsyncCallbackJsonWebHandler(
        "/api/settings", handlePostSettings);
    server.addHandler(settingsHandler);

    server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);

    server.onNotFound([](AsyncWebServerRequest *r) { r->send(404, "text/plain", "Not found"); });

    server.begin();
    RLOGI("Web server started");
}

} // namespace WebServer

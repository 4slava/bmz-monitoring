#include "keepalive.h"
#include "twai_bus.h"
#include "../settings.h"
#include <Arduino.h>

namespace Keepalive {
namespace {
TaskHandle_t g_task = nullptr;

void taskFn(void *) {
    for (;;) {
        Settings &s = SettingsStore::get();
        if (s.keepaliveEnabled) {
            CanBus::send(s.keepaliveCanId, s.keepalivePayload, s.keepalivePayloadLen, false);
            vTaskDelay(pdMS_TO_TICKS(max<uint32_t>(s.keepaliveIntervalMs, 50)));
        } else {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}
} // namespace

void begin() {
    xTaskCreate(taskFn, "can_keepalive", 2048, nullptr, 5, &g_task);
}

void onSettingsChanged() {
    // Task reads Settings fresh on each loop iteration; nothing else to do.
}

} // namespace Keepalive

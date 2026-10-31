#include "twai_bus.h"
#include "../debug/log_sink.h"
#include "driver/twai.h"
#include <vector>

static const gpio_num_t kTxPin = GPIO_NUM_20; // CTX
static const gpio_num_t kRxPin = GPIO_NUM_19; // CRX
static const gpio_num_t kEnablePin = GPIO_NUM_18; // battery "Enable" line (blue wire, cable pin 4)

void CanFrameLog::push(const CanFrame &f) {
    taskENTER_CRITICAL(&mux_);
    buf_[seq_ % kCapacity] = f;
    seq_++;
    taskEXIT_CRITICAL(&mux_);
}

size_t CanFrameLog::readSince(uint32_t &sinceSeq, CanFrame *out, size_t maxCount) const {
    taskENTER_CRITICAL(const_cast<portMUX_TYPE *>(&mux_));
    uint32_t latest = seq_;
    uint32_t start = sinceSeq;
    if (latest - start > kCapacity) start = latest - kCapacity; // we lost some, skip ahead
    size_t count = 0;
    for (uint32_t s = start; s < latest && count < maxCount; s++, count++) {
        out[count] = buf_[s % kCapacity];
    }
    taskEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&mux_));
    sinceSeq = latest;
    return count;
}

namespace CanBus {
namespace {
CanFrameLog g_log;
std::vector<FrameCallback> g_callbacks;
TaskHandle_t g_rxTask = nullptr;
volatile uint32_t g_rxCount = 0;
volatile uint32_t g_txCount = 0;
volatile uint32_t g_rxErrorCount = 0;
volatile uint32_t g_txErrorCount = 0;
volatile uint32_t g_lastRxMs = 0;
volatile twai_state_t g_busState = TWAI_STATE_STOPPED;

const char *stateToStr(twai_state_t s) {
    switch (s) {
        case TWAI_STATE_RUNNING: return "RUNNING";
        case TWAI_STATE_BUS_OFF: return "BUS_OFF";
        case TWAI_STATE_RECOVERING: return "RECOVERING";
        case TWAI_STATE_STOPPED: return "STOPPED";
        default: return "UNKNOWN";
    }
}

void rxTaskFn(void *) {
    for (;;) {
        twai_message_t msg;
        if (twai_receive(&msg, pdMS_TO_TICKS(1000)) == ESP_OK) {
            CanFrame f;
            f.id = msg.identifier;
            f.extended = msg.extd;
            f.dlc = msg.data_length_code;
            memcpy(f.data, msg.data, min<uint8_t>(f.dlc, 8));
            f.rx = true;
            f.timestampMs = millis();
            g_rxCount++;
            g_lastRxMs = f.timestampMs;
            g_log.push(f);
            for (auto &cb : g_callbacks) cb(f);
        }
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            g_rxErrorCount = status.rx_error_counter + status.arb_lost_count + status.bus_error_count;
            g_txErrorCount = status.tx_error_counter + status.tx_failed_count;
            g_busState = status.state;

            // A lone node transmitting with nobody to ACK will typically trip
            // bus-off almost immediately (expected CAN behavior, not
            // necessarily a hardware fault) — recover automatically so the
            // keep-alive/debug tooling doesn't get stuck silently after the
            // first attempt.
            if (status.state == TWAI_STATE_BUS_OFF) {
                RLOGW("CAN bus-off (tx_err=%lu, likely no other node ACKing) - recovering",
                      (unsigned long)status.tx_error_counter);
                twai_initiate_recovery();
            } else if (status.state == TWAI_STATE_STOPPED) {
                twai_start();
            }
        }
    }
}
} // namespace

bool begin(uint32_t bitrateBps) {
    // Wake the battery's comms section before touching the TWAI driver —
    // without this it has no power on its transceiver and will never ACK
    // or transmit anything.
    pinMode(kEnablePin, OUTPUT);
    digitalWrite(kEnablePin, HIGH);
    delay(200); // let the battery's internal regulator/BMS boot

    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(kTxPin, kRxPin, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 32;
    g_config.tx_queue_len = 16;

    twai_timing_config_t t_config;
    switch (bitrateBps) {
        case 125000: t_config = TWAI_TIMING_CONFIG_125KBITS(); break;
        case 250000: t_config = TWAI_TIMING_CONFIG_250KBITS(); break;
        case 1000000: t_config = TWAI_TIMING_CONFIG_1MBITS(); break;
        default: t_config = TWAI_TIMING_CONFIG_500KBITS(); break;
    }
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        RLOGE("twai_driver_install failed");
        return false;
    }
    if (twai_start() != ESP_OK) {
        RLOGE("twai_start failed");
        return false;
    }
    xTaskCreate(rxTaskFn, "can_rx", 4096, nullptr, 10, &g_rxTask);
    return true;
}

void onFrame(FrameCallback cb) { g_callbacks.push_back(std::move(cb)); }

bool send(uint32_t id, const uint8_t *data, uint8_t len, bool extended) {
    twai_message_t msg = {};
    msg.identifier = id;
    msg.extd = extended ? 1 : 0;
    msg.data_length_code = min<uint8_t>(len, 8);
    memcpy(msg.data, data, msg.data_length_code);
    if (twai_transmit(&msg, pdMS_TO_TICKS(100)) != ESP_OK) return false;
    g_txCount++;

    CanFrame f;
    f.id = id;
    f.extended = extended;
    f.dlc = msg.data_length_code;
    memcpy(f.data, data, f.dlc);
    f.rx = false;
    f.timestampMs = millis();
    g_log.push(f);
    return true;
}

CanFrameLog &frameLog() { return g_log; }
uint32_t rxCount() { return g_rxCount; }
uint32_t txCount() { return g_txCount; }
uint32_t rxErrorCount() { return g_rxErrorCount; }
uint32_t txErrorCount() { return g_txErrorCount; }
const char *busStateStr() { return stateToStr(g_busState); }
bool isAlive() { return g_lastRxMs != 0 && (millis() - g_lastRxMs) < 5000; }

} // namespace CanBus

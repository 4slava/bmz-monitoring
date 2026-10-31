#pragma once
#include <Arduino.h>

// Remote-visible logging. RLOGI/RLOGW/RLOGE behave like printf, print to
// Serial as before, and also capture the line into a ring buffer served
// over /api/logs — so firmware logs are visible from the web UI without a
// USB serial connection.
//
// Arduino's log_i/log_w/log_e macros call log_printf() -> ets_printf()
// directly on the console UART, bypassing ESP-IDF's esp_log_write/vprintf
// hook entirely (confirmed by inspecting esp32-hal-uart.c and esp_log.h for
// this core/IDF version) — so intercepting that hook wasn't viable. These
// macros log directly to our own sink instead.

struct LogLine {
    uint32_t timestampMs = 0;
    char text[160] = {0};
};

class LogRingBuffer {
public:
    static constexpr size_t kCapacity = 200;

    void push(const char *text, size_t len);
    size_t readSince(uint32_t &sinceSeq, LogLine *out, size_t maxCount) const;

private:
    LogLine buf_[kCapacity];
    uint32_t seq_ = 0;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

namespace LogSink {
LogRingBuffer &buffer();
void logf(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
} // namespace LogSink

#define RLOGI(...) LogSink::logf("I", __VA_ARGS__)
#define RLOGW(...) LogSink::logf("W", __VA_ARGS__)
#define RLOGE(...) LogSink::logf("E", __VA_ARGS__)

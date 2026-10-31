#include "log_sink.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
LogRingBuffer g_buffer;
}

void LogRingBuffer::push(const char *text, size_t len) {
    taskENTER_CRITICAL(&mux_);
    LogLine &slot = buf_[seq_ % kCapacity];
    slot.timestampMs = millis();
    size_t copyLen = min(len, sizeof(slot.text) - 1);
    memcpy(slot.text, text, copyLen);
    slot.text[copyLen] = '\0';
    seq_++;
    taskEXIT_CRITICAL(&mux_);
}

size_t LogRingBuffer::readSince(uint32_t &sinceSeq, LogLine *out, size_t maxCount) const {
    taskENTER_CRITICAL(const_cast<portMUX_TYPE *>(&mux_));
    uint32_t latest = seq_;
    uint32_t start = sinceSeq;
    if (latest - start > kCapacity) start = latest - kCapacity;
    size_t count = 0;
    for (uint32_t s = start; s < latest && count < maxCount; s++, count++) {
        out[count] = buf_[s % kCapacity];
    }
    taskEXIT_CRITICAL(const_cast<portMUX_TYPE *>(&mux_));
    sinceSeq = latest;
    return count;
}

namespace LogSink {
LogRingBuffer &buffer() { return g_buffer; }

void logf(const char *level, const char *fmt, ...) {
    char line[160];
    int prefixLen = snprintf(line, sizeof(line), "[%s] ", level);
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + prefixLen, sizeof(line) - prefixLen, fmt, args);
    va_end(args);
    Serial.println(line);
    g_buffer.push(line, strlen(line));
}
} // namespace LogSink

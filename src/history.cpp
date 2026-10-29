#include "history.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>

namespace History {
namespace {

constexpr size_t kRamCapacity = 2880; // 24h @ 30s/sample
HistorySample g_ring[kRamCapacity];
size_t g_count = 0; // number of valid samples (<= kRamCapacity once full)
size_t g_head = 0;  // next write index

constexpr size_t kFileCapBytes = 200 * 1024; // per file; two files alternate
const char *kFiles[2] = {"/history0.bin", "/history1.bin"};
uint8_t g_activeFile = 0;

void appendToFile(const HistorySample &s) {
    File f = LittleFS.open(kFiles[g_activeFile], FILE_APPEND);
    if (!f) return;
    if (f.size() + sizeof(HistorySample) > kFileCapBytes) {
        f.close();
        g_activeFile = 1 - g_activeFile;
        LittleFS.remove(kFiles[g_activeFile]); // start the other file fresh
        f = LittleFS.open(kFiles[g_activeFile], FILE_APPEND);
        if (!f) return;
    }
    f.write(reinterpret_cast<const uint8_t *>(&s), sizeof(s));
    f.close();
}

void pushRam(const HistorySample &s) {
    g_ring[g_head] = s;
    g_head = (g_head + 1) % kRamCapacity;
    if (g_count < kRamCapacity) g_count++;
}

void loadFileInOrder(const char *path, std::vector<HistorySample> &out) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return;
    HistorySample s;
    while (f.read(reinterpret_cast<uint8_t *>(&s), sizeof(s)) == (int)sizeof(s)) {
        out.push_back(s);
    }
    f.close();
}

} // namespace

void begin() {
    if (!LittleFS.begin(true)) {
        log_e("LittleFS mount failed");
        return;
    }
    // Load both files, sort by epoch, replay the most recent kRamCapacity
    // samples into the RAM ring so history survives a reboot.
    std::vector<HistorySample> all;
    loadFileInOrder(kFiles[0], all);
    loadFileInOrder(kFiles[1], all);
    std::sort(all.begin(), all.end(), [](const HistorySample &a, const HistorySample &b) {
        return a.epochSec < b.epochSec;
    });
    size_t start = all.size() > kRamCapacity ? all.size() - kRamCapacity : 0;
    for (size_t i = start; i < all.size(); i++) pushRam(all[i]);

    // Resume appending to whichever file is smaller (likely the "fresher" one).
    size_t sz0 = LittleFS.exists(kFiles[0]) ? LittleFS.open(kFiles[0], FILE_READ).size() : 0;
    size_t sz1 = LittleFS.exists(kFiles[1]) ? LittleFS.open(kFiles[1], FILE_READ).size() : 0;
    g_activeFile = (sz1 < sz0) ? 1 : 0;
}

void sampleNow(const BatteryStatus &status) {
    HistorySample s;
    s.epochSec = (uint32_t)time(nullptr);
    s.packVoltage = status.packVoltage;
    s.packCurrent = status.packCurrent;
    s.soc = status.haveSoc ? (int16_t)status.soc : -1;
    s.packTemperature = status.packTemperature;
    pushRam(s);
    appendToFile(s);
}

size_t getRecent(HistorySample *out, size_t maxCount) {
    size_t n = min(maxCount, g_count);
    // Oldest-first order: walk back `n` entries from g_head.
    size_t startIdx = (g_head + kRamCapacity - n) % kRamCapacity;
    for (size_t i = 0; i < n; i++) {
        out[i] = g_ring[(startIdx + i) % kRamCapacity];
    }
    return n;
}

} // namespace History

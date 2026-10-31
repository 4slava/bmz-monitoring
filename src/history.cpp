#include "history.h"
#include "debug/log_sink.h"
#include <Arduino.h>
#include <LittleFS.h>

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

// Reads only the last `want` samples from `path` (which holds `total` of
// them) directly into the RAM ring, oldest-first, via a seek — no
// intermediate buffer. `total` samples fit in a bounded 320KB-RAM device;
// loading every persisted sample from both files into one unbounded vector
// (the previous approach) could require hundreds of KB in one transient
// allocation, fragmenting the heap badly enough to break later small
// allocations for the rest of the session.
void appendTail(const char *path, size_t total, size_t want) {
    if (want == 0) return;
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return;
    f.seek((total - want) * sizeof(HistorySample));
    HistorySample s;
    for (size_t i = 0; i < want; i++) {
        if (f.read(reinterpret_cast<uint8_t *>(&s), sizeof(s)) != (int)sizeof(s)) break;
        pushRam(s);
    }
    f.close();
}

} // namespace

void begin() {
    if (!LittleFS.begin(true)) {
        RLOGE("LittleFS mount failed");
        return;
    }

    size_t sz[2];
    sz[0] = LittleFS.exists(kFiles[0]) ? LittleFS.open(kFiles[0], FILE_READ).size() : 0;
    sz[1] = LittleFS.exists(kFiles[1]) ? LittleFS.open(kFiles[1], FILE_READ).size() : 0;
    // Resume appending to whichever file is smaller (likely the "fresher" one).
    g_activeFile = (sz[1] < sz[0]) ? 1 : 0;
    uint8_t otherFile = 1 - g_activeFile;

    // The active file holds the most recent run of samples; the other file
    // holds the batch immediately preceding it. Replay oldest-first: the
    // tail of the older file (just enough to fill the gap), then all of the
    // active file.
    size_t countActive = sz[g_activeFile] / sizeof(HistorySample);
    size_t countOther = sz[otherFile] / sizeof(HistorySample);
    size_t fromActive = min(countActive, kRamCapacity);
    size_t fromOther = fromActive < kRamCapacity ? min(countOther, kRamCapacity - fromActive) : 0;

    appendTail(kFiles[otherFile], countOther, fromOther);
    appendTail(kFiles[g_activeFile], countActive, fromActive);
}

void sampleNow(const BatteryStatus &status) {
    HistorySample s;
    s.epochSec = (uint32_t)time(nullptr);
    int idx = status.firstPresentIndex();
    if (idx >= 0) {
        const NodeStatus &n = status.nodes[idx];
        s.packVoltage = n.packVoltage();
        s.packPowerW = n.packPowerW;
        s.soc = n.socByte5;
        if (n.haveBoardTemp) {
            s.packTemperature = (n.boardTemp[0] + n.boardTemp[1] + n.boardTemp[2]) / 3.0f;
        }
    }
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

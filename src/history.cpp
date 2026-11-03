#include "history.h"
#include "debug/log_sink.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace History {
namespace {

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

} // namespace

void begin() {
    if (!LittleFS.begin(true)) {
        RLOGE("LittleFS mount failed");
        return;
    }
    size_t sz0 = LittleFS.exists(kFiles[0]) ? LittleFS.open(kFiles[0], FILE_READ).size() : 0;
    size_t sz1 = LittleFS.exists(kFiles[1]) ? LittleFS.open(kFiles[1], FILE_READ).size() : 0;
    // Resume appending to whichever file is smaller (likely the "fresher" one).
    g_activeFile = (sz1 < sz0) ? 1 : 0;
}

void sampleNow(const BatteryStatus &status) {
    HistorySample s;
    s.epochSec = (uint32_t)time(nullptr);
    int idx = status.firstPresentIndex();
    if (idx >= 0) {
        const NodeStatus &n = status.nodes[idx];
        s.packVoltage = n.packVoltage();
        s.packPowerW = n.packPowerW;
        s.soc = (n.socValid && n.socByte5 >= 0) ? n.socByte5 : -1;
        if (n.haveBoardTemp) {
            s.packTemperature = (n.boardTemp[0] + n.boardTemp[1] + n.boardTemp[2]) / 3.0f;
        }
    }
    appendToFile(s);
}

size_t getDownsampled(HistorySample *out, size_t maxOut) {
    if (maxOut == 0) return 0;

    // Rotation (see appendToFile) only ever moves forward: whichever file
    // isn't the current "active" one is always strictly older.
    uint8_t otherFile = 1 - g_activeFile;
    File fOther = LittleFS.exists(kFiles[otherFile]) ? LittleFS.open(kFiles[otherFile], FILE_READ) : File();
    File fActive = LittleFS.exists(kFiles[g_activeFile]) ? LittleFS.open(kFiles[g_activeFile], FILE_READ) : File();
    size_t countOther = fOther ? fOther.size() / sizeof(HistorySample) : 0;
    size_t countActive = fActive ? fActive.size() / sizeof(HistorySample) : 0;
    size_t total = countOther + countActive;
    if (total == 0) return 0;

    size_t want = min(maxOut, total);
    for (size_t i = 0; i < want; i++) {
        size_t srcIdx = (i * total) / want; // 0-based position, oldest-first
        HistorySample s;
        bool ok;
        if (srcIdx < countOther) {
            fOther.seek(srcIdx * sizeof(HistorySample));
            ok = fOther.read(reinterpret_cast<uint8_t *>(&s), sizeof(s)) == (int)sizeof(s);
        } else {
            fActive.seek((srcIdx - countOther) * sizeof(HistorySample));
            ok = fActive.read(reinterpret_cast<uint8_t *>(&s), sizeof(s)) == (int)sizeof(s);
        }
        if (ok) out[i] = s;
    }
    return want;
}

} // namespace History

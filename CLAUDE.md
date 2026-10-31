# Agent notes for this project

Read this before touching anything — several of these were expensive to
learn (hours of debugging a "ghost" bug that turned out to be stale
firmware, then another round that turned out to be heap exhaustion). The
user-facing hardware/protocol docs are in README.md; this file is about
**how to work on the project effectively**, especially the deploy loop and
debugging workflow.

## The device is reached over WiFi only — assume no USB

The user runs this with USB intentionally disconnected most of the time
("remote access to the device" is the normal mode, not a fallback). Don't
ask them to plug in USB unless something below genuinely requires it
(nothing currently does). Find the device's current IP from them, or from
the OLED screen if they report it, or by having them check `/` on their
LAN.

## Deploy loop: build, then flash over HTTP, then verify — in that order

```
pio run                                           # build only (no USB needed)
curl -s -m 180 -H "Expect:" \
  -F "firmware=@.pio/build/esp32-c6-devkitc-1/firmware.bin;type=application/octet-stream" \
  http://<device-ip>/update                       # flash over OTA
```

**The OTA curl call almost always "fails"** — `HTTP 000`, curl exit code
56 or similar, after ~70-90s. This is not a real failure: the device sends
its 200 OK response and then calls `ESP.restart()` 500ms later
(`handleUpdateFinish` in `server.cpp`), which routinely wins the race
against curl finishing reading the response. **Ignore the curl exit code
for this call entirely** and go straight to verification instead.

**Verify with `buildTime`, not vibes.** `handleStatus()` in `server.cpp`
embeds `__DATE__ " " __TIME__` as `buildTime` in `/api/status`. Before
trusting *any* test result against the device, diff the reported
`buildTime` against what you just built. This one check would have saved
an entire afternoon this session — multiple "the bug is still there!"
investigations were actually against stale firmware because an upload had
silently failed.

```
curl -s -m 10 http://<device-ip>/api/status | python3 -c "import json,sys;print(json.load(sys.stdin)['buildTime'])"
```

Caveat: `buildTime` only changes when `server.cpp` itself gets recompiled.
If you only touched e.g. `twai_bus.cpp` or a vendored library file,
`buildTime` legitimately won't move — confirm via the build log instead
(`grep -i "Compiling.*yourfile.cpp" build_output`).

## `pio run -t upload` needs USB and fails silently-ish

If USB isn't connected, `pio run -t upload` prints `Error: Please specify
upload_port` and exits non-zero — but if you're running it through a shell
pipeline (e.g. `pio run -t upload | tail -40` inside a background Bash
call), **the exit code you get back is the pipeline's last command, not
pio's**. Always grep the actual output for the literal `[SUCCESS]` /
`[FAILED]` marker; don't trust a backgrounded task's reported exit code
for anything piped through `tail`/`grep` etc.

## Remote logging: use `RLOGI`/`RLOGW`/`RLOGE`, not `log_i`/`log_w`/`log_e`

Arduino's `log_i`/`log_w`/`log_e` macros on this core (arduino-esp32 3.x,
`USE_ESP_IDF_LOG` *not* defined) expand to `log_printf()` →
`ets_printf()`, which writes straight to the console UART and completely
bypasses ESP-IDF's `esp_log_write`/`esp_log_set_vprintf` hook chain.
Confirmed by reading `esp32-hal-log.h` and `esp32-hal-uart.c` directly —
there is no way to intercept it via the public vprintf-hook API on this
core/config. `src/debug/log_sink.h` provides `RLOGI`/`RLOGW`/`RLOGE`,
which print to Serial *and* push into a ring buffer served at `/api/logs`
(viewable at `/logs`). **All firmware logging should use these, not the
Arduino log macros** — anything logged via `log_i` et al. is invisible
unless a USB serial monitor happens to be attached.

If you ever need to see ESPAsyncWebServer's own internal logging
(`async_ws_log_*`), it can be redirected the same way: define
`ASYNCWEBSERVER_LOG_CUSTOM=1` in `platformio.ini` build_flags, add
`-Iinclude -Isrc` so the library can find it, and drop a header at
`include/AsyncWebServerLoggingCustom.h` defining the five
`async_ws_log_{e,w,i,d,v}` macros in terms of `RLOGE`/`RLOGI`. Remove all
three afterward — it's a debug-only technique, not something to leave
wired in permanently (it was used and reverted during this session's
investigation).

## A JsonDocument that's too big for the response fails *silently*

If you ever see `request->send(200, "application/json", out)` return a
`200 OK` with `Content-Length: 0` and an empty body — with `out.length()`
and `out.c_str()` both verified correct right before the call — the cause
is almost certainly that the `JsonDocument` (plus the resulting `String`)
needed more contiguous heap than was available at that moment, and
Arduino's `String::copy()` → `reserve()` → `realloc()` failing just quietly
`invalidate()`s the string instead of throwing or logging anything.
ArduinoJson's internal memory usage for a document with many objects/arrays
is noticeably larger than the document's final *serialized* size — don't
assume "the JSON text is only 20KB, that's fine" is the right estimate.

This is why `/api/history` and `/api/can/frames` cap how many records they
serialize per response (300 / 50) instead of returning everything in the
ring buffer. If you need to return more, raise the cap cautiously and
cross-check `freeHeap`/`maxAllocHeap` (both already exposed in
`/api/status`) before and after, on the real device — don't reason about
it from the desk.

## Battery wiring facts worth re-deriving quickly if asked again

- The battery's comms/CAN section is unpowered from the main pack
  terminals alone — it needs a separate active-high **Enable** line
  (GPIO18 here) driven before the TWAI driver starts. Without it, the
  transceiver chip on the battery's own board reads 0V regardless of
  CANH/CANL/GND wiring. See `twai_bus.cpp`.
- This unit's physical RJ45/connector pinout does **not** match the
  generic Victron Type A/B "CAN-bus BMS" cable documentation. Trace
  CANH/CANL/Enable from the battery's own CAN transceiver chip (an
  NSi1050 on this unit) rather than trusting published pinout tables.

## Protocol status

See README.md's protocol table for the message layout. Quick status of
what's actually confirmed vs. guessed, for anyone reverse-engineering
further:

- **Confirmed solid**: node addressing scheme (`base = id & ~0x3F`,
  `node = id & 0x3F`), cell voltages (0x200-0x2C0), current limit scaling
  (0x380, matches real ~28.7A spec exactly), power-not-current on 0x1C0
  bytes 2-3 (cross-checked against a real ~1300W EcoFlow charging session
  and matched a ~270W/pack estimate almost exactly).
- **Guessed, not cross-checked**: board temp scaling (×0.1°C), cell temp
  scaling (raw int8, no offset), min/max cell voltage byte layout within
  0x140 (bytes 4-5 status-flags location is solid; the 0-1/2-3 min/max
  split is the obvious convention, not independently verified).
- **Actively unresolved**: which of SOC byte 4 or byte 5 (both in 0x1C0) is
  authoritative — both are currently exposed separately rather than
  picking one; power sign convention (+charge vs +discharge not
  confirmed); purpose of the `0x80`-base heartbeat-like frame; the
  `0x3C0` balancing bitmask and its bytes 4-5; `0x380` bytes 2-3 (confirmed
  *not* capacity, otherwise unknown).

If given new reverse-engineering input (another community writeup, a new
cross-check against real charger/inverter numbers), update both
`battery_status.h`'s per-field comments and the table in README.md — they
should stay in sync, since README is explicitly the source a user reads to
judge what to trust.

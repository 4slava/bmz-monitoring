# BMZ 618031 CAN monitor (ESP32-C6)

Firmware for an ESP32-C6-WROOM-1 that monitors a BMZ 618031 LiFePO4 ESS
battery module over CAN, with a web portal (live dashboard + history
graphs), a raw CAN frame debug/sniffer view, and browser-based FOTA.

## Hardware

- ESP32-C6-WROOM-1 (16MB flash)
- WCMCU-230 CAN transceiver: CTX→GPIO20, CRX→GPIO19, 3V3/GND to board 3.3V/GND
- 0.91" SSD1306 I2C OLED: SDA→GPIO6, SCL→GPIO7, VCC/GND to board 3.3V/GND

## Protocol notes — read before enabling keep-alive

The BMZ 618031 is confirmed (via Victron's own "BMZ ESS" compatibility
documentation) to speak the industry-standard **Victron "CAN-bus BMS"
protocol**: 500 kbit/s, 11-bit IDs, little-endian. Core messages decoded
here: `0x351` (charge/discharge limits), `0x355` (SOC/SOH), `0x356`
(voltage/current/temperature), `0x35A` (alarm/warning bits, surfaced raw —
exact bit meaning isn't confirmed for this battery), `0x35E`/`0x35F`
(manufacturer/BMS name strings).

**This battery is not a passive broadcaster.** Per Victron's docs, it
expects a periodic "keep-alive" from a GX device/inverter and **shuts down
after ~10 seconds without one**. Since there is no inverter on this bus,
this firmware can optionally emulate that keep-alive (a `0x305` heartbeat,
per community reports of real GX-device traffic) — but the exact CAN
ID/payload/interval are **not an officially published spec**, only
reverse-engineered/observed. This is why keep-alive transmission:

- defaults to **off**,
- has its CAN ID, interval, and payload bytes all editable live from
  `/settings` (no reflashing needed to tune them),
- is meant to be field-verified using the `/can` raw-frame view: watch
  whether the battery starts/continues broadcasting `0x351/0x355/0x356/0x35A`
  once enabled.

The documented failure mode if this is wrong is safe: the battery just
disables its output / stops charging — not a hazard — so experimenting here
carries low risk, just inconvenience.

## Building / flashing

```
pio run                      # build
pio run -t upload            # flash
pio device monitor           # serial log
```

First boot with no saved WiFi credentials starts an AP named
`BMZ-Monitor-Setup` — connect to it and open `http://192.168.4.1/settings`
to configure your WiFi, then it will join your network on next boot/restart.

## Web portal

- `/` — live dashboard (voltage/current/SOC/SOH/temp, history graphs)
- `/can` — raw CAN frame viewer (filter by ID, pause/clear)
- `/settings` — WiFi, CAN bitrate, keep-alive tuning, optional admin password
- `/update` — browser-based firmware upload (FOTA)

## Partition layout

16MB flash, custom `partitions_16mb.csv`: two 4MB OTA app slots
(`ota_0`/`ota_1`) plus a ~7.9MB LittleFS partition used to persist recent
history across reboots (rotated across two capped files to bound flash
wear).

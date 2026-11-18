# Alice Station Open — Wi-Fi / BLE / LAN Sensor

## Overview

Turn a **rooted Yandex Station Micro** (or any device with Unisoc UWE5621 + Marlin3 chip) into a **24/7 network sensor** that logs:

| Channel | What | Method |
|---------|------|--------|
| **Wi-Fi** | All APs in range (BSSID, RSSI, SSID, freq) | `iw dev wlan0 scan` every 90s |
| **Bluetooth LE** | Nearby BLE devices (MAC, RSSI) | `bluetoothctl` LE scan every 3min |
| **LAN** | Devices on local network (IP, MAC) | `ping` sweep every 5min |

Logs are stored in `/bkupgrade/data/sensor/logs/` and survive reboots.
Events (new device appearances) are marked in `logs/events.log`.

**Requires:** OpenSSH server on the station, root access.

---

## Quick Start

### 1. Upload to station

```bash
scp -r alice-sensor/ root@STATION:/opt/alice-sensor/
ssh root@STATION 'chmod +x /opt/alice-sensor/sensor.sh'
```

### 2. Start

```bash
# manual start
ssh root@STATION '/opt/alice-sensor/sensor.sh daemon'

# or via init
ssh root@STATION '/etc/init.d/S98netwatch start'
```

### 3. Watch logs

```bash
# on station
ssh root@STATION 'tail -f /bkupgrade/data/sensor/logs/events.log'

# full report
ssh root@STATION '/opt/alice-sensor/report.sh'
```

---

## Files

```
alice-sensor/
  sensor.sh      — main sensor daemon (the one to run)
  config         — tuning (scan intervals, retention)
  report.sh      — human-readable report of collected data
  S99sensor      — init script stub
```

---

## Architecture

```
sensor.sh
  ├── scan_wifi()   → iw dev wlan0 scan → wifi.log
  ├── scan_ble()     → btmon + bluetoothctl LE → ble.log
  ├── scan_lan()     → ping sweep /proc/net/arp → lan.log
  └── note_event()   → new device → events.log + state/$kind/$mac
  cleanup()          → removes records older than LOG_MAX_DAYS
```

**BASE:** `/bkupgrade/data/sensor/` (UBIFS, ~50MB free, survives reboot)

---

## Tuning — `config`

| Variable | Default | Description |
|----------|---------|-------------|
| `WIFI_EVERY` | `90` | Wi-Fi scan interval (seconds) |
| `BLE_EVERY` | `180` | BLE scan interval (seconds) |
| `LAN_EVERY` | `300` | LAN sweep interval (seconds) |
| `BLE_SCAN_SEC` | `8` | Duration of each BLE scan |
| `LOG_MAX_DAYS` | `3` | Delete records older than this |

---

## Autostart on Reboot

The station's `/etc/init.d/S98netwatch` is patched to start the sensor automatically on every boot. The patch is stored in `/bkupgrade/etc/init.d/S98netwatch`.

If you ever reflash the firmware, re-apply the patch:
```bash
# From this repo root:
scp alice-sensor/patches/S98netwatch.patch root@STATION:/tmp/
ssh root@STATION 'patch -p1 < /tmp/S98netwatch.patch'
```

---

## Kernel Module — alice-tools (optional)

`alice-tools/` contains `alice-tools.ko` — a kernel module that provides debugfs interfaces for reverse-engineering the `sprdwl_ng` driver and communicating with the Marlin3 firmware directly.

**Only needed if you want to experiment with firmware commands.**

```
/sys/kernel/debug/alice/priv    — driver internals (priv ptr, ops table)
/sys/kernel/debug/alice/syncver — trigger firmware sync_version command
/sys/kernel/debug/alice/fwcmd    — send raw firmware command
/sys/kernel/debug/alice2/addr   — set kernel memory address for peek
/sys/kernel/debug/alice2/peek   — read arbitrary kernel memory (safe)
```

Build (requires aarch64 toolchain and kernel headers):
```bash
# Inside the provided Docker container (iwbuild):
cd kmod/alice-tools && make
```

---

## Firmware Analysis Notes

`fw-analysis/` contains Python scripts used to reverse-engineer the `wcnmodem.bin` firmware:

- `nametbl.py` — extract firmware command/event enum (64 commands, 39 events)
- `parse.py` — parse WCNE firmware container format
- `jtab.py` — find and decode the firmware command dispatch table

Key findings:
- `SNIFFER_MODE_SWITCH` = **command 85** (not 62 — 62 is blocked by driver's `api_available_check`)
- `sprdwl_cmd_send_to_ic()` bypasses the driver gate
- Monitor mode via firmware is **not yet achievable** — the sniffer command payload format is unknown

---

## Hardware

Tested on:
- **Yandex Station Micro v1.2** (yandex-station-lite)
- Chipset: Unisoc UWE5621 (Marlin3), Wi-Fi + Bluetooth
- Kernel: 4.9.68 aarch64 SMP
- Storage: UBI/UBIFS (`/bkupgrade` ~87MB, ~50MB free)

No USB Wi-Fi adapter needed — uses the built-in chip only.

---

## Known Issues

1. **Monitor mode not available** — the firmware's `SNIFFER_MODE_SWITCH` (cmd 85) is blocked by the driver. Sending it hangs Wi-Fi.
2. **Multiple sensor processes** — if the sensor is started multiple times, extra shells accumulate. Use `sensor.sh stop` to clean up.
3. **BLE RSSI = -127** — some BLE frames have invalid RSSI; these are filtered out.

---

## License

MIT. The `sprdwl_ng` driver binary (`sprdwl_ng.orig.ko`) and `wcnmodem.bin` firmware are proprietary Yandex/Unisoc software and are NOT included.

## Author

Reverse-engineering by @youruser — see `alice-notes.md` for full technical documentation.

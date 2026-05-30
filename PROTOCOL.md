# RIKA Firenet 2.0 — USB CDC Protocol

Reverse-engineered protocol for the RIKA Firenet 2.0 WiFi dongle.  
All findings are empirically confirmed against live stove logs or binary analysis of the official firmware (DOMO AVR32 v2.29.585.12 and WifiUpdateCustomer V2.0.0.15).

---

## Physical layer

- **Interface**: USB CDC (Abstract Control Model)
- **VID / PID**: `0x303A` / `0x819A`
- **USB roles**: Stove = USB host · Dongle = USB device
- **Framing**: ASCII lines terminated with `\n` or `\r\n`
- **Encoding**: 7-bit ASCII; numeric values as decimal strings

---

## Handshake sequence

### 1. USB reset (stove-initiated)
The stove sends a `\x16` (SYN) byte to reset the dongle state machine.

**Dongle → Stove:**
```
GET_CDCDEVICE3_VERSION=0; BL=999; APP=201; REV=12201; DT=3;
```

**Expected stove response:**
```
GET_CDCDEVICE_VERSION_FINISHED
```

---

### 2. Phase 1 — Initial announcement
Dongle announces itself with blank credentials (no WiFi info yet).  
Sent as a single atomic block:

**Dongle → Stove:**
```
POST_CDCDEVICE_STATUS=0;
<20 fields — see CDC status fields below, all blank>
-------
GET_CDCDEVICE_STATUS=0;
<20 fields — blank>
0
0
0
```

**Expected stove response:** Two echoes of `POST_CDCDEVICE_STATUS` (the stove echoes back its own CDC status).

---

### 3. Phase 2 — Authentication (1 000 ms after Phase 1)

**Step 1 (t = 0):** `GET_CDCDEVICE_STATUS=0;` with **full** fields (id, token, SSID, IP, MAC, RSSI) if WiFi credentials are available; blank fields if in provisioning mode.

**Step 2 (t = 300 ms):** `POST_CDCDEVICE_STATUS=0;` with full/blank fields (same rule).

**Expected stove response:** Echo of `POST_CDCDEVICE_STATUS` → `-------` → `OK`

---

### 4. Main loop entry

On receiving `OK` from the stove:

**Dongle → Stove:**
```
OK
START_YYYYMMDD_HHMMSS;    (RTC timestamp)
GET_NETWORKS_FINISHED     (triggers first poll cycle)
```

---

## CDC status fields (20 fields, `\n`-separated)

Sent in both `POST_CDCDEVICE_STATUS` and `GET_CDCDEVICE_STATUS`.

| # | Name | Blank | Full |
|---|---|---|---|
| 1 | monitoring | `0` | `0` |
| 2 | on_off | `1` | `1` |
| 3 | scan_command | `0` | `0` |
| 4 | init_command | `0` | `0` |
| 5 | initialised | `0` | `1` |
| 6 | symbol | `5` (disconnected) | `4` (connected) |
| 7 | error | `0` | `0` |
| 8 | bl_version | `999` | `999` |
| 9 | app_version | `201` | `201` |
| 10 | app_revision | `12201` | `12201` |
| 11 | spwf_version | `0` | `229` |
| 12 | rssi | `0` | RSSI dBm |
| 13 | id | `` | stove ID |
| 14 | token | `` | stove token |
| 15 | protocol | `3` | `3` |
| 16 | ssid | `` | SSID as hex string |
| 17 | wpa2 | `` | WiFi password |
| 18 | ip | `` | dongle IP |
| 19 | mac | `` | dongle MAC |
| 20 | cdc_device | `1` | `1` |

`GET_CDCDEVICE_STATUS` has 3 additional OTA fields (`0\n0\n0\n`) after field 20.  
`POST_CDCDEVICE_STATUS` is terminated with `-------\n`.

**Symbol values:**

| Value | Meaning |
|---|---|
| 4 | WiFi connected |
| 5 | WiFi disconnected / provisioning |
| 7 | WiFi scan complete — triggers network list display on stove panel |

---

## Poll cycle

Triggered by `GET_NETWORKS_FINISHED` (either sent by the dongle itself, or received from the stove). Repeats every ~30 s.

```
Dongle → Stove : GET_CONTROLS=1; onOff=12201; operatingMode=<onOff>; heatingPower=<opMode>; tempRoomTarget=<power>;
                 =<tempRoomTarget>;
[50 ms pause]
Dongle → Stove : GET_REVISION=0; revision=12201; frequency=30;
                 GET_SENSORS=0;
[100 ms drain]
Dongle → Stove : TRANSFER_COMPLETED
[2 000 ms drain]
Dongle → Stove : TRANSFER_COMPLETED
[500 ms drain]
```

---

## GET_CONTROLS / POST_CONTROLS

### Sending controls (dongle → stove)

```
GET_CONTROLS=1; onOff=12201; operatingMode=<onOff>; heatingPower=<opMode>; tempRoomTarget=<power>;
=<tempRoomTarget>;
```

**Field mapping (shifted format)** — the official firmware always sends fields shifted by one position, with `12201` as an artefact in the `onOff` slot:

| Wire field name | Actual value |
|---|---|
| `onOff` | always `12201` (artefact) |
| `operatingMode` | desired `onOff` (0 or 1) |
| `heatingPower` | desired `operatingMode` |
| `tempRoomTarget` | desired `heatingPower` |
| `=N;` (line 2) | desired `tempRoomTarget` (×10) |

### Stove echo (POST_CONTROLS)

The stove replies with its own stored values using the same shifted format:

```
POST_CONTROLS=0; onOff=12201; operatingMode=<onOff>; heatingPower=<opMode>; tempRoomTarget=<power>;
=<tempRoomTarget>;
```

**Important:** `GET_CONTROLS` takes effect on the stove (observed empirically — stove starts/stops). The `POST_CONTROLS` echo does **not** reflect the values just written; it returns independently stored values. The exact storage mechanism is unknown.

**Parse by position, not by field name** — the field names in POST_CONTROLS are shifted and misleading.

### Controls fields

| Field | Range | Notes |
|---|---|---|
| `onOff` | 0 / 1 | 0 = off, 1 = on |
| `operatingMode` | 0–3 | 0=Manual, 1=Auto/thermostat, 2=Comfort, 3=Setback |
| `heatingPower` | 30–100 | % (label: `[30-100]`) |
| `tempRoomTarget` | 140–280 | ×10 — 210 = 21.0 °C |

---

## GET_SENSORS / POST_SENSORS

### Request
```
GET_SENSORS=0;
```

### Response
The stove sends fields positionally, one per line, no field names:

```
POST_SENSORS=0;
=<f0>;
=<f1>;
...
```

### Known fields (positional order)

In **standby**, only `f0` is sent.

| Index | Confirmed | Internal name | Description |
|---|---|---|---|
| f0 | Yes (empirical) | `sRoomTemp_ACT` | Room temperature ×10 (e.g. 213 = 21.3 °C) |
| f1 | No | `usSubState` | Sub-state |
| f2 | No | `bIgnition` | Igniter active |
| f3 | No | `bExternal_ACT` | External room sensor active |
| f4 | No | `lFlameTemp_ACT` | Flue/flame temperature °C |
| f5 | No | `ulError_ACT` | Active error bitmask |
| f6 | No | `uiWarning_ACT` | Active warning bitmask |
| f7 | No | `uiDischargeMotor_ACT` | Discharge motor speed |
| f8 | No | `uiInsertionMotor_ACT` | Pellet auger speed |
| f9 | No | `uiIDFan_ACT` | Induced draft fan speed (RPM) |
| f10 | No | `uiAirFlaps_ACT` | Air flap position |
| f11 | No | `ulRuntimePellets` | Total pellet runtime (min) |
| f12 | No | `ulRuntimeLogs` | Total log runtime (min) |

Empirically observed values for f0: 191=19.1°C, 213=21.3°C, 234=23.4°C, 261=26.1°C.

**Note:** f0 was initially documented as `usMainState` (machine state 0–7). Empirical testing confirmed it is `sRoomTemp_ACT`. Values 0–7 were never observed in f0; `sRoomTemp_ACT` matches ambient temperature measurements.

---

## TRANSFER_COMPLETED

Sent by the dongle to flush the stove's response queues.

- **TC1** (after 100 ms drain): dequeues `POST_SENSORS`
- **TC2** (after 2 000 ms drain): dequeues `POST_CONTROLS`

Slot priority: POST_CONTROLS > POST_SENSORS. Each slot is consumed once per cycle.

---

## Keepalive (every 5 s)

**Provisioning mode:**
```
POST_CDCDEVICE_STATUS=0; <blank fields> -------
```
The stove echoes its stored credentials (SSID, WPA2, id, token) — this is how the dongle retrieves WiFi credentials after the user selects a network on the stove screen.

**Connected mode:**
```
GET_CDCDEVICE_STATUS=0; <full fields, symbol=4>
POST_CDCDEVICE_STATUS=0; <full fields, symbol=4>
```
The stove echoes back `POST_CDCDEVICE_STATUS` with its own stored values. **Field 3 (`scan_command`) of that echo** is monitored to detect WiFi scan requests (see WiFi scan flow below).

A 5 s interval provides fast detection of `scan_command=1` while staying well within the 360 s watchdog.

---

## WiFi scan and provisioning

### Triggering a scan

When the user opens the WiFi settings screen on the stove panel, the stove sets `scan_command=1` in **field 3** of the `POST_CDCDEVICE_STATUS` echo it sends in response to the dongle's keepalive. The dongle reads this field in the echo and triggers a WiFi scan.

### Scan flow (confirmed empirically)

```
Stove → Dongle : POST_CDCDEVICE_STATUS (field 3 = scan_command = 1)
Dongle         : WiFi.scanNetworks() [async, ~4 s]
Dongle → Stove : GET_NETWORKS=1;\n<HEX_SSID1>=<RSSI1>\n...<HEX_SSIDn>=<RSSIn>\n
Stove → Dongle : GET_NETWORKS_FINISHED   (stove acknowledges the list)
Dongle → Stove : GET_CDCDEVICE_STATUS=0; <full fields, symbol=7>
Stove          : displays the network list on the panel
```

**Key points:**
- `symbol=7` in `GET_CDCDEVICE_STATUS` is the **display trigger** — the stove only renders the network list after receiving it. Without `symbol=7`, the list is received but never shown.
- `symbol=7` must be sent **in response to the stove's `GET_NETWORKS_FINISHED`**, not immediately after `GET_NETWORKS`. The stove sends `GET_NETWORKS_FINISHED` after parsing the full network list.
- **Do NOT send `GET_NETWORKS_FINISHED` from dongle → stove during the scan flow.** The stove interprets it as a poll trigger and resets the network list, causing a "Réseau pas trouvé" error.
- SSID encoding: uppercase hex string, one byte = two hex digits (e.g. `44696575` for `Dieu`).
- RSSI: signed decimal integer in dBm (e.g. `-65`).
- Maximum 16 networks per list.
- `WiFi.scanDelete()` must be called **after** reading all SSIDs and RSSIs — calling it before zeroes out the results.

### Performance tip

Perform a background WiFi scan every 30 s and cache the result. When `scan_command=1` is detected, serve the cached list immediately instead of waiting ~4 s for a fresh scan. This keeps the total response time well within the stove's ~5 s display window.

### Network selection by user

After the stove displays the list and the user selects a network, the stove sends the new credentials in the `POST_CDCDEVICE_STATUS` echo:

- Field 16: new SSID (hex-encoded)
- Field 17: new WPA2 password (plaintext)

The dongle saves these to its own persistent storage (ESP32 NVS via `Preferences`) and reconnects.

---

## Error / warning bitmasks

### Warnings (f6 / `uiWarning_ACT`)

| Bit | Meaning |
|---|---|
| 0 | Low pellet level |
| 1 | Room sensor lost — switch to Manual mode |
| 3 | Maintenance due |
| 4 | Cleaning required |

### Errors (f5 / `ulError_ACT`)

| Bit | Code | Meaning |
|---|---|---|
| 0 | F00 | Ignition failure |
| 1 | F01 | Flame loss during operation |
| 2 | F02 | Overtemperature |
| 3 | F03 | Pellet sensor fault |
| 4 | F04 | Flue sensor fault |
| 5 | F05 | IDFan fault |

---

## Watchdog

The stove reboots the dongle after **360 seconds** without a `POST_CDCDEVICE_STATUS`. The dongle must send a keepalive at least every 6 minutes.

---

## Notes and caveats

- `GET_CONTROLS` must be sent **before** `GET_REVISION` in the poll cycle to trigger the `POST_CONTROLS` TC slot correctly.
- The field order in `POST_CONTROLS` is shifted by one; always parse positionally.
- `tempRoomTarget` is **always ×10** on the wire in both directions.
- `heatingPower` minimum is 30 in the protocol but the stove may reject values below 50 depending on model.
- Firenet v2 stoves (`symbol_current == 2`) use `GET_FIRENET_STATUS` / `POST_FIRENET_STATUS` instead of the CDC device status commands — not implemented here.

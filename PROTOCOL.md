# RIKA Firenet 2.0 — USB CDC Protocol

Reverse-engineered protocol for the RIKA Firenet 2.0 WiFi dongle, as understood from
live testing against a real stove (RIKA DOMO, firmware V2.29.585.12) and analysis of
the official firmware.

This document describes the protocol as we understand it — the wire format, the
message flow, and the behaviours you must reproduce for a stove to accept a
replacement dongle. Everything below has been observed on a real stove unless marked
otherwise.

---

## Physical layer

- **Interface**: USB CDC (Abstract Control Model)
- **VID / PID**: `0x303A` / `0x819A`
- **USB roles**: stove = USB **host**, dongle = USB **device**
- **Encoding**: 7-bit ASCII; numeric values as decimal strings
- **Framing**: ASCII, delimiter-based (`=`, `;`, space). Lines are grouped into a
  frame by a short period of silence on the link.

Two low-level details matter and are easy to get wrong:

1. **The stove never asserts DTR.** An Arduino-style `USBSerial.write()` that gates on
   `tud_cdc_n_connected()` will send nothing and the stove will just keep probing.
   Write straight to the TinyUSB FIFO instead.
2. **Send large frames in small chunks (see “USB framing” below).** A single big
   write is rejected by the stove's USB host.

---

## Handshake sequence

### 1. USB reset probe (stove-initiated)

The stove repeatedly sends a 2-byte probe — `0x16` (SYN) followed by the ASCII
character `3` — roughly every 100 ms until the dongle answers acceptably. The trailing
`3` selects the “version 3” variant of the handshake.

**Dongle → Stove:**
```
GET_CDCDEVICE3_VERSION=0; BL=999; APP=201; REV=12201; DT=3;
```

**Expected stove response:**
```
GET_CDCDEVICE_VERSION_FINISHED
```

`BL` / `APP` / `REV` are the dongle's own firmware version numbers; `DT=3` is the
device type. These values do **not** decide whether the handshake is accepted — a
DOMO 2.29 finishes the handshake with `BL=112` and `BL=999` alike. Their real use is
the firmware-update check (a high `BL` such as `999` tells the stove/cloud the dongle
is up to date, so it never tries to push an OTA firmware onto it).

If the stove keeps re-sending the `0x16 3` probe and never sends
`GET_CDCDEVICE_VERSION_FINISHED`, the reply is either not reaching it (framing / DTR)
or its firmware family expects a different reply — this has been seen on non-DOMO
models (e.g. Induo) and is not yet solved.

### 2. Announcement (blank status)

Once the version is acknowledged, the stove pushes an initial
`POST_CDCDEVICE_STATUS`. The dongle answers with a `GET_CDCDEVICE_STATUS` carrying
blank credentials while unprovisioned, or full credentials once connected (see below).

### 3. Main loop

From there the dongle drives a periodic poll cycle and a keepalive. There is no
persistent authentication step; the stove is purely reactive.

---

## CDC status fields (20 fields, `\n`-separated)

Sent in both `POST_CDCDEVICE_STATUS` (stove → dongle) and `GET_CDCDEVICE_STATUS`
(dongle → stove).

| # | Name | Blank | Full (connected) |
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
| 12 | rssi | `0` | RSSI in dBm |
| 13 | id | `` | stove ID |
| 14 | token | `` | stove token |
| 15 | protocol | `3` | `3` |
| 16 | ssid | `` | SSID as hex string |
| 17 | wpa2 | `` | WiFi password (plaintext) |
| 18 | ip | `` | dongle IP |
| 19 | mac | `` | dongle MAC |
| 20 | cdc_device | `1` | `1` |

> [!TIP]
> In Open-Firenet, field 17 (`wpa2`) is automatically sanitized to `********` by `sanitizeForLog()` before being emitted to debug logs, the Web UI console, or Serial output, preventing accidental exposure of private WiFi credentials when sharing diagnostic traces.

`GET_CDCDEVICE_STATUS` (dongle → stove) has 3 extra OTA fields (`0\n0\n0\n`) after
field 20. `POST_CDCDEVICE_STATUS` (stove → dongle) is terminated with `-------\n`.

**`symbol` values** — the WiFi icon shown on the stove panel:

| Value | Meaning |
|---|---|
| 4 | WiFi connected |
| 5 | WiFi disconnected / provisioning (grey + red cross) |
| 7 | WiFi scan complete — triggers the network-list display on the panel |

Note: `initialised` and `symbol` are recomputed by the stove according to its own view
of the connection; pushing `symbol=4` / `initialised=1` does not by itself make the
stove report “connected”. It does, however, adopt values such as `rssi`, `ssid` and
`wpa2` from what the dongle pushes.

---

## Poll cycle

Once the version is acknowledged, the dongle runs a periodic cycle (~every 2 s works;
the official firmware uses ~30 s). Each cycle it registers what it wants to read, then
flushes the stove's response queues:

```
Dongle → Stove : GET_CONTROLS=0; <control names...>
Dongle → Stove : GET_SENSORS=0; <sensor names...>
Dongle → Stove : GET_REVISION=<rssi>; revision=<rev>; frequency=<freq>;
Dongle → Stove : TRANSFER_COMPLETED
Dongle → Stove : TRANSFER_COMPLETED
```

`GET_REVISION` is mandatory before `TRANSFER_COMPLETED` — without it the stove's
response slots are never armed and `TRANSFER_COMPLETED` returns nothing. The stove
ignores the `revision=` and `frequency=` values themselves; only the presence of the
command matters.

---

## GET_CONTROLS / POST_CONTROLS

Controls are read and written positionally. The stove keeps a small set of core
controls (revision, on/off, mode, target stage, room target). A nameless read returns
them positionally:

```
POST_CONTROLS=0; =<revision>; =<onOff>; =<mode>; =<stage>; =<roomTarget>;
```

To **write**, send `GET_CONTROLS=1;` with the full set of values (read-modify-write:
start from the last values read, change only what you need):

```
GET_CONTROLS=1; revision=<rev>; onOff=<v>; mode=<v>; targetStage=<v>; roomTarget=<v>;
```

**Parse by position, not by name.** A leading artefact value (the revision) can shift
the apparent field names by one; always read the values in order and ignore the names.

`GET_CONTROLS=1` takes effect on the stove (confirmed — it actually starts/stops and
changes setpoint). The `POST_CONTROLS` echo returns the stove's stored values, which
may lag what was just written.

### Control fields

| Position | Field | Range | Notes |
|---|---|---|---|
| 0 | revision | — | stove config revision |
| 1 | onOff | 0 / 1 | 0 = off, 1 = on |
| 2 | mode | 0–3 | 0 = Manual, 1 = Auto/thermostat, 2 = Comfort, 3 = Setback |
| 3 | targetStage | 30–100 | heating power, % |
| 4 | roomTarget | 140–280 | room target ×10 (210 = 21.0 °C) |
| 5 | bakeTarget | — | bake target temperature (DOMO BACK) |
| 6 | (reserved) | — | reserved control slot |
| 7 | heatTimeMon1 | 0, decimal | Monday slot 1 (start/end encoded as integer) |
| 8 | heatTimeMon2 | 0, decimal | Monday slot 2 |
| 9 | heatTimeTue1 | 0, decimal | Tuesday slot 1 |
| 10 | heatTimeTue2 | 0, decimal | Tuesday slot 2 |
| 11 | heatTimeWed1 | 0, decimal | Wednesday slot 1 |
| 12 | heatTimeWed2 | 0, decimal | Wednesday slot 2 |
| 13 | heatTimeThu1 | 0, decimal | Thursday slot 1 |
| 14 | heatTimeThu2 | 0, decimal | Thursday slot 2 |
| 15 | heatTimeFri1 | 0, decimal | Friday slot 1 |
| 16 | heatTimeFri2 | 0, decimal | Friday slot 2 |
| 17 | heatTimeSat1 | 0, decimal | Saturday slot 1 |
| 18 | heatTimeSat2 | 0, decimal | Saturday slot 2 |
| 19 | heatTimeSun1 | 0, decimal | Sunday slot 1 |
| 20 | heatTimeSun2 | 0, decimal | Sunday slot 2 |
| 21 | heatingTimesActive | 0 / 1 | Heating schedule active (`0` = Off, `1` = On) |
| 22 | setBackTemp | 100–250 | Setback / maintenance temp ×10 (160 = 16.0 °C) |
| 23 | convectionFan1Active | 0 / 1 | MultiAir fan 1 power state (`0` = Off, `1` = On) |
| 24 | convectionFan1Level | 0–5 | MultiAir fan 1 speed (`0` = Auto, `1`–`5` = manual level) |
| 25 | convectionFan1Area | -30 to +30 | MultiAir fan 1 convection trim/correction (%) |
| 26 | convectionFan2Active | 0 / 1 | MultiAir fan 2 power state (`0` = Off, `1` = On) |
| 27 | convectionFan2Level | 0–5 | MultiAir fan 2 speed (`0` = Auto, `1`–`5` = manual level) |
| 28 | convectionFan2Area | -30 to +30 | MultiAir fan 2 convection trim/correction (%) |
| 29 | frostProtectionActive | 0 / 1 | Frost protection enabled (`0` = Off, `1` = On) |
| 30 | frostProtectionTemp | 40–100 | Frost protection target temp ×10 (40–100 = 4.0–10.0 °C, default 50 = 5.0 °C) |
| 31 | roomTempOffset | -30 to +30 | Room temperature calibration offset ×10 |
| 32 | roomSensorPower | — | Room sensor power mode |

### Heating Schedule Slot Encoding

Each time slot (indices 7–20) is encoded as a decimal integer packing start time and end time:
```
(StartHH * 100 + StartMM) * 10000 + (EndHH * 100 + EndMM)
```
- A value of `0` indicates that the slot is disabled / inactive.
- Example: `06:00` to `08:00` is encoded as `(600 * 10000) + 800 = 6000800`.
- Example: `17:30` to `22:15` is encoded as `(1730 * 10000) + 2215 = 17302215`.

> [!NOTE]
> **MultiAir Fan Controls vs Sensors**: In earlier analysis, index 23 of `GET_SENSORS` was found to be `hopperLidClosed`. This was because `GET_SENSORS` (read-only telemetry) and `GET_CONTROLS` (read/write control registers) operate in two separate index spaces on the stove. MultiAir fans are configured and controlled strictly through the **controls** table (`GET_CONTROLS` / `POST_CONTROLS`) at positions 23–28. The stove firmware (AVR32 / ESP32) maps these to hardware convection fan PWM drivers (`FUN_80036ea4` and `FUN_80037060`).


---

## GET_SENSORS / POST_SENSORS

This is the part that unlocks the full stove telemetry, and where the mechanism is
most easily misunderstood.

### The key rule: the stove emits one sensor slot per name you register

The stove holds an internal array of sensors (up to ~88 slots). When you send
`GET_SENSORS`, it records **how many names you provided** and, when it later builds
`POST_SENSORS`, it emits exactly those slots — positionally, in index order — echoing
the names you sent back:

```
GET_SENSORS=0; s0=0; s1=0; s2=0; ... s52=0;
→ POST_SENSORS=0; s0=<v0>; s1=<v1>; ... s52=<v52>;
```

Consequences:

- The **names are your choice** and are ignored by the stove — only the **position**
  matters. `s0` maps to slot 0, `s1` to slot 1, and so on.
- If you register **N** names you get slots **0 … N-1** and nothing else. Register only
  a handful and the high-index counters are never emitted — they are not “missing”, the
  stove was never asked for them.
- To read the cumulative counters (pellet hours, total consumption, service countdown),
  you **must register names up to at least index 52**. There is no way to address slot
  47 without also naming 0…46 — the mapping always starts at 0.

A nameless / near-empty `GET_SENSORS` therefore returns just slot 0 (room temperature).
That is a registration artefact, not a limitation of the stove.

### USB framing — sending the large GET_SENSORS frame

Registering 53 sensors makes the `GET_SENSORS` frame several hundred bytes long. The
stove's USB host **aborts the bulk pipe** if it receives more than 4 full-size 64-byte
USB packets in a row without a short packet. A single large write produces back-to-back
full packets and is dropped wholesale — which is why frames larger than ~64 bytes
appear to “fail”.

The fix is to transmit the frame in **small chunks (≤ 32 bytes), flushing after each
chunk**, so every USB packet is a short packet. With that, a ~600-byte `GET_SENSORS`
frame is accepted intact and all 53 slots come back. This applies to any large frame.
(The official firmware achieves the same effect by writing each field followed by a
flush.)

### Sensor table (positional)

Values below were read live from a DOMO and matched against the stove's own screen —
room temperature, flame, pellet hours, total consumption, service countdown, model and
firmware versions are confirmed; unlabelled slots read `0` in standby.

| Index | Name | Description |
|---|---|---|
| 0 | roomTemp | Room temperature ×10 (246 = 24.6 °C) |
| 1 | flame | Flame / flue temperature (°C) — observed 18→580 range across a full live burn cycle (2026-09-18), settling ~560-580 at full regulation |
| 3 | errMask32 | Active error bitmask |
| 4 | errSub | Error sub-code |
| 5 | stateMask | Blocking-state bitmask — bit value `2` confirmed 2026-09-18 by direct action: opening the pellet hopper lid sets it to `2`, closing it returns it to `0` |
| 7 | augerSet | Pellet auger setpoint (RPM) — observed 2026-09-18 cycling on/off (~360-620) during active regulation, drops hard to `0` the instant the stove enters Burn Off |
| 9 | idFanMeas | Induced-draft fan, measured (RPM) — observed 2026-09-18: ~1500 RPM at steady regulation, briefly spikes to ~2585 at the Heating→Burn Off transition (purge), then settles back down over a few minutes |
| 10 | idFanSet | Induced-draft fan, setpoint (RPM) |
| 23 | **hopperLidClosed** | Pellet hopper lid state (`1`=closed, `0`=open) — confirmed 2026-09-18 by direct action, same event as `stateMask` bit `2` above |
| 27 | boardSensor | Board temperature sensor |

> **Correction (2026-09-18)**: an earlier hypothesis (from a separate, older research corpus at `~/dev/rika/PROTOCOL.md`, sourced from `WifiUpdateCustomer_V2.0.0.15.exe`'s PRIO1 string-table order) speculated that positions 19-25 in *that* numbering corresponded to MultiAir fan controls (`bConvectionFanActive`, `sConvectionFanLevel`, `sConvectionFan2Level`, etc.). The live confirmation above that **our own index 23 is `hopperLidClosed`**, not a MultiAir field, directly contradicts that hypothesis for this index — and was confirmed with MultiAir switched **off** on the stove during a live burn cycle, while our index 23 still toggled with the hopper lid alone. This means our bridge's `sNN` positional numbering (order of names registered via `GET_SENSORS`) is **not the same index space** as that older PRIO1 field-order theory; the two should not be assumed to line up position-for-position without independent confirmation for each index.
| 28–30 | stageCur1 / stageTgt2 / stageCur | Current / target heating stage — **indirect MultiAir effect observed 2026-09-18**: activating MultiAir 1 during an active burn immediately jumped `stageTgt2` 70→90% while `stageCur`/`stageCur1` briefly lagged at 51%, and `augerSet` restarted (0→367 RPM). No direct MultiAir on/off flag has been found on the wire, but this looks like the stove's power regulation compensating for the extra heat MultiAir draws away — an indirect signature, not a direct read of the setting. |
| 31 | mainState | Machine state — corrected 2026-09-18, matches `open-firenet.ino`'s own switch and a live burn cycle: `0`=Off, `1`=Standby, `2`=Ignition, `3`=Flame Start, `4`=Heating, `5`=Grate Cleaning, `6`=Burn Off, `7`=Split Log (previous table here, "0 Standby...5 Burnoff", was wrong) |
| 32 | subState | Sub-state |
| 33 | rssi | WiFi RSSI reported back |
| 35 | fabNumber | Fabrication number |
| 36 | model | Stove model ID (see [Known Stove Models](#known-stove-models-sensors36--model) below) |
| 37 | language | UI language index |
| 38 | appVerBoard | Main board firmware version (229 = V2.29) |
| 40 | appVersion | Same value as `status.app_version` — confirmed 2026-09-18 by cross-referencing the independently-parsed `POST_CDCDEVICE_STATUS` heartbeat |
| 43 | blVersion | Same value as `status.bl_version` — confirmed 2026-09-18 by cross-referencing the independently-parsed `POST_CDCDEVICE_STATUS` heartbeat |
| 44 | firmwareBuild | Firmware build (58512 = 585.12) |
| 45 | subVersion | Firmware sub-version |
| 46 | appRevision | Same value as `status.app_revision` — confirmed 2026-09-18 by cross-referencing the independently-parsed `POST_CDCDEVICE_STATUS` heartbeat |
| 47 | **pelletHours** | Total pellet operating time |
| 49 | **pelletsTotal** | Total pellet consumption (kg) |
| 50 | **serviceCountdown** | Consumption remaining before service (kg) |
| 51 | serviceOffset | Service interval offset |
| 52 | serviceMinutes | Service time counter |
| 53 | **ignitionCount** | Total ignition count — confirmed 2026-09-18 by direct comparison against the stove's own Info > Paramètres screen ("Nb d'allumages") |
| 54 | **onOffCycles** | Total on/off cycle count — confirmed 2026-09-18 by direct comparison against the stove's own Info > Paramètres screen ("Cycles ON/OFF") |

Indices not listed read `0` in standby and are not yet identified.

**Slot 8** is a partial exception: still unidentified, but observed 2026-09-18 to hold `0`
in standby and swing through varying non-zero values (roughly 20-112) throughout an
active burn — likely a combustion-related reading (air/flow/motor-adjacent), not a
simple flag. Exact meaning not determined.

**Unconfirmed lead — slot 39**: reads the exact same value as `appVerBoard` (index 38,
`229`) in every capture taken so far (2026-09-18). Not an artefact of the bridge —
`parseBody()` fills `raw_sensors` straight from what the stove sends positionally in
`POST_SENSORS`, so the stove genuinely emits `229` twice. Hypothesis, **not confirmed**:
this could be an "expected" vs. "actual" board-version pair used by the official OTA
validation logic (`WIFI Version OK` / `WIFI Version INVALID` states, already documented
elsewhere in this repo's OTA state-string findings) — the two would only diverge during
a real firmware update, which we have no way to trigger from our side to test this.
Left unnamed (`s39`) pending that opportunity.

Registering more names than the ~9-12 previously documented as a GET_SENSORS ceiling
works fine — no "TOO MUCH ENTRIES" error, tested live up to 150 names requested
(2026-09-18). **The request side has no practical limit found so far.**

**The stove's real ceiling is on the response side, and it is exactly 88 slots
(indices 0-87)** — confirmed empirically (2026-09-18): registering 150 names still
only ever gets a `POST_SENSORS` response populated up to `s87`; indices 88-149 are
requested but never come back with a value, with or without error. This matches the
"~88 slots" figure that appeared in this doc pre-2026-09-18 without any citation —
it is now a directly-tested fact, not an unsourced claim.

Slots 53-87 beyond `onOffCycles` return real (non-zero) values in standby but are not
yet identified.

**Resolved (2026-09-18)**: slot 82 matching `serviceCountdown` (index 50) was flagged
earlier as an unverified coincidence. During a live burn, `serviceCountdown` dropped
from `700` to `699` on real pellet consumption while **slot 82 stayed at `700`** —
confirming they are two independent values that simply happened to match, not the
same counter. Slot 82 remains unidentified.

### Known Stove Models (`sensors[36]` / `model`)

The stove reports its hardware model identifier in sensor index 36. This ID matches the 3-digit code in official Rika firmware binaries (`RIKA_<type>_<modelId>_<boardVer>_<Description>_<ModelName>_V<Version>.bin`) and the byte stored at header offset `0x0006` in test firmware. The table below was built by downloading every official firmware package from Rika's update servers and reading the model ID/code straight out of each binary's filename and header — not guessed or inferred from the wire protocol.

| Model ID | Hex | Firmware Code | Commercial Model | Type |
|:---:|:---:|:---:|:---|:---|
| **`1`** | `0x01` | `001` / `INDUO` | **RIKA INDUO** | Combined pellet and firewood stove (*Kombiofen*) |
| **`2`** | `0x02` | `002` / `TOPO` | **RIKA TOPO** | Pellet stove (*Pelletofen*) |
| **`3`** | `0x03` | `003` / `ROCO` | **RIKA ROCO** | Pellet stove with sliding glass door |
| **`4`** | `0x04` | `004` / `RCMA` | **RIKA ROCO MULTIAIR** | Pellet stove with MultiAir ducting |
| **`5`** | `0x05` | `005` / `RCAO` | **RIKA ROCO RAO** | Pellet stove with top flue connection (*RAO*) |
| **`6`** | `0x06` | `006` / `KAPO` | **RIKA KAPO** | Compact pellet stove |
| **`7`** | `0x07` | `007` / `MIRO` | **RIKA MIRO** | Pellet stove (4 kW / 6 kW) |
| **`8`** | `0x08` | `008` / `COMO` | **RIKA COMO** | Pellet stove (1st generation) |
| **`9`** | `0x09` | `009` / `REVO` | **RIKA REVO** | Pellet stove with natural stone |
| **`10`** | `0x0A` | `010` / `ITRO` | **RIKA INTERNO** | Pellet fireplace insert (*Kamineinsatz*) |
| **`11`** | `0x0B` | `011` / `FILO` | **RIKA FILO** | Customizable pellet stove |
| **`12`** | `0x0C` | `012` / `SUMO` | **RIKA SUMO** | Pellet stove with large hopper capacity |
| **`13`** | `0x0D` | `013` / `DOMO` | **RIKA DOMO** | Pellet stove (natural convection + MultiAir) |
| **`14`** | `0x0E` | `014` / `CORSO` | **RIKA CORSO** | Cylindrical round pellet stove |
| **`15`** | `0x0F` | `015` / `IND_2` | **RIKA INDUO II** | Combined pellet & firewood stove, 2nd gen |
| **`16`** | `0x10` | `016` / `REVIVO` | **RIKA REVIVO** | Pellet fireplace insert (*Kamineinsatz*) |
| **`17`** | `0x11` | `017` / `PARO` | **RIKA PARO** | Combined pellet and firewood stove (*Kombiofen*) |
| **`18`** | `0x12` | `018` / `LIVO` | **RIKA LIVO** | Pellet stove with wide panoramic view |
| **`19`** | `0x13` | `019` / `CMO_2` | **RIKA COMO II** | Pellet stove, 2nd generation |
| **`20`** | `0x14` | `020` / `RVO_2` | **RIKA REVO II** | Pellet stove, 2nd generation |
| **`21`** | `0x15` | `021` / `COSMO` | **RIKA COSMO** | Pellet stove |
| **`22`** | `0x16` | `022` / `SONO` | **RIKA SONO** | Compact pellet stove with large autonomy |
| **`23`** | `0x17` | `023` / `DOBA` | **RIKA DOMO BACK** | Pellet stove with integrated baking oven (*Backofen*) |
| **`24`** | `0x18` | `024` / `PKE` | **RIKA PK E** | Central heating pellet boiler (*Pelletkessel*) |
| **`25`** | `0x19` | `025` / `SUMA` | **RIKA SUMO MULTIAIR** | Pellet stove with MultiAir |
| **`26`** | `0x1A` | `026` / `CNECT` | **RIKA CONNECT** | Modular pellet stove (*CONNECT Pellet*) |

#### Sibling Brand: ANIMO Models (Brand ID `0x02`)

RIKA manufactures stoves under the sister brand **ANIMO** (byte 5 = `0x02` in firmware test headers):

| Model ID | Code | Model Name | Type |
|:---:|:---:|:---|:---|
| **`1`** | `001` / `AVITO` | **ANIMO AVITO** | Pellet stove |
| **`2`** | `002` / `AVSLM` | **ANIMO AVITO SLIM** | Compact slim pellet stove |
| **`3`** | `003` / `AVRAO` | **ANIMO AVITO RAO** | Pellet stove with top flue |
| **`4`** | `004` / `ADEVO` | **ANIMO ADEVO** | Pellet stove |
| **`5`** | `005` / `PURE` | **ANIMO PURE** | Pellet stove |
| **`6`** | `006` / `ADUO` | **ANIMO ADUO** | Combined pellet/wood stove |
| **`7`** | `007` / `AMITO` | **ANIMO AMITO** | Pellet stove |
| **`8`** | `008` / `ARND` | **ANIMO ARONDO** | Round pellet stove |
| **`9`** | `009` / `ADUO_2` | **ANIMO ADUO 2** | Combined pellet/wood stove, 2nd gen |

---

## TRANSFER_COMPLETED

Sent by the dongle to flush the stove's response queues. Each `TRANSFER_COMPLETED`
dequeues one pending `POST_*` frame; controls have priority over sensors. Send it
several times per cycle to drain everything (status, controls, sensors). It only
produces output after a `GET_REVISION` has armed the slots.

---

## Keepalive

The dongle must send a `POST_CDCDEVICE_STATUS` (or the normal poll traffic) regularly.
A short interval (a few seconds) gives fast reaction to scan requests while staying
well within the watchdog window.

The stove echoes its stored WiFi credentials (SSID in field 16, WPA2 in field 17) in
the status it sends back — this is how the credentials the user entered on the stove
panel become visible to the dongle.

---

## WiFi scan (network list on the stove panel)

### Trigger

When the user opens the WiFi settings screen on the stove, the stove sets
`scan_command = 1` (field 3) in the `POST_CDCDEVICE_STATUS` it sends back. The dongle
reads that field and starts a WiFi scan.

### Flow (confirmed empirically)

```
Stove → Dongle : POST_CDCDEVICE_STATUS (field 3 scan_command = 1)
Dongle         : WiFi scan (async, ~4 s)
Dongle → Stove : GET_NETWORKS=1;\n<HEX_SSID1>=<RSSI1>\n ... <HEX_SSIDn>=<RSSIn>\n
Stove → Dongle : GET_NETWORKS_FINISHED
Dongle → Stove : GET_CDCDEVICE_STATUS=0; <full fields, symbol=7>
Stove          : displays the network list on the panel
```

Key points:

- `symbol = 7` is the **display trigger** — the stove only renders the list after
  receiving it, and it must be sent **in response to** the stove's
  `GET_NETWORKS_FINISHED`, not right after `GET_NETWORKS`.
- Do **not** send `GET_NETWORKS_FINISHED` from dongle → stove during the scan; the
  stove treats it as a poll trigger and drops the list (“network not found”).
- SSID encoding: uppercase hex, two hex digits per byte (`44696575` = `Dieu`).
- RSSI: signed decimal dBm.
- At most 16 networks per list.
- Caching a background scan and serving it on demand keeps the response within the
  stove's display window.

---

## WiFi provisioning (dongle side)

Two ways to give the dongle its own WiFi credentials:

- **Captive portal** — on first boot (or after a reset) the dongle starts an open
  access point `Open-Firenet-Setup`; the captive portal lets you pick your 2.4 GHz
  network and enter the password. Credentials are stored and the dongle reboots into
  station mode.
- **Serial command** — send `SETWIFI:<ssid>:<password>` over the ESP32 serial port
  (115200 baud). The SSID ends at the first `:`; everything after it is the password.

Robust station connection on the ESP32-S3 (coexisting with native USB): disable WiFi
power save, set TX power, configure the station, and connect with a short delay after
setup. A plain `WiFi.begin()` alone tends not to associate.

---

## Error / warning bitmasks

### Warnings

| Bit | Meaning |
|---|---|
| 0 | Low pellet level |
| 1 | Room sensor lost — switch to Manual mode |
| 3 | Maintenance due |
| 4 | Cleaning required |

### Errors

| Bit | Code | Meaning |
|---|---|---|
| 0 | F00 | Ignition failure |
| 1 | F01 | Flame loss during operation |
| 2 | F02 | Overtemperature |
| 3 | F03 | Pellet sensor fault |
| 4 | F04 | Flue sensor fault |
| 5 | F05 | Induced-draft fan fault |

---

## Watchdog

The stove reboots the dongle after **360 seconds** without a `POST_CDCDEVICE_STATUS`.
Keep the keepalive well under that.

---

## Notes and caveats

- The stove never asserts DTR — write directly to the USB CDC FIFO.
- Send large frames in ≤ 32-byte flushed chunks (short packets) or the stove's USB host
  aborts the pipe.
- Register **all** sensor names (0…52) to get the high-index counters; the stove only
  emits the slots you name, starting at 0.
- Parse `POST_CONTROLS` / `POST_SENSORS` **positionally**; the field names are your own
  and can appear shifted by a leading artefact value.
- `roomTarget` / room temperature are **×10** on the wire in both directions.
- `GET_REVISION` must precede `TRANSFER_COMPLETED`, or nothing is returned.
- Firenet v2 stoves (`symbol_current == 2`) use `GET_FIRENET_STATUS` /
  `POST_FIRENET_STATUS` instead of the CDC device-status commands — not covered here.

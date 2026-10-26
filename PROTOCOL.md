# RIKA Firenet 2.0 — USB CDC Protocol

Reverse-engineered protocol for the RIKA Firenet 2.0 WiFi dongle, as understood from
live testing against real stoves (RIKA DOMO, mainboard firmware 2.29; RIKA INDUO,
firmware 2.26 and 2.27) and the disassembly of the stove firmware (INDUO 2.27,
INDUO II 2.28, DOMO 2.29) and of the official dongle firmware.

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

### 1. USB reset probe and version frame

The stove repeatedly sends a short probe — `0x16` (SYN) followed by an ASCII digit —
until the dongle answers with a version frame. The probe is the same on every stove
generation, so it does not tell which frame to send.

Three version frames exist; what decides is the **mainboard firmware version**, not the
stove model:

| Stove firmware | Version frame sent by the dongle | Stove reply |
|---|---|---|
| 2.29 (e.g. DOMO) | `GET_CDCDEVICE3_VERSION=0; BL=999; APP=201; REV=12201; DT=3; ` | `GET_CDCDEVICE_VERSION_FINISHED` |
| 2.28 (e.g. INDUO II, SONO) | `GET_CDCDEVICE_VERSION=0; BL=101; APP=112; REV=13301; DT=1; ` | `GET_CDCDEVICE_VERSION_FINISHED` |
| 2.26 / 2.27 (e.g. INDUO) | `GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; BL=101; APP=111; REV=360; DT=1; ` | `GET_WIFI_VERSION_FINISHED` |

Rules read in the stove firmware, and why the order matters:

- The stove replies `*_FINISHED` **before** it validates the version, so the reply does
  not mean the frame was accepted.
- 2.26 / 2.27 accept only `APP=111`, 2.28 only `APP=112` (or `APP=1` with `DT=2`); any
  other value sends the stove into its "offline update" state (screen "Firenet UPDATE",
  replies `02 30 03` to everything). 2.29 does not reject on `BL`/`APP`/`REV`.
- 2.26 / 2.27 know no `GET_CDCDEVICE*` command at all: the 2.28 frame (no
  `GET_WIFI_VERSION` in it) falls through their command decoder without effect.
- 2.28 recognises both `GET_WIFI_VERSION=0` and `GET_CDCDEVICE_VERSION`: it must get the
  2.28 frame before the 2.26/2.27 one, whose `APP=111` it would reject.

Open Firenet therefore probes 2.29 → 2.28 → 2.26/2.27, about 3 s each, one frame at a
time, and keeps the first one acknowledged. After a link reset it tries the family that
worked first. `version_frame` in `/api/state` reports it (`V3`, `V28`, `V1`).

### 2. Announcement (Status Handshake)

- **2.29:** The stove pushes an initial `POST_CDCDEVICE_STATUS`.
  The dongle answers with a `GET_CDCDEVICE_STATUS` carrying blank credentials while unprovisioned, or full credentials once connected.
- **2.28:** Same CDC status commands as 2.29 (hex SSID, protocol `3`), but the dongle
  pushes its `GET_CDCDEVICE_STATUS` right after the version reply, with an **8-digit id**.
  Confirmed on a real LIVO 2.28; a bare `POST_CDCDEVICE_STATUS` is answered with the
  stove's status, `POST_FIRENET_STATUS` is ignored.
- **2.26 / 2.27:** The dongle must push `GET_FIRENET_STATUS=0;\n` (19 fields)
  right after the version reply. The stove decodes no command before this status is
  accepted; the link is then decided at the next `TRANSFER_COMPLETED`. The **id must be
  exactly 8 digits and the token 8 printable characters**, otherwise the stove raises
  error **UW27**. A stove that is never linked raises **UW29** after about 24 attempts.
  A bare `POST_FIRENET_STATUS` is a request the stove always answers with its own status.

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
> In Open Firenet, field 17 (`wpa2`) is automatically sanitized to `********` by `sanitizeForLog()` before being emitted to debug logs, the Web UI console, or Serial output, preventing accidental exposure of private WiFi credentials when sharing diagnostic traces.

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

**2.26 / 2.27 / 2.28 (read in the stove firmware and confirmed on real 2.26 / 2.27
stoves):**

- A `GET_SENSORS` or `GET_CONTROLS` frame **replaces** the list of registered names; a
  frame without names empties it. The names are therefore registered **once** after the
  handshake (flag `0` = "send everything once"), then each cycle only sends
  `GET_REVISION` + `TRANSFER_COMPLETED`.
- The stove prepares its data in the `GET_REVISION` handler and emits one `POST_*` per
  `TRANSFER_COMPLETED` (controls first). It only re-sends records whose value changed;
  a full refresh of all sensors comes every 30th `GET_REVISION`.
- A stove idling in standby can thus stay silent for more than a minute: Open Firenet
  sends a bare `POST_FIRENET_STATUS` every ~20 s, which the stove always answers, so its
  own 60 s "no data" watchdog does not restart the link.
- Command frames (`GET_CONTROLS=1; ...`) also replace the registered control names: the
  names are registered again at the next poll.

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
| 5 | bakeTarget | 130–340 | bake target temperature, °C (DOMO BACK model 23) — official `bakeTemperature` |
| 6 | ecoMode | 0 / 1 | eco mode — official `ecoMode`; only settable when sensor `ecoModePossible` is 1 |
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
| 31 | roomTempOffset | -40 to +40 | Room temperature sensor calibration offset ×10 (-4.0 °C to +4.0 °C) |
| 32 | roomSensorPower | — | official `RoomPowerRequest` |
| 33–37 | debug0 … debug4 | — | official `debug0` … `debug4`, the same variables as sensors 83–87 |

The names are the official ones of the RIKA cloud `controls` object, in the same order
(`operatingMode` = mode, `heatingPower` = targetStage, `targetTemperature` = roomTarget,
`heatingTimesActiveForComfort`, `setBackTemperature`, `temperatureOffset`...).

**2.26 / 2.27:** the table has no `bakeTarget` record: position *p* is position *p* above
for *p* < 5 and *p* + 1 from 5 on (records 0–36). Because the stove stores the *k*-th
value of a `GET_CONTROLS=1` frame in its record *k*, the command frame must follow this
order. The stove then applies **all** its records 1–36, whatever the number of values
received (read in the 2.27 firmware): every value sent must be the stove's current one
unless it is the commanded one. Open Firenet sends records 0–30 in this order once the
stove has posted them; records 31–36 keep the values the stove reloads itself at each
`GET_REVISION`.

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

- The **names are your choice** — only the **position** matters. The *k*-th name is
  stored as the label of slot *k* (a 32-byte field, copied without a length check:
  keep names under 32 characters) and echoed back with its value.
- If you register **N** names you get slots **0 … N-1** and nothing else. Register only
  a handful and the high-index counters are never emitted — they are not “missing”, the
  stove was never asked for them.
- To read the cumulative counters (pellet hours, total consumption, service countdown),
  you **must register names up to at least index 52** (Open Firenet registers all 88). There is no way to address slot
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

### Sensor table

The official key does not hold the sensor names: it fetches them from the RIKA server
and registers them with the stove, so the order of the RIKA cloud `sensors` object is
the slot order. A full cloud dump in its natural order lines up position by position with
every slot previously identified here (live tests) and in the stove firmware. Open Firenet
uses shorter names on the wire (some official names are longer than the stove's 32-byte
name field, and 88 long names would not fit the stove's 2048-byte receive buffer); the
official name is given for reference.

Indices are those of 2.28 / 2.29 stoves. **2.26 / 2.27** have no slot 2: their slot *p*
is slot *p* below for *p* < 2 and *p* + 1 from 2 on (87 slots).

| Index | Name (Open Firenet) | Official name | Notes |
|---|---|---|---|
| 0 | roomTemp | `inputRoomTemperature` | Room temperature ×10 (246 = 24.6 °C); 1024 = no room sensor connected |
| 1 | flame | `inputFlameTemperature` | Flame / flue temperature (°C) — observed 18→580 across a full burn cycle |
| 2 | bakeTemp | `inputBakeTemperature` | Oven temperature (DOMO BACK); record absent on 2.26 / 2.27 |
| 3 | errMask32 | `statusError` | Active error code / bitmask |
| 4 | errSub | `statusSubError` | Error sub-code |
| 5 | statusWarning | `statusWarning` | Active warnings bitmask — value `2` while the pellet hopper lid is open (observed live) |
| 6 | statusService | `statusService` | Service state |
| 7 | augerSet | `outputDischargeMotor` | Pellet auger (discharge motor), ~360-620 during regulation, `0` in Burn Off |
| 8 | augerCurrent | `outputDischargeCurrent` | Auger motor current — swings ~20-112 during a burn, `0` in standby |
| 9 | idFanMeas | `outputIDFan` | Induced-draft fan (RPM) — ~1500 at regulation, ~2585 purge peak |
| 10 | idFanSet | `outputIDFanTarget` | Induced-draft fan setpoint (RPM) |
| 11 | insertionMotor | `outputInsertionMotor` | Insertion motor |
| 12 | insertionCurrent | `outputInsertionCurrent` | Insertion motor current |
| 13 | airFlaps | `outputAirFlaps` | Air flaps position, per mille as on the stove screen (50 = 5.0 %, 810 = 81.0 %) |
| 14 | airFlapsTarget | `outputAirFlapsTargetPosition` | Air flaps target position, per mille |
| 15 | burnBackMagnet | `outputBurnBackFlapMagnet` | Burn-back flap magnet |
| 16 | gridMotor | `outputGridMotor` | Grate motor |
| 17 | ignition | `outputIgnition` | Igniter |
| 18 | tempLimiter | `inputUpperTemperatureLimiter` | Safety temperature limiter (1 = OK) |
| 19 | pressureSwitch | `inputPressureSwitch` | Pressure switch |
| 20 | pressureSensor | `inputPressureSensor` | Pressure sensor |
| 21 | gridContact | `inputGridContact` | Grate contact |
| 22 | door | `inputDoor` | Door contact |
| 23 | hopperLidClosed | `inputCover` | Pellet hopper lid (`1` = closed, `0` = open) — confirmed live |
| 24 | externalRequest | `inputExternalRequest` | External request contact |
| 25 | burnBackSwitch | `inputBurnBackFlapSwitch` | Burn-back flap switch |
| 26 | flueGasSwitch | `inputFlueGasFlapSwitch` | Flue gas flap switch |
| 27 | boardSensor | `inputBoardTemperature` | Board temperature (°C) |
| 28 | stageCur1 | `inputCurrentStage` | Current heating stage (%) |
| 29 | stageTgt2 | `inputTargetStagePID` | Target stage of the regulation (%) |
| 30 | stageCur | `inputCurrentStagePID` | Current stage of the regulation (%) |
| 31 | mainState | `statusMainState` | `0` Off, `1` Standby, `2` Ignition, `3` Flame Start, `4` Heating, `5` Grate Cleaning, `6` Burn Off, `7` Split Log |
| 32 | subState | `statusSubState` | Sub-state |
| 33 | rssi | `statusWifiStrength` | Wi-Fi signal the dongle reports in `GET_REVISION` |
| 34 | ecoModePossible | `parameterEcoModePossible` | Eco mode available (1) or not (0) |
| 35 | fabNumber | `parameterFabricationNumber` | Fabrication number |
| 36 | model | `parameterStoveTypeNumber` | Stove model ID (see below) |
| 37 | language | `parameterLanguageNumber` | Panel language |
| 38 | appVerBoard | `parameterVersionMainBoard` | Mainboard firmware version (229 = 2.29) |
| 39 | tftVersion | `parameterVersionTFT` | Display (TFT) firmware version |
| 40 | appVersion | `parameterVersionWiFi` | Dongle APP version, as sent in the version frame |
| 41 | blVerBoard | `parameterVersionMainBoardBootLoader` | Mainboard bootloader version |
| 42 | blVerTft | `parameterVersionTFTBootLoader` | Display bootloader version |
| 43 | blVersion | `parameterVersionWiFiBootLoader` | Dongle BL version, as sent in the version frame |
| 44 | firmwareBuild | `parameterVersionMainBoardSub` | Mainboard build (58512 = 585.12) |
| 45 | tftBuild | `parameterVersionTFTSub` | Display build |
| 46 | appRevision | `parameterVersionWiFiSub` | Dongle REV, as sent in the version frame |
| 47 | pelletHours | `parameterRuntimePellets` | Pellet operating time, in hours |
| 48 | logRuntime | `parameterRuntimeLogs` | Wood (log) operating time, in **minutes** (91007 shown as 1516 h by a combined stove); 0 on a stove that burns pellets only |
| 49 | pelletsTotal | `parameterFeedRateTotal` | Total pellet consumption (kg) |
| 50 | serviceCountdown | `parameterFeedRateService` | Consumption remaining before service (kg) |
| 51 | serviceOffset | `parameterServiceCountdownKg` | Service countdown (kg) |
| 52 | serviceMinutes | `parameterServiceCountdownTime` | Service countdown (time) |
| 53 | ignitionCount | `parameterIgnitionCount` | Ignition count — confirmed against the stove screen |
| 54 | onOffCycles | `parameterOnOffCycleCount` | On/off cycles — confirmed against the stove screen |
| 55 | flameSensorOffset | `parameterFlameSensorOffset` | Flame sensor offset |
| 56 | pressureOffset | `parameterPressureSensorOffset` | Pressure sensor offset |
| 57–76 | errCount0 … errCount19 | `parameterErrorCount0` … `19` | Error counters |
| 77 | heatTimesNotProg | `statusHeatingTimesNotProgrammed` | 1 when the heating schedule is off (read in the 2.27 firmware) |
| 78 | frostStarted | `statusFrostStarted` | Frost protection running |
| 79 | spiralTuning | `parameterSpiralMotorsTuning` | Auger tuning (can be negative) |
| 80 | idFanTuning | `parameterIDFanTuning` | Induced-draft fan tuning |
| 81 | cleanInterval | `parameterCleanIntervalBig` | Big cleaning interval |
| 82 | kgTillCleaning | `parameterKgTillCleaning` | kg until cleaning — independent of `serviceCountdown` (observed live) |
| 83–87 | debug0 … debug4 | `parameterDebug0` … `4` | Debug values, the same variables as controls 33–37 |

The stove's response is capped at **88 slots** (0–87): registering more names is accepted
but slots 88 and above never come back (tested live up to 150 names).

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
| **`29`** | `0x1D` | — | **RIKA PRIMO MULTIAIR** | Pellet stove with MultiAir (reported by a user with a dongle plugged in, issue #34; abbreviation not verified; the ID of a PRIMO without MultiAir is unknown) |

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

> Source of this table not verified. Observed live: `statusWarning` = `2` (bit 1) while
> the pellet hopper lid is open; `32` on an INDUO without room sensor.

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

The stove reboots the dongle after **360 seconds** without a `POST_CDCDEVICE_STATUS`
(2.29). Keep the keepalive well under that.

Open Firenet also restarts itself after 60 s without any data from the stove; on
2.26 / 2.27 the `POST_FIRENET_STATUS` ping (every ~20 s) keeps an idle stove answering.

---

## Notes and caveats

- The stove never asserts DTR — write directly to the USB CDC FIFO.
- Send large frames in ≤ 32-byte flushed chunks (short packets) or the stove's USB host
  aborts the pipe.
- Register **all** sensor names (0…87) to get the high-index counters; the stove only
  emits the slots you name, starting at 0.
- 2.29 may send values positionally (`=value;` without names): parse them by position.
  2.26 / 2.27 echo the registered names: parse them by name.
- `roomTarget` / room temperature are **×10** on the wire in both directions.
- `GET_REVISION` must precede `TRANSFER_COMPLETED`, or nothing is returned.
- 2.26 / 2.27 use `GET_FIRENET_STATUS` / `POST_FIRENET_STATUS` (19 fields,
  plain-text SSID, 8-digit id) instead of the CDC device-status commands. 2.28 uses the
  CDC commands, with an 8-digit id.

# Changelog

## v4.1.0 (2026-10-10)

### Breaking changes
- The stove is reported as "Off" (state `off`) when it is switched off, and as "Standby" only when it is switched on and waits for a heat demand. Both used to be reported as "Standby": **an automation or a script that tests the state `standby` to know that the stove is off must now test `off`.** (#89)

### Fixes
- Combined wood and pellet stoves: the states of wood operation are now named instead of "Unknown": "Split Log Check" (state `splitlog_check`), "Split Log" (`splitlog`), "Put On Split Logs" (`splitlog_refuel`) and "Do Not Put Logs" (`splitlog_no_refuel`), which an automation can use, and the stove is reported as burning in split log mode. The last two follow the combustion chamber temperature as the stove's own screen does (300 to 350 °C, and below 300 °C). Read on an INDUO on firmware 2.30. (#89)
- Web page: the MultiAir settings are now shown for a CONNECT too, a model RIKA sells with one or two MultiAir outlets.

## v4.0.0 (2026-10-08)

### Features
- Wi-Fi power saving is now a setting of the Bridge tab. It stays on by default, as it has always been: the bridge runs cooler and draws less power, and an answer can wait up to a quarter of a second. Switch it off if your bridge often loses the Wi-Fi or if you want it to answer faster; the change applies at once. (#82)
- Optional password for wireless updates. Set it, or remove it, with the installer while the bridge is plugged into your computer; the installer then asks for it at each wireless update. Without it, nothing changes. A full flash over USB removes it. (#77)

### Changes
- The web API now answers only the bridge's own page and programs on your network (Home Assistant, the installer, scripts). A page of another website opened in a browser on your network can no longer read the bridge's state or send it commands. Nothing changes for normal use. **If you open the bridge, or have set up Home Assistant, with a name given by your router (for example `open-firenet.lan` or `open-firenet.fritz.box`) or a domain of your own:** the bridge now only answers at its IP address and at `open-firenet.local`. The page tells you so and gives the address to use; add your name once in the Bridge tab, new section "Access to the page", and it works again. (#77)
- `/api/restart` only accepts POST (the web page already used it). (#77)
- Web page: the names of the Wi-Fi networks found around are shown as plain text, whatever characters they contain. (#77)

### Fixes
- Wi-Fi: the bridge now rejoins your network by itself after a drop (router restart, access point update, weak signal). It used to stay off the network until it was restarted. (#82)
- Wi-Fi: after a power cut, when the bridge starts before your router, it keeps trying to join your network while its setup access point is up, and leaves setup mode as soon as the network answers. It used to stay in setup mode until it was restarted. (#82)
- Firmware 2.28: changes of the heating schedule, frost protection, eco mode and room sensor offset are now sent to the stove. The page showed them as applied, but they never reached it. (#4)
- Web page: the MultiAir settings are now shown for a SONO. On any other model that is not known as equipped, a link "My stove has MultiAir: show its settings" brings them up. (#4)
- Wi-Fi: when a password is set, the bridge only joins an encrypted network. It no longer accepts an open access point that shows the same name as yours. A network without password still works as before; a network still protected by the old WEP is no longer joined.
- A command received in the first seconds after the bridge starts, before the stove has sent its settings, could write wrong settings to the stove (for example switch it on). The bridge now refuses commands until it knows the stove's settings, and answers "stove not ready"; Home Assistant and scripts simply retry. (#77)
- Commands are only accepted on POST and PUT. (#77)
- A Wi-Fi network whose name contains a quote or a backslash, yours or a neighbour's, no longer breaks the page or the list of networks. (#77)

## v3.7.0 (2026-10-08)

### Features
- Diagnostics tab: a "Bridge health" card shows how the bridge itself is doing: cause of the last restart, free memory and its lowest value, chip temperature, Wi-Fi signal and disconnections, time since the last message from the stove, and how many times the link with the stove was established and lost. The same values are in `/api/state` (`health`) and over MQTT (`<base>/health/...`), so Home Assistant can keep their history and raise alerts. (#74)
- Diagnostics tab: a "Diagnostic file" button downloads a single file to attach to a report, with the bridge health, the link, every value and the exchange log. Its name carries the version and the date, and the Wi-Fi name and the MAC address are masked in it. (#74)

## v3.6.1 (2026-10-07)

### Fixes
- Firmware 2.28: the stove no longer shows "FIRENET UPDATE" instead of linking. The bridge now announces its version in a single USB message; sent in two pieces, this stove recorded a wrong version and asked to update the stick. (#4)
- Stove link: when the stove refuses a message after having accepted the bridge, the bridge no longer floods it with about 20 detection messages per second, and no longer sends it the detection messages meant for other firmware versions. (#4)

## v3.6.0 (2026-10-07)

### Features
- Web page: Italian translation (contributed by @lupin28). (#65)
- The bridge reports the size of its update slot (`device.ota_slot_bytes` in `/api/state`), so the installer can tell when a firmware is too large for a wireless update instead of failing silently. (open-firenet-installer#16)

## v3.5.0 (2026-10-06)

### Features
- MQTT: optional encrypted connection (TLS). The broker's certificate is checked against the public authorities, or against your own authority if you paste its certificate, for a home broker with a self-made certificate. (#54)

### Fixes
- MQTT: after a connection, the bridge no longer queues all its messages at once, which used a lot of memory for a few seconds; and changing the MQTT settings no longer freezes the web page for several seconds.
- The log of the exchanges with the stove keeps about the last 32 kB instead of 48 kB, to leave more free memory.

## v3.4.0 (2026-10-06)

### Features
- MQTT: optional Home Assistant discovery. With the option ticked in the Bridge tab, the stove appears by itself in Home Assistant through its MQTT integration, without installing anything: thermostat, heating power, schedule, frost protection and eco mode switches, and the sensors. Off by default; leave it off if you use the Open Firenet integration. (#54)
- The REST API is now described in an OpenAPI file, `openapi.yaml`: every endpoint, field, type, unit and range, usable with documentation tools and client generators. (#61)

### Fixes
- Web page: the Save button of the MQTT settings now tells what happened ("Saved" or "Not saved"); it used to give no sign at all.
- Web page: the emblem next to the title and the browser tab icon are now the actual Open Firenet logo, instead of a simplified drawing, and "Firenet" in the title carries the orange gradient of the logo.

## v3.3.1 (2026-10-05)

### Fixes
- The name is written "Open Firenet" everywhere, as on the logo, without the hyphen: web page, documentation, and the name reported by `/api/state` and `/api/version`. The Wi-Fi network created for the first setup keeps its name, `Open-Firenet-Setup`.
- Web page: the hint under the Wi-Fi form ("If the button does nothing, open http://192.168.4.1...") is now shown in the language of the page instead of in French and English at once.
- Web page: on a first visit the page uses the language of your browser (French, German, or English for any other) instead of always starting in French.

## v3.3.0 (2026-10-04)

### Features
- Web page reorganised into three tabs at the top: **Stove** (the dashboard and controls, unchanged), **Bridge** (Wi-Fi, MQTT, restart) and **Diagnostics** (link with the stove, exchange log, all values, and the frame delay under "Advanced"). Each tab has its own address (`/#bridge`, `/#diagnostics`), the page opens on Bridge during the first Wi-Fi setup, and the "stove does not answer" banner links to Diagnostics. The stove model, its firmware version and the link state are now shown in the dashboard, and the Stove tab carries a green or grey dot. (#55)
- MQTT support, optional and off by default, for home automation systems other than Home Assistant (Jeedom, openHAB, Node-RED, Domoticz...). The bridge publishes the stove state to your broker (one JSON message, and one topic per value) with its availability, and accepts the same commands as the REST API (on/off, mode, power, target temperature, MultiAir, schedule...). Set it up in the Bridge tab of the web page; see the MQTT section of the README for the topics. (#52)

### Fixes
- Target temperature: a value with a half degree (21.5 °C), sent through the REST API or MQTT, is rounded to the whole degree, as the stove sets its target in steps of 1 °C.
- `/api/state`: the stove model, its firmware version and build are `null` until the stove has sent them. They used to default to a DOMO on firmware 2.29, which looked like real data when the bridge was not linked to the stove at all.

## v3.2.0 (2026-10-03)

### Features
- The web page now tells why there is no link with the stove: bridge not connected to the stove over USB (wrong port on the board, charge-only cable, stove off), or connected but the stove stays silent. `/api/state` reports the same under `usb` (`host_connected`, `rx_bytes`).
- Web page header: the Open Firenet version is shown next to the title, and a single badge gives the stove model, its firmware version and the link state (e.g. "DOMO v2.29 connected"). The badge turns grey when the stove is not connected.

### Fixes
- Stoves on firmware 2.28 (e.g. LIVO): the link works again. v3.0.0 and v3.1.0 talked to these stoves in the wrong message format, so the stove never answered and showed UW29.
- Web page header on phones: the title and the badge are no longer split over two lines.
- Web page: while no data has been received from the stove, the page says "Waiting for the stove" and greys out the values and controls, instead of showing a DOMO in standby with zero values.

## v3.1.0 (2026-10-02)

### Features
- Stoves on firmware 2.26 / 2.27 (e.g. INDUO): the heating schedule, frost protection, room sensor calibration and eco mode can now be changed too, not only on/off, mode, power and temperature.

### Fixes
- Web page, "Counters & maintenance" card: the two counters no longer overlap on wide screens, and their labels are clearer ("Running time: … h", then "Service in: … kg" right above its bar).
- Web page: the service bar is now a gauge of what is left before the next service, based on the interval reported by the stove (700 kg when it reports none). It turns orange below 20 %, and hovering it shows the figures.
- Web page, Settings tab: at most two cards per row; the eco mode card now sits on its own row below frost protection and room temperature calibration.
- Stoves on firmware 2.28 (e.g. LIVO): commands are sent in the short form again (on/off, mode, power, target temperature), as on 2.26 / 2.27. Since v3.0.0 they received the long form meant for firmware 2.29.

## v3.0.0 (2026-10-01)

### Features
- Support for stoves running mainboard firmware 2.26 / 2.27 (e.g. INDUO): the link comes up and every stove value is read. On these stoves, on/off, mode, heating power and target temperature can be changed; the other settings (schedule, MultiAir, frost protection, offset) are read-only for now. Stoves on firmware 2.28 are detected but not supported yet.
- The stove type is detected automatically at startup (firmware 2.29, 2.28 or 2.26 / 2.27), nothing to configure.
- Every stove value now has a meaningful name, aligned with the official Rika names: warnings (`statusWarning`), air flaps (`airFlaps`, `airFlapsTarget`), error counters, display versions, and more.
- Eco mode switch in the web page (Stove controls > Settings), enabled only when the stove reports that eco mode is possible.
- Eco mode can be read and changed: `eco_mode` / `eco_mode_possible` in `/api/state`, `ecoMode` in `/api/controls` (firmware 2.29; read-only on 2.26 / 2.27 for now). Settings now use their official Rika names too (`ecoMode` instead of `reserved6`, `debug0`…`debug4`).
- `/api/state` now reports the warning code (`warning_code`), the air flap position and target in % (`air_flaps_percent`, `air_flaps_target_percent`), and whether a room sensor is connected (`room_sensor_connected`).

### Breaking changes
- Removed the legacy endpoints `/api/status`, `/api/sensors` and `/api/arm`, and the duplicate routes `/restart` and `/reset-wifi`: use `/api/state`, `/api/restart` and `/api/forget` instead. The old `docs/api-explorer.html`, which relied on them, is removed too.
- The reply to `POST /api/controls` is now a short acknowledgement (`ok`, `on`, `mode`, `target_temperature`, `power_percent`); read `/api/state` for the full state.

### Fixes
- Smaller firmware (about 130 KB less flash, more room for future updates): the web page is stored compressed, and the command parsing of the API was rewritten around a single table.
- More free memory on the bridge: the debug log (CDC Logs tab, `/log`) keeps 48 KB of history, with repeated lines merged into one.
- Stoves without a RIKA room sensor no longer show 102.4 °C: the room temperature is reported as unavailable (`null` in the API, `--` on the web page).
- The link no longer restarts every minute while the stove is idle: the stove only sends changes, so the bridge now checks regularly that it still answers.
- Default delay between two frames sent to the stove lowered from 600 ms to 150 ms, for faster reactions (still adjustable from 50 to 600 ms in the Logs tab; a value you already set is kept).

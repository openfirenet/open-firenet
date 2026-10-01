# Changelog

## Non publié

### Fixes
- Stoves on firmware 2.28 (e.g. INDUO II, SONO): the bridge now announces itself the way these stoves expect, so they can complete the link (to be confirmed on a real stove).

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

// Checks the diagnostic file built by the web page (open-firenet/web/index.html, between the "diagnostic file"
// markers): what it contains, and that the Wi-Fi name and the MAC address are masked everywhere. Run by
// test/build_and_test.sh.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const html = fs.readFileSync(path.join(root, 'open-firenet', 'web', 'index.html'), 'utf8');
const start = html.indexOf('// --- diagnostic file (begin)');
const end = html.indexOf('// --- diagnostic file (end)');
if (start < 0 || end < 0) { console.error('diagnostic file functions not found in index.html'); process.exit(1); }
const { buildDiagFile, diagMaskMac } = new Function(html.slice(start, end) + '\nreturn { buildDiagFile, diagMaskMac };')();

let failures = 0, checks = 0;
const check = (name, ok) => { checks++; if (!ok) { failures++; console.error('FAIL ' + name); } };

const ssid = 'FRITZ!Box 6660 (LE)';
const hex = Buffer.from(ssid, 'utf8').toString('hex').toUpperCase();
const mac = 'E0:72:A1:C2:86:1C';
const state = {
  device: { version: '3.6.1', wifi_ssid: ssid, mac, ip: '192.168.178.28', uptime_seconds: 120, free_heap: 130000, wifi_rssi: -55 },
  health: { restart_reason: 'power_on', stove_detections: 1, stove_link_losses: 0 },
  stove: { model_name: 'SONO', mainboard_version: '2.28', firmware_build: '53001' },
  status: { ssid, mac, wpa2: '', ip: '192.168.178.28' },
  version_ack: true, version_frame: 'V28', generation: 1, usb: { host_connected: true, rx_bytes: 4096 },
  frames_in: 12, frames_out: 30, revision: 3, raw_sensors: { roomTemp: 206 },
};
const log = '[19928][tx] GET_CDCDEVICE_STATUS=0;\n0\n1\n' + hex + '\n********\n192.168.178.28\n' + mac + '\n0\n'
          + '[20000][tx] GET_FIRENET_STATUS=0;\n' + ssid + '\n' + mac.toLowerCase() + '\n[20100][rx] POST_SENSORS=0; roomTemp=206; \n';
const out = buildDiagFile({ when: '2026-10-08T14:32:10+02:00', lang: 'de', browser: 'test', state,
                            mqtt: { enabled: true, host: '192.168.1.43', user: 'openfirenet', password_set: true }, schedule: { ok: true },
                            txgap: { ms: 200 }, log });

check('header and sections', ['Open Firenet diagnostic file', 'Bridge firmware: 3.6.1', '===== BRIDGE HEALTH =====', '===== LINK WITH THE STOVE =====',
  '===== MQTT (/api/mqtt) =====', '===== SCHEDULE (/api/schedule) =====', '===== STATE (/api/state) =====', '===== LOG (/log) ====='].every(x => out.includes(x)));
check('health and link values', out.includes('restart_reason: power_on') && out.includes('version_frame: V28') && out.includes('stove firmware: 2.28'));
check('the delay between frames is in the file, and a dash when it is not known', out.includes('frame_gap_ms: 200')
  && buildDiagFile({ when: 'x', lang: 'en', browser: 't', state, mqtt: null, schedule: null, log: null }).includes('frame_gap_ms: -'));
check('the raw values and the log are in the file', out.includes('"roomTemp": 206') && out.includes('POST_SENSORS=0; roomTemp=206;'));
check('Wi-Fi name masked everywhere, as text and as hexadecimal', !out.includes(ssid) && !out.includes(hex) && out.includes('<wifi name>'));
check('MAC address masked everywhere, the vendor part kept', !out.includes(mac) && !out.toLowerCase().includes(mac.toLowerCase()) && out.includes('E0:72:A1:xx:xx:xx'));
check('MQTT user masked, the rest kept', !out.includes('"user": "openfirenet"') && out.includes('"host": "192.168.1.43"'));
check('the state given by the caller is not modified', state.device.wifi_ssid === ssid && state.status.mac === mac);
check('a missing part is said so', buildDiagFile({ when: 'x', lang: 'en', browser: 't', state: null, mqtt: null, schedule: null, log: null }).split('(not available)').length === 5);
check('a value that is not a MAC address is left alone', diagMaskMac('') === '' && diagMaskMac('n/a') === 'n/a');
const stateWithCrash = JSON.parse(JSON.stringify(state));
stateWithCrash.health.crash = { task: 'loopTask', pc: '0x4037d155', cause: 0, fault_address: '0x00000000', elf_sha256: '3379f5276' };
const outCrash = buildDiagFile({ when: 'x', lang: 'en', browser: 't', state: stateWithCrash, mqtt: null, schedule: null, log: null });
check('crash line is formatted on one line and not [object Object]', outCrash.includes('crash: task loopTask, pc 0x4037d155, cause 0, fault 0x00000000, firmware 3379f5276') && !outCrash.includes('[object Object]'));

console.log(`diagnostic file: ${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);

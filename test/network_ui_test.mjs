import fs from 'node:fs';
import assert from 'node:assert/strict';

const html = fs.readFileSync(new URL('../open-firenet/web/index.html', import.meta.url), 'utf8');
const start = html.indexOf('// --- Ethernet settings (begin)');
const end = html.indexOf('// --- Ethernet settings (end)');
assert(start >= 0 && end > start);
let confirmed = false, restarts = 0, errors = 0;
const calls = [];
const select = { value: 'none' };
let fail = true;
const { loadEthernet, saveEthernet } = new Function(
  'fetch', 'document', 'I18N', 'curLang', 'askConfirm', 'restartDongle', 'toast',
  html.slice(start, end) + '\nreturn { loadEthernet, saveEthernet };'
)(async (url, options) => {
  calls.push({ url, options });
  return { ok: !fail, json: async () => ({ board: 'waveshare_s3_eth' }) };
}, { getElementById: () => select }, { en: {} }, 'en', async () => confirmed,
  async (value) => { assert.equal(value, true); restarts++; }, () => errors++);

await loadEthernet(); // A failed request is retried.
assert.equal(select.value, 'none');
fail = false;
await loadEthernet();
assert.equal(select.value, 'waveshare_s3_eth');
select.value = 'none';
await loadEthernet(); // Polls must preserve a user's unsaved selection.
assert.equal(calls.length, 2);
assert.equal(select.value, 'none');
await saveEthernet(); // Cancel: no flash write and no restart.
assert.equal(calls.length, 2);
confirmed = true;
fail = true;
await saveEthernet();
assert.equal(restarts, 0);
assert.equal(errors, 1);
fail = false;
await saveEthernet();
assert.equal(restarts, 1);
assert.equal(calls.at(-1).url, '/api/ethernet');
assert.equal(calls.at(-1).options.method, 'POST');
assert.equal(calls.at(-1).options.body.toString(), 'board=none');
console.log('Ethernet UI tests passed');

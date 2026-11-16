import fs from 'node:fs';
import assert from 'node:assert/strict';

const html = fs.readFileSync(new URL('../open-firenet/web/index.html', import.meta.url), 'utf8');
const start = html.indexOf('// --- Ethernet settings (begin)');
const end = html.indexOf('// --- Ethernet settings (end)');
assert(start >= 0 && end > start);
const i18nStart = html.indexOf('const I18N = {');
const i18nEnd = html.indexOf('\n};', i18nStart);
const I18N = new Function(html.slice(i18nStart, i18nEnd + 3) + '; return I18N;')();
let restarts = 0, errors = 0, fail = true, throws = false, mismatched = false;
let answers = [], questions = [];
const calls = [];
const nodes = Object.fromEntries(['ethernetBoard', 'lblConnectionUnsaved', 'btnConnectionSave',
  'btnConnectionCancel', 'wifiSettings', 'wifiPsCard', 'wifiSignal'].map(id => [id, { value: 'none', style: {} }]));
const select = nodes.ethernetBoard;
let stored = 'none';
const ui = new Function(
  'fetch', 'document', 'I18N', 'curLang', 'askConfirm', 'restartDongle', 'toast',
  html.slice(start, end) + '\nreturn { loadEthernet, saveEthernet, cancelEthernet, updateEthernetDirty, renderConnection };'
)(async (url, options) => {
  calls.push({ url, options });
  if (throws) throw Error('offline');
  if (!fail && options && !mismatched) stored = options.body.get('board');
  return { ok: !fail, json: async () => ({ board: stored }) };
}, { getElementById: id => nodes[id] }, I18N, 'en', async (text, opts) => {
  questions.push(text);
  assert.deepEqual(opts, { wide: true, danger: true });
  return answers.shift();
}, async value => { assert.equal(value, true); restarts++; }, () => errors++);

ui.updateEthernetDirty();
assert(select.disabled);
assert(nodes.btnConnectionSave.disabled && nodes.btnConnectionCancel.disabled);
await ui.saveEthernet();
assert.equal(calls.length, 0);
await ui.loadEthernet(); // Failed request retries on the next poll.
fail = false;
await ui.loadEthernet();
assert(!select.disabled);
assert(nodes.btnConnectionSave.disabled && nodes.lblConnectionUnsaved.hidden);
await ui.saveEthernet(); // No changes: no request or restart.
assert.equal(calls.length, 2);
select.value = 'waveshare_s3_eth';
ui.updateEthernetDirty();
assert(!nodes.btnConnectionSave.disabled && !nodes.btnConnectionCancel.disabled);
assert(!nodes.lblConnectionUnsaved.hidden);
await ui.loadEthernet(); // Polls preserve unsaved choices.
assert.equal(calls.length, 2);
ui.cancelEthernet();
assert.equal(select.value, 'none');
assert(nodes.btnConnectionSave.disabled && nodes.lblConnectionUnsaved.hidden);
select.value = 'waveshare_s3_eth';
for (const responses of [[false], [true, false]]) {
  answers = [...responses]; questions = [];
  await ui.saveEthernet();
  assert.equal(questions.length, responses.length);
  assert.equal(calls.length, 2); // Both cancellation paths leave flash untouched.
  assert.equal(restarts, 0);
  assert(!nodes.btnConnectionSave.disabled);
}
answers = [true, true];
fail = true;
await ui.saveEthernet();
assert.equal(errors, 1);
assert.equal(restarts, 0);
assert(!nodes.btnConnectionSave.disabled); // Preserve the draft on failure.
fail = false;
throws = true;
answers = [true, true];
await ui.saveEthernet();
assert.equal(errors, 2);
throws = false;
mismatched = true;
answers = [true, true];
await ui.saveEthernet();
assert.equal(errors, 3);
assert.equal(restarts, 0);
mismatched = false;
answers = [true, true]; questions = [];
await ui.saveEthernet();
assert.deepEqual(questions, [I18N.en.connectionConfirm1, I18N.en.connectionConfirm2]);
assert.equal(restarts, 1);
assert(nodes.btnConnectionSave.disabled && nodes.lblConnectionUnsaved.hidden);
assert.equal(calls.at(-1).options.body.toString(), 'board=waveshare_s3_eth');

// Returning to Wi-Fi needs no hardware warning.
select.value = 'none'; questions = [];
await ui.saveEthernet();
assert.equal(questions.length, 0);
assert.equal(restarts, 2);
assert.equal(calls.at(-1).options.body.toString(), 'board=none');
for (const ethernet of [true, false]) {
  ui.renderConnection(ethernet);
  for (const id of ['wifiSettings', 'wifiPsCard', 'wifiSignal']) {
    assert.equal(nodes[id].hidden, ethernet);
    assert.equal(nodes[id].style.display, ethernet ? 'none' : '');
  }
}
assert(html.includes('renderConnection(s.ethernet_enabled === true)'));
for (const lang of Object.values(I18N)) {
  for (const key of ['connectionLabel', 'connectionWifi', 'connectionEthernet', 'connectionSaveRestart',
    'connectionConfirm1', 'connectionConfirm2']) assert(lang[key]);
}
console.log('Ethernet UI tests passed');

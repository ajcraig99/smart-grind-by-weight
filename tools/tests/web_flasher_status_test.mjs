// Run the web Bluetooth flasher's OTA status handling with DOM doubles.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../web-flasher/flasher.js', import.meta.url), 'utf8');
const shown = [];
const element = {className: '', style: {}, set textContent(text) { shown.push(text); }};
const context = vm.createContext({
  window: {addEventListener() {}},
  document: {getElementById: () => element},
  console: {log() {}, error() {}},
  setTimeout, Date, Error, Promise, TextEncoder, TextDecoder, DataView, ArrayBuffer, Uint8Array
});
vm.runInContext(source, context);

const notify = status => vm.runInContext(
  `handleStatusUpdate({target: {value: {buffer: new Uint8Array([${status}]).buffer}}})`, context);
const wait = (status, timeout = 2000) =>
  vm.runInContext(`waitForOtaStatus(${status}, ${timeout})`, context);

// A refusal ends the wait at once and says how to allow the update.
notify(0x06);
await assert.rejects(wait(0x02), error =>
  error.deviceStatus === 0x06 && error.message.includes('Allow Update'));
assert.ok(shown.at(-1).includes('Allow Update'));

// Errors end the wait too, and the END handler can tell them from a reboot.
for (const status of [0x04, 0x05]) {
  notify(status);
  await assert.rejects(wait(0x03), error => error.deviceStatus === status);
}

// The expected status resolves; a silent grinder still times out.
notify(0x02);
assert.equal(await wait(0x02), true);
await assert.rejects(wait(0x03, 150), error =>
  error.deviceStatus === undefined && error.message.includes('Timeout'));

// A new START ignores a failure left over from an earlier attempt.
const start = source.indexOf('await controlChar.writeValue(startData);');
const reset = source.lastIndexOf('currentOtaStatus = BLE_OTA_IDLE;', start);
assert.ok(reset > 0 && source.slice(reset, start).split('\n').length <= 3);

console.log('Web flasher status tests passed.');

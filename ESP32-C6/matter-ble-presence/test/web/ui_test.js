// Headless check of main/web/index.html against a mocked device API.
// Usage: node ui_test.js [path/to/index.html] [screenshot-dir]   (needs the `playwright` package + Chromium)
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const html = fs.readFileSync(process.argv[2] || path.join(__dirname, '../../main/web/index.html'), 'utf8');
const out = process.argv[3];
let failures = 0;
function check(cond, msg) { if (!cond) { failures++; console.log('FAIL', msg); } else console.log('ok  ', msg); }

const slot = (i, extra = {}) => Object.assign({ enabled: false, label: 'Presence ' + (i + 1), mac: '', addr_type: 'any',
  timeout_s: 45, rssi_filter: false, rssi_threshold: -80, rssi_hysteresis: 5 }, extra);
const config = {
  scan: { interval_ms: 160, window_ms: 40 },
  slots: [slot(0, { enabled: true, label: 'Ví của Bố', mac: 'C0:11:22:33:44:01', addr_type: 'public' }), slot(1), slot(2), slot(3), slot(4)],
  limits: { label_max_bytes: 48, timeout_min: 10, timeout_max: 3600, rssi_min: -100, rssi_max: -20, hysteresis_max: 20,
            interval_min: 20, interval_max: 10240, window_min: 10 },
  defaults: { timeout_s: 45, rssi_threshold: -80, rssi_hysteresis: 5, interval_ms: 160, window_ms: 40 },
  allow_duplicates: false, storage: { status: 'ok', detail: '' },
};
const status = {
  mode: 'config', firmware: '1.0.181', idf: 'v6.0.2', uptime_s: 75, idle_left_s: 587, idle_timeout_s: 600,
  heap: { free: 123456, min: 100000, largest: 80000 }, ap: { ssid: 'VN-Presence-3A7C', stations: 1, channel: 1 },
  scan: { state: 'running', paused_by: '', active: true, interval_ms: 100, window_ms: 50, adv_reports: 1234,
          ibeacon_reports: 210, starts: 1, start_failures: 0, unexpected_stops: 0, watchdog_restarts: 0, last_error: 0,
          list: 3, capacity: 32, queue_drops: 0, evictions: 0 },
  storage: { status: 'ok', detail: '', nvs_main: 'ESP_OK' },
};
const evil = '<img src=x onerror="window.__xss=1">';
const beacons = [
  { mac: 'C0:11:22:33:44:01', addr_type: 'public', kind: 'public', stable: true, rssi: -58, uuid: 'E2C56DB5-DFFB-48D2-B060-D0F5A71096E0',
    major: 1, minor: 1, tx_power: -59, name: 'Ví', age_ms: 800, count: 120, same_id_other_mac: false },
  { mac: 'D4:AA:BB:CC:DD:02', addr_type: 'random', kind: 'static', stable: true, rssi: -71, uuid: 'E2C56DB5-DFFB-48D2-B060-D0F5A71096E0',
    major: 1, minor: 2, tx_power: -59, name: evil, age_ms: 2100, count: 40, same_id_other_mac: false },
  { mac: '5A:01:02:03:04:05', addr_type: 'random', kind: 'rpa', stable: false, rssi: -84, uuid: 'FDA50693-A4E2-4FB1-AFCF-C6EB07647825',
    major: 10, minor: 7, tx_power: -60, name: '', age_ms: 9000, count: 5, same_id_other_mac: true },
];

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
  const posts = [];
  let dialogs = 0;
  page.on('dialog', async d => { dialogs++; await d.accept(); });
  page.on('pageerror', e => { failures++; console.log('FAIL page error', e.message); });
  await page.route('http://device.test/**', async route => {
    const req = route.request();
    const url = new URL(req.url());
    const json = (obj, code = 200) => route.fulfill({ status: code, contentType: 'application/json', body: JSON.stringify(obj) });
    if (url.pathname === '/') return route.fulfill({ status: 200, contentType: 'text/html; charset=utf-8', body: html });
    if (url.pathname === '/api/status') return json(status);
    if (url.pathname === '/api/scan' && req.method() === 'GET') return json({ beacons });
    if (url.pathname === '/api/config' && req.method() === 'GET') return json(config);
    if (req.method() === 'POST') {
      posts.push({ path: url.pathname, body: req.postData() });
      if (url.pathname === '/api/config') return json({ ok: true, message: 'Đã lưu và đọc lại kiểm tra thành công.' });
      return json({ ok: true, message: 'ok', idle_left_s: 600 });
    }
    return route.fulfill({ status: 404, body: '' });
  });

  await page.goto('http://device.test/');
  await page.waitForSelector('.beacon');
  await page.waitForTimeout(300);

  // Rendering and escaping
  const names = await page.$$eval('.beacon b', els => els.map(e => e.textContent));
  check(names.includes(evil), 'malicious beacon name shown as plain text');
  check(await page.evaluate(() => window.__xss === undefined), 'no script from a beacon name ran');
  check((await page.$$('.beacon img')).length === 0, 'no <img> element injected');
  check((await page.textContent('#beaconList')).includes('đổi theo thời gian'), 'RPA warning shown');
  check((await page.inputValue('#mac0')) === 'C0:11:22:33:44:01', 'stored MAC loaded into slot 1');
  check((await page.textContent('#seen0')).includes('-58 dBm'), 'slot 1 shows its beacon is heard');
  check((await page.textContent('#hdrSsid')).includes('VN-Presence-3A7C'), 'SSID in header');

  // Assign the second beacon to Presence 2 from the list
  const selects = await page.$$('.beacon select');
  await selects[1].selectOption('1');
  check((await page.inputValue('#mac1')) === 'D4:AA:BB:CC:DD:02', 'assign from list fills the MAC');
  check((await page.inputValue('#type1')) === 'random', 'assign from list fills the address type');
  check(await page.isChecked('#en1'), 'assign from list enables the slot');
  check((await page.textContent('#dirtyMsg')).length > 0, 'unsaved changes flagged');

  // Assigning the beacon of slot 1 to slot 3 is refused
  await selects[0].selectOption('2');
  check((await page.inputValue('#mac2')) === '', 'duplicate assign refused');
  check((await page.textContent('#banner')).includes('Presence 1'), 'duplicate assign explains which slot');

  // Invalid manual MAC blocks saving
  await page.check('#en2');
  await page.fill('#mac2', 'C0:11:22:33:44');
  await page.click('#btnSave');
  check((await page.textContent('#emac2')).length > 0, 'invalid MAC error shown');
  check(posts.filter(p => p.path === '/api/config').length === 0, 'nothing sent while invalid');

  // Manual duplicate (same MAC as slot 1, "any") is an error too
  await page.fill('#mac2', 'c0-11-22-33-44-01');
  check((await page.textContent('#emac2')).includes('Presence 1'), 'manual duplicate MAC flagged');
  // Fix it with a new beacon in another notation; out-of-range timeout flagged
  await page.fill('#mac2', 'aabbccddeeff');
  await page.fill('#to2', '5');
  check((await page.textContent('#eto2')).length > 0, 'timeout below 10 s flagged');
  await page.fill('#to2', '60');
  await page.fill('#label2', 'Phòng ngủ <b>x</b>');
  await page.fill('#scanWin', '120');
  check((await page.textContent('#scanInfo')).includes('75%'), 'scan duty shown with warning above 50%');
  await page.fill('#scanWin', '40');

  await page.click('#btnSave');
  await page.waitForTimeout(300);
  const saves = posts.filter(p => p.path === '/api/config');
  check(saves.length === 1, 'one save request sent');
  if (saves.length) {
    const body = JSON.parse(saves[0].body);
    check(body.slots.length === 5, 'save body has 5 slots');
    check(body.slots[2].mac === 'AA:BB:CC:DD:EE:FF', 'manual MAC normalised to AA:BB:CC:DD:EE:FF');
    check(body.slots[1].mac === 'D4:AA:BB:CC:DD:02' && body.slots[1].addr_type === 'random', 'assigned slot saved with type');
    check(body.slots[2].label === 'Phòng ngủ <b>x</b>', 'label sent as typed (escaped by the device on output)');
    check(body.scan.interval_ms === 160 && body.scan.window_ms === 40, 'scan settings sent');
    check(body.slots[2].timeout_s === 60, 'timeout sent as number');
  }
  check((await page.textContent('#banner')).includes('Đã lưu'), 'save result shown');

  // Keepalive happens on interaction (not on the periodic polls)
  check(posts.some(p => p.path === '/api/keepalive'), 'keepalive sent on user interaction');

  if (out) await page.screenshot({ path: path.join(out, 'config_page_mobile.png'), fullPage: true });

  // Restart
  await page.click('#btnRestart');
  await page.waitForTimeout(200);
  check(posts.some(p => p.path === '/api/restart'), 'restart request sent');
  check(dialogs === 0, 'no confirm dialog after a successful save');

  await browser.close();
  console.log(failures ? `${failures} FAILED` : 'UI checks passed');
  process.exit(failures ? 1 : 0);
})();

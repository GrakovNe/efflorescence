/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
const fs = require("fs");
const vm = require("vm");
const path = require("path");
const WEB = path.join(__dirname, "..", "..", "web");

function mockClassList(el) {
  const has = c => el.className.split(/\s+/).includes(c);
  return {
    add: c => { if (!has(c)) el.className = (el.className + " " + c).trim(); },
    remove: c => { el.className = el.className.split(/\s+/).filter(x => x && x !== c).join(" "); },
    contains: has,
  };
}

function mockEl() {
  const el = {
    textContent: "", className: "", value: "", checked: false, disabled: false,
    innerHTML: "", style: { setProperty(k, v) { this[k] = v; } }, dataset: {}, type: "text", href: "",
    children: [],
    appendChild(c) { this.children.push(c); },
    addEventListener(name, fn) { (this.listeners ||= {})[name] = fn; },
    setAttribute(k, v) { (this.attrs ||= {})[k] = v; },
    getBoundingClientRect() { return { width: 600, height: 190, left: 0, top: 0 }; },
    querySelectorAll() { return []; },
    querySelector() { return null; },
  };
  const cl = mockClassList(el);
  cl.toggle = (c, on) => { const want = on === undefined ? !cl.contains(c) : on; if (want) cl.add(c); else cl.remove(c); };
  el.classList = cl;
  return el;
}

function runPage(file, apiData) {
  const html = fs.readFileSync(file, "utf8");
  const js = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const els = {};
  const fetchCalls = [];
  const doc = {
    getElementById: (id) => (els[id] ||= mockEl()),
    createElement: () => mockEl(),
    querySelectorAll: () => [],
    body: mockEl(),
    title: "",
  };
  const ctx = {
    document: doc,
    window: { addEventListener() {} },
    location: { origin: "http://efflorescence-c6e0.local", href: "", reload() {} },
    fetch: (url, opts) => {
      fetchCalls.push(url + (opts && opts.method ? " " + opts.method : "") + (opts && opts.body ? " " + opts.body : ""));
      const key = url.split("?")[0];
      if (apiData[key] === undefined) {
        return Promise.reject(new Error("network down"));
      }
      return Promise.resolve({ ok: true, json: async () => apiData[key] });
    },
    setInterval: () => 0,
    clearInterval: () => {},
    setTimeout: () => 0,
    clearTimeout: () => {},
    Intl: { DateTimeFormat: () => ({ resolvedOptions: () => ({ timeZone: "Europe/Moscow" }) }) },
    console, Date, Math, JSON, String, Number, Array, Promise, Set,
  };
  vm.createContext(ctx);
  vm.runInContext(js, ctx, { filename: file });
  return new Promise((resolve) => setImmediate(() => setImmediate(() => setImmediate(() =>
    resolve({ els, fetchCalls, ctx, doc })))));
}

let fails = 0;
function check(cond, name) {
  if (cond) console.log("PASS " + name);
  else { fails++; console.log("FAIL " + name); }
}

(async () => {
  const now = Math.floor(Date.now() / 1000);
  const light = { state: "ON", brightness: 60, soft: true, battery_low: false };
  const status = {
    time: { timestamp: now, synced: true, tz: "MSK-3", local: "2026-11-05T23:41:07", weekday: 4, uptime_s: 5231 },
    device: {
      name: "Bedside", id: "c6e0", hostname: "bedside", chip: "ESP32-C3", revision: "v0.4", cores: 1, cpu_mhz: 160,
      firmware: "abc1234", slot: "ota_1", pending_verify: false, idf: "v5.5.5", build_date: "Nov  5 2026", mac: "44:b1:76:5d:c6:e0",
      flash_bytes: 4194304, heap_free: 180000, heap_min_free: 150000, reset_reason: "power_on",
    },
    mode: "normal",
    light: { ...light, pwm: { freq_hz: 1250, bits: 14, duty: 4915, duty_min: 163, duty_max: 16383, raw: false } },
    wifi: { connected: true, ap_up: false, ssid: "Home", bssid: "aa:bb:cc:dd:ee:ff", rssi: -58, channel: 6,
            ip: "192.168.1.42", gateway: "192.168.1.1", last_disconnect: "other" },
    mqtt: { enabled: true, connected: true, last_publish: now - 10, errors: 0 },
  };

  const m = await runPage(path.join(WEB, "main.html"), { "/api/v1/light": light, "/api/v1/status": status });
  check(m.els.bri.textContent === "60", "main: brightness shown");
  check(m.els.slider.value === 60, "main: slider follows the device");
  check(m.els.slider.style["--p"].startsWith("59.59"), "main: slider fill position");
  check(m.els.light.className === "light", "main: on is not dimmed");
  check(m.els.state.innerHTML.includes("dot good") && m.els.state.innerHTML.includes(">On<"), "main: on state line");
  check(m.els.toggle.textContent === "Turn off", "main: button offers off");
  check(m.els.toggle.className === "btn", "main: off button is plain");
  check(m.els.toggle.disabled === false, "main: button enabled once loaded");
  check(m.els.upd.textContent.startsWith("Updated "), "main: updated line");
  check(m.els.fw.textContent.includes("abc1234"), "main: firmware footer");
  check(m.doc.title === "Bedside", "main: title is the device name");
  check(m.els.devname.textContent === "Bedside", "main: header is the device name");
  check(!m.els.state.innerHTML.includes("battery"), "main: no battery note when it is fine");
  const ml = await runPage(path.join(WEB, "main.html"), { "/api/v1/light": { ...light, battery_low: true }, "/api/v1/status": status });
  check(ml.els.state.innerHTML.includes('class="low">battery low'), "main: low battery called out");

  const mo = await runPage(path.join(WEB, "main.html"), {
    "/api/v1/light": { state: "OFF", brightness: 35, soft: true }, "/api/v1/status": status,
  });
  check(mo.els.light.className === "light off", "main: off is dimmed");
  check(mo.els.state.innerHTML.includes(">Off<") && !mo.els.state.innerHTML.includes("good"), "main: off state line");
  check(mo.els.toggle.textContent === "Turn on", "main: button offers on");
  check(mo.els.toggle.className === "btn primary", "main: on button is the accent one");
  check(mo.els.bri.textContent === "35", "main: brightness kept while off");

  let posted = [];
  mo.ctx.fetch = (url, opts) => {
    if (opts && opts.method === "POST") posted.push(JSON.parse(opts.body));
    return Promise.resolve({ ok: true, json: async () => ({ state: "ON", brightness: 35, soft: true }) });
  };
  await mo.els.toggle.onclick();
  await new Promise(r => setImmediate(r));
  check(posted.length === 1 && posted[0].state === "TOGGLE", "main: button posts a toggle");
  check(mo.els.toggle.textContent === "Turn off", "main: state taken from the answer");

  posted = [];
  mo.els.slider.value = "72";
  mo.els.slider.listeners.input();
  await new Promise(r => setImmediate(r));
  check(mo.els.bri.textContent === "72", "main: number follows the finger");
  check(posted.length === 1 && posted[0].brightness === 72 && posted[0].state === "ON", "main: slider posts brightness and turns on");
  check(posted[0].transition === 0.2, "main: slider steps are eased");
  mo.els.slider.value = "73";
  mo.els.slider.listeners.input();
  check(posted.length === 1, "main: a fast drag is throttled");
  mo.els.slider.listeners.change();
  await new Promise(r => setImmediate(r));
  check(posted.length === 2 && posted[1].brightness === 73, "main: the last position is always sent");

  const m2 = await runPage(path.join(WEB, "main.html"), {});
  check(m2.els.upd.textContent === "Device not reachable", "main: unreachable footer");
  check(m2.els.upd.className === "err", "main: unreachable red");

  const cfg = {
    device: { name: "Bedside" },
    wifi: { ssid: "Home", password_set: true },
    time: { ntp_server: "pool.ntp.org", tz: "MSK-3" },
    light: { soft: true },
    mqtt: {
      enabled: true, host: "mqtt.test", port: 1884, username: "u",
      password_set: true, tls: false, ca_cert_set: true, skip_verify: false,
      topic_prefix: "efflorescence/c6e0", discovery: true,
    },
  };
  const i = await runPage(path.join(WEB, "info.html"), { "/api/v1/status": status, "/api/v1/config": cfg });
  check(i.els["s-light"].textContent === "On", "info: light state");
  check(i.els["s-light"].className === "state ok", "info: light on is green");
  check(i.els["v-bri"].textContent === "60 %", "info: brightness row");
  check(i.els["v-soft"].textContent === "On", "info: soft row");
  check(i.els["v-bat"].textContent === "Not reported low" && i.els["v-bat"].className === "", "info: battery row");
  check(i.els["v-pwm"].textContent === "1250 Hz · 14 bit", "info: pwm row");
  check(i.els["v-duty"].textContent === "30.00 % · 4915 of 16383", "info: duty row");
  const full = { ...status, light: { ...status.light, brightness: 100,
                 pwm: { ...status.light.pwm, duty: 16383, steady: true } } };
  const ifull = await runPage(path.join(WEB, "info.html"), { "/api/v1/status": full, "/api/v1/config": cfg });
  check(ifull.els["v-duty"].textContent === "Steady on, no PWM", "info: full brightness is not PWM");
  check(i.els["s-wifi"].textContent === "Connected", "info: wifi state");
  check(i.els["v-rssi"].textContent === "-58 dBm", "info: rssi row");
  check(i.els["v-host"].textContent === "bedside.local", "info: hostname row");
  check(i.els["v-ntp"].textContent === "pool.ntp.org", "info: ntp from config");
  check(i.els["s-mqtt"].textContent === "Connected" && i.els["s-mqtt"].className === "state ok", "info: mqtt connected");
  check(i.els["v-broker"].textContent === "mqtt.test : 1884", "info: broker from config");
  check(i.els["v-topics"].textContent === "efflorescence/c6e0/…", "info: topic prefix");
  check(i.els["v-pub"].textContent === "10 s ago", "info: last publish");
  check(i.els["v-up"].textContent === "1 h 27 min", "info: uptime");
  check(i.els["v-tz"].textContent === "MSK-3", "info: time zone");
  check(i.els["v-now"].textContent === "2026-11-05 23:41:07", "info: device local time");
  check(i.els["v-flash"].textContent === "4.0 MB", "info: flash size");
  check(i.els["v-reset"].textContent === "Power on", "info: reset reason");
  check(i.els["v-chip"].textContent === "ESP32-C3 rev v0.4 · 1 × 160 MHz", "info: chip row");
  check(i.els["v-fw"].textContent === "abc1234", "info: firmware");
  check(i.els["v-slot"].textContent === "ota_1", "info: slot");

  const st2 = { ...status, mode: "fallback",
                light: { state: "OFF", brightness: 60, soft: false, battery_low: true,
                         pwm: { freq_hz: 400, bits: 14, duty: 200, duty_min: 163, duty_max: 16383, raw: true } },
                wifi: { ...status.wifi, connected: false, rssi: null, channel: null, ip: null, gateway: null },
                mqtt: { enabled: false, connected: false, last_publish: null, errors: 0 },
                time: { ...status.time, synced: false, timestamp: null, local: null, weekday: null } };
  const i2 = await runPage(path.join(WEB, "info.html"), { "/api/v1/status": st2, "/api/v1/config": cfg });
  check(i2.els["s-light"].textContent === "Off", "info: light off");
  check(i2.els["s-light"].className === "state", "info: light off is neutral");
  check(i2.els["v-soft"].textContent === "Off", "info: soft off");
  check(i2.els["v-bat"].textContent === "Low" && i2.els["v-bat"].className === "bad", "info: low battery is red");
  check(i2.els["v-duty"].textContent.endsWith("bench override"), "info: bench override flagged");
  check(i2.els["s-wifi"].textContent === "No connection", "info: lost wifi");
  check(i2.els["s-wifi"].className === "state err", "info: lost wifi is red");
  check(i2.els["v-ip"].textContent === "—", "info: no ip dash");
  check(i2.els["s-mqtt"].textContent === "Off" && i2.els["s-mqtt"].className === "state", "info: mqtt off is neutral");
  check(i2.els["s-time"].textContent === "Not synced", "info: time not synced");
  check(i2.els["v-now"].textContent === "—", "info: no device time");

  const i3 = await runPage(path.join(WEB, "info.html"), { "/api/v1/status": { ...st2, mode: "provisioning" }, "/api/v1/config": cfg });
  check(i3.els["s-wifi"].textContent === "Not configured", "info: unconfigured wifi");
  check(i3.els["s-wifi"].className === "state", "info: unconfigured wifi is neutral");

  const i4 = await runPage(path.join(WEB, "info.html"), {});
  check(i4.els.upd.textContent === "Device not reachable", "info: unreachable footer");

  const s = await runPage(path.join(WEB, "setup.html"), {
    "/api/v1/status": status,
    "/api/v1/config": cfg,
    "/api/v1/wifi/scan": { scanning: false, networks: [{ ssid: "Home", rssi: -50, secure: true },
                                                       { ssid: "Guest", rssi: -70, secure: false }] },
  });
  check(s.els.name.value === "Bedside", "setup: device name");
  check(s.els.hostprev.textContent === "http://bedside.local/", "setup: hostname preview");
  check(s.els.passfor.textContent === "Home", "setup: password for the saved ssid");
  check(s.els.passhint.textContent.includes("leave empty"), "setup: pass hint");
  check(s.els.ntp.value === "pool.ntp.org", "setup: ntp");
  check(s.els.soft.checked === true, "setup: soft switch");
  check(s.els.mq_en.checked === true, "setup: mqtt enabled");
  check(s.els.mq_host.value === "mqtt.test", "setup: mqtt host");
  check(s.els.mq_port.value === 1884, "setup: mqtt port");
  check(s.els.mq_pfx.value === "efflorescence/c6e0", "setup: topic prefix");
  check(s.els.mqpfxhint.textContent.includes("efflorescence/c6e0/set") && s.els.mqpfxhint.textContent.includes("efflorescence/c6e0/state"), "setup: topic preview");
  check(s.els.mq_disc.checked === true, "setup: discovery");
  check(s.els.mq_skip.disabled === true, "setup: skip disabled without TLS");
  check(s.els.cawrap.style.display === "none", "setup: CA hidden without TLS");
  check(s.els.tz.value === "MSK-3", "setup: time zone selected");
  check(s.els.tz.children.length > 20 && s.els.tz.children.every(o => o.value !== "MSK-3" || o.textContent.includes("Moscow")), "setup: zone list filled");
  check(s.els.nets.children.length === 2, "setup: scan list rendered");
  check(s.els.nets.children[0].className === "net sel", "setup: saved network selected");
  check(s.els.h1 === undefined, "setup: title untouched on LAN");
  check(s.els.devname.textContent === "Bedside", "setup: header is the device name");

  s.els.nets.children[1].onclick();
  check(s.els.passwrap.style.display === "none", "setup: open network hides password");
  check(s.els.savebar.className.includes("show"), "setup: save bar appears");
  check(s.els.nchanges.textContent === "1 change", "setup: one change counted");
  await s.els.save.onclick();
  const post = s.fetchCalls.find(c => c.startsWith("/api/v1/config POST"));
  check(!!post, "setup: config POSTed");
  check(s.els.rtitle.textContent === "Restarting", "setup: restart screen after save");

  let saved = null;
  const s3 = await runPage(path.join(WEB, "setup.html"), {
    "/api/v1/status": status, "/api/v1/config": cfg,
    "/api/v1/wifi/scan": { scanning: false, networks: [] },
  });
  s3.ctx.fetch = (url, opts) => {
    if (opts && opts.method === "POST") saved = JSON.parse(opts.body);
    return Promise.resolve({ ok: true, json: async () => ({ ok: true, url: "http://bedside.local/" }) });
  };
  s3.els.soft.checked = false;
  s3.els.name.value = "Night Light";
  await s3.els.save.onclick();
  check(saved && saved.light.soft === false, "setup: posts the soft switch");
  check(saved && saved.device.name === "Night Light", "setup: posts the device name");
  check(saved && saved.wifi.ssid === "Home" && !("password" in saved.wifi), "setup: untouched password is not sent");
  check(saved && saved.time.ntp_server === "pool.ntp.org", "setup: posts ntp");
  check(saved && saved.time.tz === "MSK-3", "setup: posts the time zone");
  check(saved && saved.mqtt.topic_prefix === "efflorescence/c6e0" && saved.mqtt.port === 1884, "setup: posts mqtt");
  check(saved && !("password" in saved.mqtt) && !("ca_cert" in saved.mqtt), "setup: untouched mqtt secrets are not sent");

  const sz = await runPage(path.join(WEB, "setup.html"), {
    "/api/v1/status": status, "/api/v1/config": { ...cfg, time: { ntp_server: "pool.ntp.org", tz: "<+0545>-5:45" } },
    "/api/v1/wifi/scan": { scanning: false, networks: [] },
  });
  check(sz.els.tz.value === "<+0545>-5:45", "setup: an unlisted zone is kept");
  check(sz.els.tz.children[sz.els.tz.children.length - 1].textContent === "<+0545>-5:45", "setup: an unlisted zone gets its own option");
  sz.els.tzdetect.onclick();
  check(sz.els.tz.value === "MSK-3", "setup: zone taken from the browser");
  check(sz.els.savebar.className.includes("show"), "setup: detected zone is an unsaved change");

  const ota = { current: "v1.0.12", slot: "ota_0", pending_verify: false, phase: "idle", progress: null,
                available: null, newer: false, error: null, manifest: "https://example.org/ota.json" };
  const withOta = o => ({ "/api/v1/status": status, "/api/v1/config": cfg, "/api/v1/ota": { ...ota, ...o },
                          "/api/v1/wifi/scan": { scanning: false, networks: [] } });
  const o1 = await runPage(path.join(WEB, "setup.html"), withOta({}));
  check(o1.els.otaline.textContent === "Version v1.0.12", "setup: firmware line before a check");
  check(o1.els.otainstall.style.display === "none", "setup: no install button before a check");
  const o2 = await runPage(path.join(WEB, "setup.html"), withOta({ available: "v1.0.13", newer: true }));
  check(o2.els.otaline.textContent === "Version v1.0.12 · v1.0.13 is available", "setup: update offered");
  check(o2.els.otainstall.style.display === "" && o2.els.otainstall.textContent === "Install v1.0.13", "setup: install button names the version");
  const o3 = await runPage(path.join(WEB, "setup.html"), withOta({ available: "v1.0.12" }));
  check(o3.els.otaline.textContent === "Version v1.0.12 · up to date", "setup: up to date");
  const o4 = await runPage(path.join(WEB, "setup.html"), withOta({ phase: "downloading", progress: 43, available: "v1.0.13", newer: true }));
  check(o4.els.otaline.textContent === "Downloading v1.0.13 · 43 %", "setup: download progress");
  check(o4.els.otacheck.disabled === true && o4.els.otainstall.style.display === "none", "setup: buttons locked while busy");
  const o5 = await runPage(path.join(WEB, "setup.html"), withOta({ error: "manifest: HTTP 404" }));
  check(o5.els.otaline.textContent === "Version v1.0.12 · manifest: HTTP 404", "setup: update error shown");
  check(o5.els.otaline.className === "hint err", "setup: update error is red");
  const o6 = await runPage(path.join(WEB, "setup.html"), withOta({ pending_verify: true }));
  check(o6.els.otaline.textContent === "Version v1.0.12 · on probation", "setup: probation shown");
  const o7 = await runPage(path.join(WEB, "setup.html"), withOta({ phase: "restarting" }));
  check(o7.els.rtitle.textContent === "Updating", "setup: restart screen after an install");
  await o1.els.otacheck.onclick();
  check(o1.fetchCalls.some(c => c.startsWith("/api/v1/ota/check POST")), "setup: check button posts");
  check(!o1.els.savebar || !o1.els.savebar.className.includes("show"), "setup: firmware actions are not unsaved changes");

  const sched = {
    entries: [
      { enabled: true, days: [1, 2, 3, 4, 5], time: "07:00", state: "ON", brightness: 60, transition: 1800 },
      { enabled: false, days: [1, 2, 3, 4, 5, 6, 7], time: "23:30", state: "OFF", brightness: null, transition: 0 },
    ],
    max: 12,
    clock: { set: true, tz: "MSK-3", local: "2026-11-05T23:41:07", weekday: 4 },
    next: { entry: 0, in_minutes: 439 },
  };
  const sc = await runPage(path.join(WEB, "schedule.html"), { "/api/v1/schedule": sched, "/api/v1/status": status });
  check(sc.els.clock.textContent === "Thu 23:41", "schedule: device clock");
  check(sc.els.next.textContent === "Time zone MSK-3 · next: Fri 07:00, on", "schedule: next entry");
  check((sc.els.list.innerHTML.match(/class="row/g) || []).length === 2, "schedule: two rows");
  check(sc.els.list.innerHTML.includes('value="07:00"') && sc.els.list.innerHTML.includes('value="30"'), "schedule: time and fade minutes");
  check(sc.els.list.innerHTML.includes("Reaches its brightness at 07:30"), "schedule: sunrise end shown");
  check(sc.els.list.innerHTML.includes('class="row offed"'), "schedule: disabled entry dimmed");
  check((sc.els.list.innerHTML.match(/class="day on"/g) || []).length === 12, "schedule: day toggles");
  check(sc.els.add.disabled === false, "schedule: can add");
  check(!sc.els.savebar || !sc.els.savebar.className.includes("show"), "schedule: clean after load");
  check(sc.doc.title === "Bedside · Schedule", "schedule: title");

  let sent = null;
  sc.ctx.fetch = (url, opts) => {
    if (opts && opts.method === "POST") sent = JSON.parse(opts.body);
    return Promise.resolve({ ok: true, json: async () => ({ ...sched, entries: sent.entries }) });
  };
  sc.ctx.addEntry();
  check((sc.els.list.innerHTML.match(/class="row/g) || []).length === 3, "schedule: entry added");
  check(sc.els.savebar.className.includes("show"), "schedule: adding is an unsaved change");
  sc.ctx.setField(2, "time", "06:15");
  sc.ctx.setField(2, "fade", "1.5");
  sc.ctx.setField(2, "brightness", "35");
  sc.ctx.toggleDay(2, 7);
  sc.ctx.toggleDay(2, 6);
  sc.ctx.setField(0, "state", "OFF");
  await sc.ctx.save();
  check(sent && sent.entries.length === 3, "schedule: posts every entry");
  check(sent && sent.entries[2].time === "06:15" && sent.entries[2].transition === 90, "schedule: fade minutes become seconds");
  check(sent && sent.entries[2].brightness === 35 && sent.entries[2].days.join() === "1,2,3,4,5", "schedule: brightness and days posted");
  check(sent && sent.entries[0].state === "OFF" && sent.entries[0].brightness === null, "schedule: off carries no brightness");
  check(sent && sent.entries[1].enabled === false && sent.entries[1].transition === 0, "schedule: untouched entry survives");
  check(!sc.els.savebar.className.includes("show"), "schedule: clean after save");
  sc.ctx.removeEntry(0);
  check((sc.els.list.innerHTML.match(/class="row/g) || []).length === 2, "schedule: entry removed");

  const sc2 = await runPage(path.join(WEB, "schedule.html"), {
    "/api/v1/schedule": { entries: [], max: 12, clock: { set: false, tz: "UTC0", local: null, weekday: null }, next: null },
    "/api/v1/status": status,
  });
  check(sc2.els.clock.className === "clock err" && sc2.els.clock.textContent.includes("not set"), "schedule: unset clock is called out");
  check(sc2.els.list.innerHTML.includes("No entries."), "schedule: empty list");
  const sc3 = await runPage(path.join(WEB, "schedule.html"), {});
  check(sc3.els.upd.textContent === "Device not reachable", "schedule: unreachable footer");

  const s4 = await runPage(path.join(WEB, "setup.html"), {
    "/api/v1/status": { ...status, mode: "provisioning" },
    "/api/v1/config": { ...cfg, wifi: { ssid: "", password_set: false }, mqtt: { ...cfg.mqtt, enabled: false } },
    "/api/v1/wifi/scan": { scanning: false, networks: [] },
  });
  check(s4.els.h1.textContent === "Set up Efflorescence", "setup: first-run title");
  check(s4.els.nav.style.display === "none", "setup: first-run hides nav");
  check(s4.els.mq_disc.disabled === true, "setup: MQTT off disables discovery");
  check(s4.els.mqbody.className.includes("dim"), "setup: MQTT off dims fields");

  console.log(fails === 0 ? "ALL OK" : fails + " FAILURES");
  process.exit(fails ? 1 : 0);
})();

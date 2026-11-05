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
  const light = { state: "ON", brightness: 60, soft: true };
  const status = {
    time: { timestamp: now, synced: true, uptime_s: 5231 },
    device: {
      name: "Bedside", id: "c6e0", hostname: "bedside", chip: "ESP32-C3", revision: "v0.4", cores: 1, cpu_mhz: 160,
      firmware: "abc1234", slot: "ota_1", pending_verify: false, idf: "v5.5.5", build_date: "Nov  5 2026", mac: "44:b1:76:5d:c6:e0",
      flash_bytes: 4194304, heap_free: 180000, heap_min_free: 150000, reset_reason: "power_on",
    },
    mode: "normal",
    light: { ...light, pwm: { freq_hz: 1250, bits: 14, duty: 4915, duty_min: 163, duty_max: 16383, raw: false } },
    wifi: { connected: true, ap_up: false, ssid: "Home", bssid: "aa:bb:cc:dd:ee:ff", rssi: -58, channel: 6,
            ip: "192.168.1.42", gateway: "192.168.1.1", last_disconnect: "other" },
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
    time: { ntp_server: "pool.ntp.org" },
    light: { soft: true },
  };
  const i = await runPage(path.join(WEB, "info.html"), { "/api/v1/status": status, "/api/v1/config": cfg });
  check(i.els["s-light"].textContent === "On", "info: light state");
  check(i.els["s-light"].className === "state ok", "info: light on is green");
  check(i.els["v-bri"].textContent === "60 %", "info: brightness row");
  check(i.els["v-soft"].textContent === "On", "info: soft row");
  check(i.els["v-pwm"].textContent === "1250 Hz · 14 bit", "info: pwm row");
  check(i.els["v-duty"].textContent === "30.00 % · 4915 of 16383", "info: duty row");
  check(i.els["s-wifi"].textContent === "Connected", "info: wifi state");
  check(i.els["v-rssi"].textContent === "-58 dBm", "info: rssi row");
  check(i.els["v-host"].textContent === "bedside.local", "info: hostname row");
  check(i.els["v-ntp"].textContent === "pool.ntp.org", "info: ntp from config");
  check(i.els["v-up"].textContent === "1 h 27 min", "info: uptime");
  check(i.els["v-flash"].textContent === "4.0 MB", "info: flash size");
  check(i.els["v-reset"].textContent === "Power on", "info: reset reason");
  check(i.els["v-chip"].textContent === "ESP32-C3 rev v0.4 · 1 × 160 MHz", "info: chip row");
  check(i.els["v-fw"].textContent === "abc1234", "info: firmware");
  check(i.els["v-slot"].textContent === "ota_1", "info: slot");

  const st2 = { ...status, mode: "fallback",
                light: { state: "OFF", brightness: 60, soft: false,
                         pwm: { freq_hz: 400, bits: 14, duty: 200, duty_min: 163, duty_max: 16383, raw: true } },
                wifi: { ...status.wifi, connected: false, rssi: null, channel: null, ip: null, gateway: null },
                time: { ...status.time, synced: false, timestamp: null } };
  const i2 = await runPage(path.join(WEB, "info.html"), { "/api/v1/status": st2, "/api/v1/config": cfg });
  check(i2.els["s-light"].textContent === "Off", "info: light off");
  check(i2.els["s-light"].className === "state", "info: light off is neutral");
  check(i2.els["v-soft"].textContent === "Off", "info: soft off");
  check(i2.els["v-duty"].textContent.endsWith("bench override"), "info: bench override flagged");
  check(i2.els["s-wifi"].textContent === "No connection", "info: lost wifi");
  check(i2.els["s-wifi"].className === "state err", "info: lost wifi is red");
  check(i2.els["v-ip"].textContent === "—", "info: no ip dash");
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

  const s4 = await runPage(path.join(WEB, "setup.html"), {
    "/api/v1/status": { ...status, mode: "provisioning" },
    "/api/v1/config": { ...cfg, wifi: { ssid: "", password_set: false } },
    "/api/v1/wifi/scan": { scanning: false, networks: [] },
  });
  check(s4.els.h1.textContent === "Set up Efflorescence", "setup: first-run title");
  check(s4.els.nav.style.display === "none", "setup: first-run hides nav");

  console.log(fails === 0 ? "ALL OK" : fails + " FAILURES");
  process.exit(fails ? 1 : 0);
})();

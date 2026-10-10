/* Setup portal web page for esp32-weather-epd.
 * Copyright (C) 2026  Haodong Zhao
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __PORTAL_HTML_H__
#define __PORTAL_HTML_H__

#include <Arduino.h>

// Served at "/". The phone has no internet access while connected to the
// setup access point, so everything is inline. Talks to the JSON endpoints in
// provisioning.cpp.
const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>设置天气屏</title>
<style>
:root{--fg:#1d1d1f;--muted:#6e6e73;--bg:#f5f5f7;--card:#fff;--line:#d2d2d7;
--accent:#0a66c2;--ok:#1a7f37;--err:#c62828}
*{box-sizing:border-box}
body{margin:0;font:16px/1.5 -apple-system,"PingFang SC","Microsoft YaHei",
sans-serif;color:var(--fg);background:var(--bg)}
main{max-width:480px;margin:0 auto;padding:20px 16px 40px}
h1{font-size:24px;margin:8px 0 16px}
section{background:var(--card);border-radius:12px;padding:16px;
margin-bottom:16px}
h2{font-size:16px;margin:0 0 12px}
label{display:block;font-size:14px;color:var(--muted);margin:12px 0 4px}
label:first-of-type{margin-top:0}
select,input{width:100%;font:inherit;padding:10px 12px;border:1px solid
var(--line);border-radius:8px;background:#fff;color:var(--fg)}
.row{display:flex;gap:8px}.row>*{flex:1}
button{font:inherit;border:0;border-radius:8px;padding:12px;cursor:pointer}
.secondary{background:var(--bg);color:var(--accent);flex:0 0 auto}
#save{width:100%;background:var(--accent);color:#fff;font-weight:600}
#save:disabled{opacity:.5}
.check{display:flex;align-items:center;gap:8px;color:var(--fg)}
.check input{width:auto}
details summary{cursor:pointer;color:var(--muted);font-size:14px}
#status{margin-top:12px;min-height:24px;text-align:center}
.ok{color:var(--ok)}.err{color:var(--err)}.hidden{display:none}
.hint{font-size:13px;color:var(--muted);margin:4px 0 0}
</style>
</head>
<body>
<main>
<h1>设置天气屏</h1>

<section>
<h2>WiFi</h2>
<label for="net">网络</label>
<div class="row">
<select id="net"><option value="">正在扫描…</option></select>
<button type="button" class="secondary" id="rescan">刷新</button>
</div>
<div id="manualBox" class="hidden">
<label for="ssid">网络名称</label>
<input id="ssid" maxlength="32" autocomplete="off" autocapitalize="off">
</div>
<label for="pass">密码</label>
<input id="pass" type="password" maxlength="63" autocomplete="off">
<label class="check"><input type="checkbox" id="showPass">显示密码</label>
<p class="hint hidden" id="keepPass">当前网络的密码已保存，不修改可留空</p>
<p class="hint">仅支持 2.4GHz 网络</p>
</section>

<section id="citySection">
<h2>城市</h2>
<div id="listBox">
<label for="prov">省份</label>
<select id="prov"></select>
<label for="city">城市</label>
<select id="city"></select>
</div>
<div id="manualCityBox" class="hidden">
<label for="cityName">城市名称</label>
<input id="cityName" maxlength="32">
<div class="row">
<div><label for="lat">纬度</label><input id="lat" inputmode="decimal"></div>
<div><label for="lon">经度</label><input id="lon" inputmode="decimal"></div>
</div>
</div>
</section>

<section>
<details>
<summary>高级设置</summary>
<label for="apikey">OpenWeatherMap API key</label>
<input id="apikey" maxlength="64" autocomplete="off" autocapitalize="off"
placeholder="留空则保持不变">
<p class="hint" id="keyState"></p>
<label class="check"><input type="checkbox" id="resetKey">恢复默认 API key</label>
</details>
</section>

<button id="save" type="button">保存并连接</button>
<div id="status"></div>
</main>

<script>
const $ = id => document.getElementById(id);
const MANUAL = '__manual__';
let state = {}, cities = null;

function setStatus(text, cls) {
  $('status').textContent = text;
  $('status').className = cls || '';
}

async function getJson(url) {
  const r = await fetch(url, {cache: 'no-store'});
  if (!r.ok) throw new Error(r.status);
  return r.json();
}

async function loadNetworks(refresh) {
  $('net').innerHTML = '<option value="">正在扫描…</option>';
  try {
    const nets = await getJson('/api/networks' + (refresh ? '?refresh=1' : ''));
    const sel = $('net');
    sel.innerHTML = '';
    for (const n of nets) {
      const o = document.createElement('option');
      o.value = n.ssid;
      o.textContent = n.ssid + (n.secure ? ' 🔒' : '') + '  ' + bars(n.rssi);
      sel.appendChild(o);
    }
    const m = document.createElement('option');
    m.value = MANUAL;
    m.textContent = '其他网络（手动输入）…';
    sel.appendChild(m);
    if (state.ssid && nets.some(n => n.ssid === state.ssid)) sel.value = state.ssid;
    else if (!nets.length) sel.value = MANUAL;
    onNetChange();
  } catch (e) {
    setStatus('扫描失败，请点"刷新"重试', 'err');
  }
}

function bars(rssi) {
  return rssi > -55 ? '▂▄▆█' : rssi > -67 ? '▂▄▆' : rssi > -78 ? '▂▄' : '▂';
}

function selectedSsid() {
  return $('net').value === MANUAL ? $('ssid').value.trim() : $('net').value;
}

function onNetChange() {
  $('manualBox').classList.toggle('hidden', $('net').value !== MANUAL);
  const same = state.ssid && selectedSsid() === state.ssid;
  $('keepPass').classList.toggle('hidden', !same);
}

function fillCities() {
  const p = Number($('prov').value);
  const sel = $('city');
  sel.innerHTML = '';
  cities.cities.forEach((c, i) => {
    if (c[0] !== p) return;
    const o = document.createElement('option');
    o.value = i;
    o.textContent = c[1];
    sel.appendChild(o);
  });
}

async function loadCities() {
  if (state.cityMode !== 'list') {
    $('listBox').classList.add('hidden');
    $('manualCityBox').classList.remove('hidden');
    $('cityName').value = state.city || '';
    $('lat').value = state.lat || '';
    $('lon').value = state.lon || '';
    return;
  }
  cities = await getJson('/api/cities');
  const prov = $('prov');
  cities.provinces.forEach((name, i) => {
    const o = document.createElement('option');
    o.value = i;
    o.textContent = name;
    prov.appendChild(o);
  });
  const cur = state.cityIndex >= 0 ? cities.cities[state.cityIndex] : null;
  prov.value = cur ? cur[0] : 0;
  fillCities();
  if (cur) $('city').value = state.cityIndex;
}

async function save() {
  const ssid = selectedSsid();
  if (!ssid) return setStatus('请选择或输入 WiFi 网络', 'err');
  const pass = $('pass').value;
  if (pass && pass.length < 8) return setStatus('WiFi 密码至少 8 位', 'err');
  const body = new URLSearchParams({ssid, password: pass});
  if (state.cityMode === 'list') {
    if ($('city').value === '') return setStatus('请选择城市', 'err');
    body.set('city', $('city').value);
  } else {
    body.set('cityName', $('cityName').value.trim());
    body.set('lat', $('lat').value.trim());
    body.set('lon', $('lon').value.trim());
  }
  if ($('resetKey').checked) body.set('resetApikey', '1');
  else if ($('apikey').value.trim()) body.set('apikey', $('apikey').value.trim());

  $('save').disabled = true;
  setStatus('正在连接 ' + ssid + ' …');
  try {
    const r = await fetch('/api/save', {method: 'POST', body});
    const j = await r.json();
    if (!r.ok) {
      $('save').disabled = false;
      return setStatus(ERRORS[j.error] || '保存失败', 'err');
    }
  } catch (e) {
    $('save').disabled = false;
    return setStatus('无法连接设备，请确认手机仍连着设备的 WiFi', 'err');
  }
  pollStatus(Date.now());
}

const ERRORS = {
  busy: '正在连接中，请稍候',
  ssid: 'WiFi 名称无效',
  password: 'WiFi 密码需为 8 到 63 位',
  city: '请选择城市',
  location: '城市名称或经纬度无效',
  apikey: 'API key 格式无效',
};

const REASONS = {
  no_ssid: '找不到该网络，请确认名称正确、路由器已开启，且是 2.4GHz 网络',
  auth: '连接失败，请检查 WiFi 密码',
};

async function pollStatus(start) {
  try {
    const s = await getJson('/api/status');
    if (s.state === 'connected') {
      setStatus('✓ 设置成功！设备正在重启，稍后屏幕会显示天气。可以关闭此页面了。', 'ok');
      return;
    }
    if (s.state === 'failed') {
      $('save').disabled = false;
      setStatus(REASONS[s.reason] || '连接失败', 'err');
      return;
    }
  } catch (e) {
    // The phone may lose the connection briefly while the device joins the
    // router; keep polling.
  }
  if (Date.now() - start > 45000) {
    $('save').disabled = false;
    setStatus('等待超时。如果屏幕已经显示天气，说明设置成功；否则请重新连接设备的 WiFi 再试。', 'err');
    return;
  }
  setTimeout(() => pollStatus(start), 1000);
}

async function init() {
  try {
    state = await getJson('/api/state');
  } catch (e) {
    setStatus('无法读取设备信息，请刷新页面', 'err');
    return;
  }
  $('keyState').textContent = state.customKey
    ? '当前使用自定义 API key' : '当前使用默认 API key';
  $('net').addEventListener('change', onNetChange);
  $('ssid').addEventListener('input', onNetChange);
  $('rescan').addEventListener('click', () => loadNetworks(true));
  $('prov').addEventListener('change', fillCities);
  $('showPass').addEventListener('change',
    e => $('pass').type = e.target.checked ? 'text' : 'password');
  $('save').addEventListener('click', save);
  await Promise.all([loadNetworks(false), loadCities()]);
}

init();
</script>
</body>
</html>
)HTML";

#endif

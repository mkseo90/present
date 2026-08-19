// POV 완드 웹앱 v0.1 — PROTOCOL.md v0.1 구현
"use strict";

const NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // 앱→기기 write
const NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // 기기→앱 notify

// 기기가 color=/leds= 를 알려주지 않을 때 쓰는 기본값 (현재 선물용 완드 기준)
const DEFAULT_MONO_COLOR = "00FF66";
const DEFAULT_LED_COUNT = 7;

const $ = (id) => document.getElementById(id);
const enc = new TextEncoder();
const dec = new TextDecoder();

// ---------- 상태 ----------
const state = {
  device: null,
  rxChar: null,
  sim: false,
  color: "FF4FA0",
  // 현재 하드웨어는 단색 완드다 → 색상 선택 UI를 기본으로 숨긴다.
  // 3색 LED 완드(fw 1.0.0 이상)가 붙으면 INFO를 보고 자동으로 살아난다
  mono: true,
  monoColor: DEFAULT_MONO_COLOR,   // 단색 완드의 실제 LED 색 (INFO의 color= 로 갱신)
  ledCount: DEFAULT_LED_COUNT,     // 완드의 LED 개수 (INFO의 leds= 로 갱신)
  slots: [], // {n, type, preview, selected}
  pending: [], // 응답 대기 콜백
};

// ---------- 통신 로그 ----------
function log(dir, line) {
  const pre = $("log");
  pre.textContent += `${dir} ${line}\n`;
  pre.scrollTop = pre.scrollHeight;
}

// ---------- 시뮬레이터 (가짜 기기) ----------
const sim = {
  slots: JSON.parse(localStorage.getItem("simSlots") || "[]"),
  selected: 1,
  persist() { localStorage.setItem("simSlots", JSON.stringify(this.slots)); },
  handle(line) {
    const [cmd, ...args] = line.split(" ");
    const reply = (s) => setTimeout(() => onLine(s), 80);
    switch (cmd) {
      case "PING": return reply("PONG");
      case "INFO": return reply(`INFO fw=sim slots=12 used=${this.slots.length} bat=100 mode=auto bright=80 owner=주인공 serial=No.001/001 mac=SIM`);
      case "SHOW": return reply("OK SHOW");
      case "SHOWSLOT": {
        const n = parseInt(args[0]);
        if (!this.slots.some((s) => s.n === n)) return reply("ERR 2 empty slot");
        return reply("OK SHOWSLOT");
      }
      case "SAVE": {
        const n = parseInt(args[0]);
        if (!(n >= 1 && n <= 12)) return reply("ERR 2 slot out of range");
        const text = args.slice(2).join(" ");
        this.slots = this.slots.filter((s) => s.n !== n);
        this.slots.push({ n, type: "TXT", preview: text });
        this.slots.sort((a, b) => a.n - b.n);
        this.persist();
        return reply("OK SAVE");
      }
      case "DEL": {
        this.slots = this.slots.filter((s) => s.n !== parseInt(args[0]));
        this.persist();
        return reply("OK DEL");
      }
      case "SEL": { this.selected = parseInt(args[0]); return reply("OK SEL"); }
      case "LIST": {
        this.slots.forEach((s) => reply(`SLOT ${s.n} ${s.type} ${s.preview}`));
        return reply("OK LIST");
      }
      case "SET": return reply("OK SET");
      default: return reply("ERR 1 unknown command");
    }
  },
};

// ---------- 송수신 ----------
async function sendLine(line) {
  log("→", line);
  if (state.sim) return sim.handle(line);
  if (!state.rxChar) { toastConnectNeeded(); throw new Error("not connected"); }
  const bytes = enc.encode(line + "\n");
  // 기기 MTU는 기본 23 (= ATT 페이로드 20B). 펌웨어가 MTU를 키우지 않으므로
  // 20바이트씩 나눠 쓴다. 펌웨어는 바이트 단위로 재조립하니 UTF-8 중간이 갈려도 무해.
  for (let i = 0; i < bytes.length; i += 20) {
    await state.rxChar.writeValueWithoutResponse(bytes.slice(i, i + 20));
  }
}

let rxBuf = "";
function onNotify(e) {
  rxBuf += dec.decode(e.target.value);
  let idx;
  while ((idx = rxBuf.indexOf("\n")) >= 0) {
    const line = rxBuf.slice(0, idx).trim();
    rxBuf = rxBuf.slice(idx + 1);
    if (line) onLine(line);
  }
}

function onLine(line) {
  log("←", line);
  const [head, ...rest] = line.split(" ");
  if (head === "SLOT") {
    const [n, type, ...pv] = rest;
    state.slots.push({ n: parseInt(n), type, preview: pv.join(" ") });
  } else if (head === "OK" && rest[0] === "LIST") {
    renderSlots();
  } else if (head === "INFO" || (head === "OK" && rest[0] === "INFO") ) {
    // INFO fw=.. slots=.. used=.. bat=.. mode=.. bright=..
  }
  if (head === "INFO") {
    const kv = Object.fromEntries(rest.map((t) => t.split("=")));
    $("infFw").textContent = kv.fw || "-";
    $("infBat").textContent = (kv.bat || "-") + "%";
    $("infOwner").textContent = kv.owner && kv.owner !== "-"
      ? `${kv.owner} (${kv.serial || ""})` : "-";
    // 기기가 알려준 실제 하드웨어 사양을 반영 (보드마다 LED 색·개수가 다르다)
    // color= 를 주지 않는 기기(미등록 보드·시뮬레이터)에 붙었을 땐 이전 기기의 색을
    // 그대로 물고 있으면 안 되므로 기본값으로 되돌린다
    state.monoColor = (kv.color && /^[0-9A-Fa-f]{6}$/.test(kv.color))
      ? kv.color.toUpperCase()
      : DEFAULT_MONO_COLOR;
    const n = parseInt(kv.leds);
    state.ledCount = (n >= 1 && n <= 8) ? n : DEFAULT_LED_COUNT;
    setMonoUI(!fwIsRgb(kv.fw));
    if (kv.bright) { $("bright").value = kv.bright; $("brightVal").textContent = kv.bright + "%"; }
    if (kv.mode) setSegUI(kv.mode);
  }
  if (head === "ERR") alert("기기 오류: " + line);
}

// ---------- BLE 연결 ----------
async function connect() {
  if (state.sim) return;
  try {
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [NUS_SERVICE] }],
      optionalServices: [NUS_SERVICE],
    });
    device.addEventListener("gattserverdisconnected", onDisconnected);
    const server = await device.gatt.connect();
    const svc = await server.getPrimaryService(NUS_SERVICE);
    state.rxChar = await svc.getCharacteristic(NUS_RX);
    const tx = await svc.getCharacteristic(NUS_TX);
    await tx.startNotifications();
    tx.addEventListener("characteristicvaluechanged", onNotify);
    state.device = device;
    setConnUI(true, device.name || "POV-STICK");
    await sendLine("INFO");
    refreshSlots();
  } catch (err) {
    if (err.name !== "NotFoundError") alert("연결 실패: " + err.message);
  }
}
function disconnect() {
  if (state.device?.gatt.connected) state.device.gatt.disconnect();
  onDisconnected();
}
function onDisconnected() {
  state.device = null;
  state.rxChar = null;
  setConnUI(false);
  setMonoUI(true);  // 연결 해제 시에도 단색 UI 유지 (현재 하드웨어가 단색이므로)
}
function setConnUI(on, name) {
  const btn = $("btnConnect");
  btn.classList.toggle("connected", on);
  btn.textContent = on ? "연결됨" : "연결";
  $("infDev").textContent = on ? name : "-";
}
function toastConnectNeeded() {
  alert(state.sim ? "" : "기기를 먼저 연결하거나 시뮬레이터 모드를 켜세요 (설정 탭)");
}

// ---------- 펌웨어 버전 → UI 모드 ----------
// fw 1.0.0 이상 = RGB 완드(풀컬러 UI), 미만 = 단색 완드(색 선택 숨김)
// 판별 불가(시뮬레이터 fw=sim 등)는 **단색**으로 본다 — 현재 존재하는 하드웨어가
// 전부 단색이라, 못 쓰는 색 선택 UI를 보여주는 것이 더 혼란스럽다.
// 3색 LED 완드를 만들면 그 펌웨어의 FW_VERSION을 1.0.0 이상으로 올리면 자동 전환된다.
function fwIsRgb(fw) {
  const v = (fw || "").split(".").map(Number);
  if (v.length < 3 || v.some(isNaN)) return false;
  return v[0] >= 1;
}
function setMonoUI(mono) {
  state.mono = mono;
  document.body.classList.toggle("mono", mono);
  renderPreview();   // 라벨 갱신은 renderPreview가 담당 (잘림 표시와 합쳐야 하므로)
}

// ---------- 색 ----------
function currentColorSpec() {
  if (state.mono) return state.monoColor;   // 그 보드에 실제로 달린 LED 색
  if (state.color === "rainbow") return "rainbow";
  if (state.color === "grad") {
    const a = $("gradA").value.slice(1).toUpperCase();
    const b = $("gradB").value.slice(1).toUpperCase();
    return `grad:${a}-${b}`;
  }
  return state.color;
}

// ---------- 미리보기 (펌웨어와 동일한 8x8 비트맵 폰트) ----------
// 이전에는 캔버스에 시스템 폰트를 8px로 그려 알파 임계값으로 잘라냈는데,
// Galmuri7이 앱에 없어 Malgun Gothic으로 폴백되면서 한글이 뭉개졌고
// 폰트를 넣어도 캔버스 래스터화라 기기 출력과 달랐다.
// 이제 font8x8.js(= 펌웨어 font8x8.h/font8x8_kr.h와 같은 테이블)를 직접 읽어
// 미리보기가 기기 출력과 픽셀 단위로 일치한다.

const MAX_COLS = 256;   // 펌웨어 MAX_COLS와 반드시 같아야 함

// 펌웨어 glyphFor()와 동일 규칙. 폰트에 없는 문자는 ▯
function glyphFor(cp) {
  const g = new Uint8Array(8);
  if (cp >= 0x20 && cp < 0x7F && window.FONT8) {
    g.set(window.FONT8.subarray((cp - 0x20) * 8, (cp - 0x20) * 8 + 8));
  } else if (cp >= 0xAC00 && cp <= 0xD7A3 && window.FONT8_KR) {
    g.set(window.FONT8_KR.subarray((cp - 0xAC00) * 8, (cp - 0xAC00) * 8 + 8));
  } else {
    g.fill(0x81); g[0] = 0xFF; g[7] = 0xFF;
  }
  return g;
}

// 펌웨어 renderText()와 동일: 글자당 8컬럼 + 간격 1컬럼, MAX_COLS에서 잘림
function textToColumns(text) {
  const cols = [];
  let truncated = false;
  for (const ch of text) {
    const cp = ch.codePointAt(0);
    if (cp < 0x20) continue;
    if (cols.length >= MAX_COLS - 9) { truncated = true; break; }
    const g = glyphFor(cp);
    for (let gx = 0; gx < 8; gx++) cols.push(g[gx]);
    cols.push(0);
  }
  return { cols, truncated };
}

// 색 보간에 쓰는 전체 폭도 펌웨어와 같은 방식으로 계산
function colorWidth(text) {
  let total = 0;
  for (const _ of text) {
    if (total >= MAX_COLS) break;
    total += 9;
  }
  return total || 9;
}

function renderPreview() {
  const text = $("sendText").value || "안녕";
  // 단색 완드는 기기가 알려준 LED 개수만큼만 (bit0부터). RGB 완드는 8행 전체
  const H = state.mono ? state.ledCount : 8;
  const { cols, truncated } = textToColumns(text);
  const total = colorWidth(text);

  // backing store를 컬럼 수에 맞춰 잡고 CSS(width:100%)가 축소하게 둔다.
  // 그래야 긴 문구도 잘리지 않고, image-rendering:pixelated로 도트가 선명하다
  const dot = 6;
  const cv = $("preview");
  cv.width = Math.max(8, cols.length) * dot;
  cv.height = H * dot + 8;

  const ctx = cv.getContext("2d");
  ctx.fillStyle = "#0B0A12";
  ctx.fillRect(0, 0, cv.width, cv.height);

  const spec = currentColorSpec();
  for (let x = 0; x < cols.length; x++) {
    for (let y = 0; y < H; y++) {
      if (!((cols[x] >> y) & 1)) continue;
      ctx.fillStyle = pixelColor(spec, x, total);
      ctx.beginPath();
      ctx.arc(x * dot + dot / 2, y * dot + dot / 2 + 4, dot * 0.42, 0, 7);
      ctx.fill();
    }
  }
  updatePreviewLabel(truncated);
}

// 완드 색은 미리보기 도트 색으로 이미 보이므로 라벨에 따로 적지 않는다.
// 기기 한도를 넘겨 잘릴 때만 경고를 덧붙인다
function updatePreviewLabel(truncated) {
  const parts = ["잔상 미리보기"];
  if (truncated) parts.push("기기 한도 초과 — 뒷부분 잘림");
  document.querySelector(".preview-label").textContent = parts.join(" · ");
}
function pixelColor(spec, x, w) {
  if (spec === "rainbow") return `hsl(${Math.round((x / w) * 300)} 95% 62%)`;
  if (spec.startsWith("grad:")) {
    const [a, b] = spec.slice(5).split("-").map(hex2rgb);
    const t = x / Math.max(1, w - 1);
    return `rgb(${a.map((v, i) => Math.round(v + (b[i] - v) * t)).join(",")})`;
  }
  const [r, g, bl] = hex2rgb(spec);
  return `rgb(${r},${g},${bl})`;
}
function hex2rgb(h) {
  return [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16));
}

// ---------- 프리셋 (앱 로컬 저장 — 송별 메시지 등) ----------
// 기본 예시 없이 빈 목록에서 시작. 사용자가 "+ 추가"로 직접 채운다
function getPresets() {
  return JSON.parse(localStorage.getItem("presets") || "[]");
}
function setPresets(p) { localStorage.setItem("presets", JSON.stringify(p)); renderPresets(); }
function renderPresets() {
  const ul = $("presetList");
  ul.innerHTML = "";
  getPresets().forEach((p, i) => {
    const li = document.createElement("li");
    const txt = document.createElement("span");
    txt.className = "txt";
    txt.textContent = p.text;
    const send = document.createElement("button");
    send.className = "mini send";
    send.textContent = "보내기";
    send.onclick = () => sendLine(`SHOW ${p.color} ${p.text}`).catch(() => {});
    const del = document.createElement("button");
    del.className = "mini del";
    del.textContent = "×";
    del.onclick = () => { const arr = getPresets(); arr.splice(i, 1); setPresets(arr); };
    li.append(txt, send, del);
    ul.appendChild(li);
  });
}

// ---------- 슬롯 (기기 저장) ----------
function refreshSlots() {
  state.slots = [];
  sendLine("LIST").catch(() => {});
}
function renderSlots() {
  const ul = $("slotList");
  ul.innerHTML = "";
  $("slotCount").textContent = `${state.slots.length}/12 사용`;
  state.slots.forEach((s) => {
    const li = document.createElement("li");
    const txt = document.createElement("span");
    txt.className = "txt";
    txt.innerHTML = `${escapeHtml(s.preview)}<span class="meta">슬롯 ${s.n} · ${s.type}</span>`;
    const show = document.createElement("button");
    show.className = "mini send";
    show.textContent = "표시";
    show.onclick = () => sendLine(`SHOWSLOT ${s.n}`).catch(() => {});
    const sel = document.createElement("button");
    sel.className = "mini";
    sel.textContent = "시작";
    sel.onclick = () => sendLine(`SEL ${s.n}`).catch(() => {});
    const del = document.createElement("button");
    del.className = "mini del";
    del.textContent = "×";
    del.onclick = () => sendLine(`DEL ${s.n}`).then(() => setTimeout(refreshSlots, 200)).catch(() => {});
    li.append(txt, show, sel, del);
    ul.appendChild(li);
  });
}
function escapeHtml(s) {
  return s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

// ---------- UI 배선 ----------
$("btnConnect").onclick = () => (state.device ? disconnect() : connect());

document.querySelectorAll("nav button").forEach((b) => {
  b.onclick = () => {
    document.querySelectorAll("nav button").forEach((x) => x.classList.remove("on"));
    document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
    b.classList.add("on");
    $("tab-" + b.dataset.tab).classList.add("active");
    if (b.dataset.tab === "slots") refreshSlots();
  };
});

$("colorRow").addEventListener("click", (e) => {
  const btn = e.target.closest("[data-color]");
  if (!btn) return;
  document.querySelectorAll("#colorRow [data-color]").forEach((x) => x.classList.remove("on"));
  btn.classList.add("on");
  state.color = btn.dataset.color;
  $("gradRow").classList.toggle("hidden", state.color !== "grad");
  renderPreview();
});
document.querySelector('.swatch[data-color="FF4FA0"]').classList.add("on");

$("sendText").addEventListener("input", renderPreview);
$("gradA").addEventListener("input", renderPreview);
$("gradB").addEventListener("input", renderPreview);

$("btnShow").onclick = () => {
  const text = $("sendText").value.trim();
  if (!text) return;
  sendLine(`SHOW ${currentColorSpec()} ${text}`).catch(() => {});
};

$("btnAddPreset").onclick = () => {
  const text = $("sendText").value.trim();
  if (!text) { alert("먼저 문구를 입력하세요"); return; }
  setPresets([...getPresets(), { color: currentColorSpec(), text }]);
};

$("btnSaveNew").onclick = () => {
  const text = $("sendText").value.trim();
  if (!text) { alert("보내기 탭에서 문구를 먼저 입력하세요"); return; }
  const used = new Set(state.slots.map((s) => s.n));
  let n = 1;
  while (used.has(n) && n <= 12) n++;
  if (n > 12) { alert("슬롯이 가득 찼어요"); return; }
  sendLine(`SAVE ${n} ${currentColorSpec()} ${text}`)
    .then(() => setTimeout(refreshSlots, 250))
    .catch(() => {});
};

$("bright").oninput = (e) => { $("brightVal").textContent = e.target.value + "%"; };
$("bright").onchange = (e) => sendLine(`SET BRIGHT ${e.target.value}`).catch(() => {});

function setSegUI(mode) {
  document.querySelectorAll("#modeSeg button").forEach((b) =>
    b.classList.toggle("on", b.dataset.mode === mode));
}
$("modeSeg").addEventListener("click", (e) => {
  const b = e.target.closest("[data-mode]");
  if (!b) return;
  setSegUI(b.dataset.mode);
  sendLine(`SET MODE ${b.dataset.mode}`).catch(() => {});
});

$("simToggle").onchange = (e) => {
  state.sim = e.target.checked;
  if (state.sim) {
    disconnect();
    setConnUI(true, "시뮬레이터");
    sendLine("INFO");
    refreshSlots();
  } else {
    setConnUI(false);
  }
};

// 전체화면 (지원 브라우저에서만 버튼 노출)
const fsRoot = document.documentElement;
const fsReq = fsRoot.requestFullscreen || fsRoot.webkitRequestFullscreen;
if (!fsReq) {
  $("btnFull").style.display = "none";
} else {
  $("btnFull").onclick = () => {
    const cur = document.fullscreenElement || document.webkitFullscreenElement;
    if (cur) (document.exitFullscreen || document.webkitExitFullscreen).call(document);
    else fsReq.call(fsRoot);
  };
  // 터치 기기: 첫 탭에 자동 전체화면 (사용자 조작이 있어야 브라우저가 허용)
  if (matchMedia("(pointer: coarse)").matches) {
    const once = () => {
      window.removeEventListener("touchend", once);
      if (!document.fullscreenElement && !document.webkitFullscreenElement) {
        try { fsReq.call(fsRoot); } catch (e) {}
      }
    };
    window.addEventListener("touchend", once);
  }
}

if (!("bluetooth" in navigator)) {
  $("btnConnect").textContent = "BLE 미지원";
  $("btnConnect").disabled = true;
}

renderPresets();
setMonoUI(true);   // 기본 = 단색 완드 UI. 내부에서 renderPreview()까지 수행

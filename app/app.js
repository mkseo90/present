// POV 완드 웹앱 v0.1 — PROTOCOL.md v0.1 구현
"use strict";

const NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // 앱→기기 write
const NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // 기기→앱 notify

const $ = (id) => document.getElementById(id);
const enc = new TextEncoder();
const dec = new TextDecoder();

// ---------- 상태 ----------
const state = {
  device: null,
  rxChar: null,
  sim: false,
  color: "FF4FA0",
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
  // MTU 대비 청크 write (Web Bluetooth는 512B까지지만 보수적으로)
  for (let i = 0; i < bytes.length; i += 180) {
    await state.rxChar.writeValueWithoutResponse(bytes.slice(i, i + 180));
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

// ---------- 색 ----------
function currentColorSpec() {
  if (state.color === "rainbow") return "rainbow";
  if (state.color === "grad") {
    const a = $("gradA").value.slice(1).toUpperCase();
    const b = $("gradB").value.slice(1).toUpperCase();
    return `grad:${a}-${b}`;
  }
  return state.color;
}

// ---------- 미리보기 (텍스트 → 8px 도트) ----------
function renderPreview() {
  const text = $("sendText").value || "안녕";
  const off = document.createElement("canvas");
  const H = 8;
  const octx = off.getContext("2d", { willReadFrequently: true });
  octx.font = "8px Galmuri7, 'Malgun Gothic', sans-serif";
  const W = Math.max(8, Math.ceil(octx.measureText(text).width) + 2);
  off.width = W; off.height = H;
  const o2 = off.getContext("2d", { willReadFrequently: true });
  o2.font = "8px Galmuri7, 'Malgun Gothic', sans-serif";
  o2.fillStyle = "#fff";
  o2.textBaseline = "top";
  o2.fillText(text, 1, 0);
  const img = o2.getImageData(0, 0, W, H).data;

  const cv = $("preview");
  const ctx = cv.getContext("2d");
  const dot = Math.max(2, Math.floor(cv.width / W));
  cv.height = dot * H + 8;
  ctx.fillStyle = "#0B0A12";
  ctx.fillRect(0, 0, cv.width, cv.height);
  const spec = currentColorSpec();
  for (let x = 0; x < W; x++) {
    for (let y = 0; y < H; y++) {
      if (img[(y * W + x) * 4 + 3] > 100) {
        ctx.fillStyle = pixelColor(spec, x, W);
        ctx.beginPath();
        ctx.arc(x * dot + dot / 2, y * dot + dot / 2 + 4, dot * 0.42, 0, 7);
        ctx.fill();
      }
    }
  }
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
function getPresets() {
  return JSON.parse(localStorage.getItem("presets") || JSON.stringify([
    { color: "rainbow", text: "그동안 감사했습니다" },
    { color: "FFD644", text: "새 출발을 응원해요" },
  ]));
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
renderPreview();

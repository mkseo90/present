#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""웹앱을 폰 없이 PC에서 실기기와 붙여보는 테스트 하네스.

왜 필요한가: Web Bluetooth의 `requestDevice()`는 브라우저 네이티브 기기 선택창을
띄우는데, 자동화된 브라우저(헤드리스/원격 제어)에서는 그 창을 표시할 수 없어
NotFoundError로 자동 취소된다. 앱 코드는 정상인데도 테스트가 불가능해진다.

이 스크립트는 선택창만 우회한다:
  - app/ 을 http://localhost:8765 로 서빙하면서 index.html에 shim을 주입
  - shim이 navigator.bluetooth.requestDevice만 교체 → WebSocket(8766)으로 프록시
  - 이 프로세스가 bleak으로 실제 POV-STICK과 BLE 통신

즉 app.js는 한 줄도 고치지 않고, sendLine의 20바이트 청크 분할 · onNotify 재조립 ·
onLine 파싱 · UI 갱신이 전부 실제 펌웨어를 상대로 돌아간다.

준비:  pip install bleak websockets
사용:  python firmware/tools/webapp_bridge.py
       → 브라우저로 http://localhost:8765 열고 "연결" 클릭

주의: 이건 어디까지나 개발용 우회다. 실제 사용 경로(Android Chrome / iOS Bluefy)는
      선택창이 정상 동작하므로 이 스크립트가 필요 없다.
"""
import asyncio
import base64
import functools
import json
import os
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

import websockets
from bleak import BleakClient

from povble import RX_UUID, TX_UUID, find_wand

HTTP_PORT = 8765
WS_PORT = 8766

APP_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "app")
APP_DIR = os.path.normpath(APP_DIR)

SHIM_JS = r"""
// webapp_bridge.py가 주입한 개발용 shim.
// navigator.bluetooth.requestDevice만 교체해 기기 선택창을 우회한다.
(function () {
  const st = { ws: null, notifyCb: null, connected: false, writes: 0, maxWrite: 0 };
  window.__bridge = st;

  st.ws = new WebSocket("ws://127.0.0.1:%WS_PORT%");
  st.ready = new Promise((res) => { st.ws.onopen = res; });
  st.ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.t === "notify" && st.notifyCb) {
      const bin = atob(m.data);
      const u8 = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
      st.notifyCb({ target: { value: new DataView(u8.buffer) } });
    } else if (m.t === "connected") {
      st.connected = true;
      st.resolveConn && st.resolveConn(m);
    } else if (m.t === "error") {
      st.rejectConn && st.rejectConn(new Error(m.msg));
    } else if (m.t === "disconnected") {
      st.connected = false;
    }
  };
  st.send = (o) => st.ws.send(JSON.stringify(o));

  navigator.bluetooth = navigator.bluetooth || {};
  navigator.bluetooth.getAvailability = async () => true;
  navigator.bluetooth.requestDevice = async function () {
    await st.ready;
    const info = await new Promise((res, rej) => {
      st.resolveConn = res;
      st.rejectConn = rej;
      st.send({ t: "connect" });
    });

    const txChar = {
      startNotifications: async function () { return this; },
      addEventListener: (_type, cb) => { st.notifyCb = cb; },
    };
    const rxChar = {
      writeValueWithoutResponse: async (buf) => {
        const u8 = new Uint8Array(buf.buffer || buf, buf.byteOffset || 0,
                                  buf.byteLength || buf.length);
        let s = "";
        u8.forEach((b) => { s += String.fromCharCode(b); });
        st.send({ t: "write", data: btoa(s) });
        st.writes += 1;
        st.maxWrite = Math.max(st.maxWrite, u8.length);
      },
    };
    const svc = {
      getCharacteristic: async (u) =>
        (u.toLowerCase().indexOf("6e400002") === 0 ? rxChar : txChar),
    };
    return {
      name: info.name,
      gatt: {
        get connected() { return st.connected; },
        connect: async () => ({ getPrimaryService: async () => svc }),
        disconnect: () => { st.connected = false; st.send({ t: "disconnect" }); },
      },
      addEventListener: () => {},
    };
  };
  console.log("[webapp_bridge] shim installed — 기기 선택창 우회 활성");
})();
""".replace("%WS_PORT%", str(WS_PORT))


class Handler(SimpleHTTPRequestHandler):
    """index.html에 shim <script>를 끼워 넣어 서빙."""

    def do_GET(self):
        if self.path == "/__bridge.js":
            body = SHIM_JS.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path in ("/", "/index.html"):
            with open(os.path.join(APP_DIR, "index.html"), "rb") as f:
                html = f.read().decode("utf-8")
            # app.js보다 먼저 실행되어야 하므로 </head> 직전에 넣는다
            html = html.replace("</head>", '<script src="/__bridge.js"></script></head>', 1)
            body = html.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        SimpleHTTPRequestHandler.do_GET(self)

    def log_message(self, fmt, *args):
        pass  # 접근 로그는 조용히


def serve_http():
    handler = functools.partial(Handler, directory=APP_DIR)
    httpd = ThreadingHTTPServer(("127.0.0.1", HTTP_PORT), handler)
    httpd.serve_forever()


client = None
ws_peers = set()


async def ws_handler(ws):
    global client
    ws_peers.add(ws)
    print("[ws] 브라우저 연결됨", flush=True)
    try:
        async for raw in ws:
            msg = json.loads(raw)
            kind = msg.get("t")

            if kind == "connect":
                # 광고 이름이 주인마다 다르므로 서비스 UUID로 찾는다
                dev = await find_wand()
                if dev is None:
                    await ws.send(json.dumps({"t": "error", "msg": "완드를 못 찾음"}))
                    continue
                client = BleakClient(dev)
                await client.connect()

                def cb(_sender, data):
                    payload = json.dumps({"t": "notify",
                                          "data": base64.b64encode(bytes(data)).decode()})
                    for w in list(ws_peers):
                        asyncio.create_task(w.send(payload))
                    print("   기기->앱 %r" % bytes(data), flush=True)

                await client.start_notify(TX_UUID, cb)
                await ws.send(json.dumps({"t": "connected",
                                          "name": dev.name or "POV-STICK",
                                          "address": dev.address}))
                print("[ble] 연결: %s" % dev.address, flush=True)

            elif kind == "write":
                data = base64.b64decode(msg["data"])
                print("   앱->기기 %r" % data, flush=True)
                if client and client.is_connected:
                    await client.write_gatt_char(RX_UUID, data, response=False)

            elif kind == "disconnect":
                if client and client.is_connected:
                    await client.disconnect()
                await ws.send(json.dumps({"t": "disconnected"}))
                print("[ble] 연결 해제", flush=True)
    finally:
        ws_peers.discard(ws)


async def main():
    threading.Thread(target=serve_http, daemon=True).start()
    print("웹앱:   http://localhost:%d   (열고 '연결' 클릭)" % HTTP_PORT, flush=True)
    print("브리지: ws://127.0.0.1:%d" % WS_PORT, flush=True)
    async with websockets.serve(ws_handler, "127.0.0.1", WS_PORT):
        await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n종료")

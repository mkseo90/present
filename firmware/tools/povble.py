# -*- coding: utf-8 -*-
"""POV Wand BLE 공용 헬퍼 (이 폴더의 도구들이 함께 쓴다).

기기를 **이름으로 찾지 않는다.** 광고 이름이 주인마다 다르기 때문이다
("<이름>의 LED", 미주입 보드는 "POV-STICK"). 대신 Nordic UART Service UUID를
광고하는 기기를 찾는다 — 이건 펌웨어 버전·주인과 무관하게 항상 같다.

준비: pip install bleak
"""
import asyncio

from bleak import BleakScanner

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # 앱 -> 기기 (write)
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # 기기 -> 앱 (notify)

CHUNK = 20        # MTU 23 기준 ATT 페이로드. 웹앱 sendLine 과 동일하게 유지
CHUNK_GAP = 0.03  # 청크 간 간격. 펌웨어는 150ms 유휴 시 줄을 강제 완성하므로 그보다 짧게


def _advertises_nus(_device, adv):
    uuids = [u.lower() for u in (adv.service_uuids or [])]
    return NUS_SERVICE in uuids


async def find_wand(timeout=20.0, verbose=True, settle=1.5):
    """NUS를 광고하는 완드를 찾아 BLEDevice를 반환. 못 찾으면 None.

    기기 이름은 광고 패킷이 아니라 **스캔 응답**에 담겨 오므로, 첫 보고에서 바로
    반환하면 이름이 비어 있는 경우가 많다. 첫 발견 후 settle초 더 수집해서
    이름까지 채운 뒤 반환한다 (연결 자체는 이름과 무관하게 동작한다).
    """
    if verbose:
        print("완드 스캔 중 (최대 %.0fs, 서비스 UUID로 탐색)..." % timeout, flush=True)

    found = {}

    def cb(device, adv):
        if _advertises_nus(device, adv):
            prev = found.get(device.address)
            name = device.name or (prev[1] if prev else None)
            found[device.address] = (device, name)

    scanner = BleakScanner(detection_callback=cb)
    await scanner.start()
    try:
        waited = 0.0
        step = 0.25
        first_at = None
        while waited < timeout:
            await asyncio.sleep(step)
            waited += step
            if found:
                if first_at is None:
                    first_at = waited
                if waited - first_at >= settle:
                    break
    finally:
        await scanner.stop()

    if not found:
        if verbose:
            print("!! 완드를 못 찾음 — 전원/광고 상태 확인", flush=True)
        return None

    dev, name = next(iter(found.values()))
    if verbose:
        print("발견: %s  (%s)" % (name or "(이름 없음)", dev.address), flush=True)
        if len(found) > 1:
            print("   주의: 완드 %d대가 켜져 있다. 목록은 provision.py --list" % len(found),
                  flush=True)
    return dev


async def find_all_wands(timeout=8.0):
    """여러 대가 켜져 있을 때 목록을 본다. [(name, address), ...]"""
    found = {}

    def cb(device, adv):
        if _advertises_nus(device, adv):
            found[device.address] = device.name or "(이름 없음)"

    scanner = BleakScanner(detection_callback=cb)
    await scanner.start()
    await asyncio.sleep(timeout)
    await scanner.stop()
    return sorted((n, a) for a, n in found.items())


class LineReader:
    """notify 바이트를 줄 단위로 조립한다. EV HB(하트비트)는 걸러낸다."""

    def __init__(self, echo=True, drop_heartbeat=True):
        self._buf = bytearray()
        self.lines = []
        self.echo = echo
        self.drop_heartbeat = drop_heartbeat

    def __call__(self, _sender, data):
        self._buf.extend(data)
        while b"\n" in self._buf:
            i = self._buf.index(b"\n")
            line = bytes(self._buf[:i]).decode("utf-8", "replace").strip()
            del self._buf[: i + 1]
            if not line:
                continue
            if self.drop_heartbeat and line.startswith("EV HB"):
                continue
            if self.echo:
                print("   <- " + line, flush=True)
            self.lines.append(line)

    def clear(self):
        self.lines.clear()

    def info(self):
        """마지막 INFO 응답을 key=value dict 로."""
        for l in reversed(self.lines):
            if l.startswith("INFO "):
                return dict(t.split("=", 1) for t in l.split()[1:] if "=" in t)
        return {}


async def send(client, line, wait=1.8, echo=True):
    """한 줄을 CHUNK 바이트씩 나눠 보낸다 (기기 MTU가 23이라 그 이상은 전달되지 않는다)."""
    if echo:
        print("   -> " + line, flush=True)
    data = (line + "\n").encode("utf-8")
    for i in range(0, len(data), CHUNK):
        await client.write_gatt_char(RX_UUID, data[i : i + CHUNK], response=False)
        await asyncio.sleep(CHUNK_GAP)
    await asyncio.sleep(wait)

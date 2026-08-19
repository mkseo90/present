#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""POV Wand 실기기 BLE 자체 검증 — 폰(nRF Connect) 없이 PC에서 프로토콜을 돌려본다.

준비:
    python -m venv .venv && .venv/Scripts/pip install bleak
사용:
    python ble_selftest.py            # 기본 왕복 검사 (PING/INFO/LIST/SHOW/오류코드)
    python ble_selftest.py fs         # 슬롯 저장·조회·삭제 (USE_FS 1 펌웨어 필요)
    python ble_selftest.py persist    # 재부팅 후 슬롯이 남아있는지 (리셋 뒤 실행)
    python ble_selftest.py pov        # POV 스캔 루프 중 BLE 생존 확인
    python ble_selftest.py clean      # 테스트로 만든 슬롯 정리 + 기본 설정 복구

시리얼(COM)과 동시에 보면 좋다. 단, 시리얼 포트를 잡고 있으면 arduino-cli의
1200-baud 리셋(DFU 진입)이 실패하므로 플래시 전에는 모니터를 닫아야 한다.

기기 탐색은 광고 이름이 아니라 Nordic UART Service UUID로 한다 (povble.py) —
광고 이름이 주인마다 다르기 때문이다.
"""
import asyncio
import sys

from bleak import BleakClient

from povble import LineReader, TX_UUID, find_wand, send

# 광고 이름은 주인마다 다르므로(<이름>의 LED) 이름으로 찾지 않는다 — povble.find_wand 참고
_rx = LineReader(drop_heartbeat=False)   # EV HB 도 세야 하므로 남긴다
received = _rx.lines


# ---------------- 시나리오 ----------------

async def scenario_basic(client):
    print("[기본] 왕복 / 오류코드 / 긴 응답 분할", flush=True)
    for cmd in ("PING", "INFO", "LIST", "SET BRIGHT 55",
                "SHOW rainbow 안녕", "BOGUS", "PING"):
        await send(client, cmd)
    info = [l for l in received if l.startswith("INFO ")]
    ok = received.count("PONG") >= 2
    # 긴 응답이 중간에 잘리지 않았는지 (notify 큐 부족 시 패킷이 격번 유실된다)
    if info and "mac=" not in info[0]:
        print("!! INFO 응답이 잘렸다 — notify 큐(hvn_qsize) 설정을 확인할 것", flush=True)
        ok = False
    if "ERR 1 unknown command" not in received:
        print("!! 오류 응답이 잘렸거나 안 왔다", flush=True)
        ok = False
    return ok


async def scenario_fs(client):
    print("[FS] 슬롯 저장·조회·삭제", flush=True)
    await send(client, "LIST", 2.5)
    await send(client, "SAVE 1 rainbow 사랑해")
    await send(client, "SAVE 2 FF66CC 공주님")
    await send(client, "SAVE 3 grad:FF0000-0000FF 잘가요")
    await send(client, "LIST", 3.0)
    await send(client, "INFO", 2.0)
    await send(client, "SHOWSLOT 2")
    await send(client, "SEL 2")
    await send(client, "SAVE 99 rainbow x")   # ERR 2 기대
    await send(client, "SHOWSLOT 9")          # ERR 2 기대
    await send(client, "DEL 1")
    await send(client, "LIST", 3.0)
    slots = [l for l in received if l.startswith("SLOT ")]
    errs = [l for l in received if l.startswith("ERR 2")]
    print("   SLOT 줄 %d개, ERR 2 %d개" % (len(slots), len(errs)), flush=True)
    return len(slots) >= 5 and len(errs) >= 2


async def scenario_persist(client):
    print("[영속성] 재부팅 후 슬롯 확인", flush=True)
    await send(client, "LIST", 2.5)
    await send(client, "INFO", 2.0)
    slots = [l for l in received if l.startswith("SLOT ")]
    print("   남아있는 슬롯 %d개" % len(slots), flush=True)
    return len(slots) > 0


async def scenario_pov(client):
    print("[POV] 컬럼 스캔 루프 중 BLE 생존", flush=True)
    await send(client, "SHOW rainbow 선물")
    await send(client, "SET SPEED 10")
    await send(client, "SET MODE pov", 2.0)
    before = received.count("PONG")
    for _ in range(3):
        await send(client, "PING", 1.5)
    got = received.count("PONG") - before
    await send(client, "SET MODE auto", 2.0)
    await send(client, "TEST chase", 2.5)
    await send(client, "TEST end")
    print("   POV 스캔 중 PONG %d/3" % got, flush=True)
    return got == 3


async def scenario_clean(client):
    print("[정리] 테스트 슬롯 삭제 + 기본 설정 복구", flush=True)
    await send(client, "SET MODE auto")
    await send(client, "SET SPEED 0")
    await send(client, "SET BRIGHT 80")
    await send(client, "SEL 1", 1.5)
    for n in range(1, 13):
        await send(client, "DEL %d" % n, 0.9)
    await send(client, "LIST", 2.5)
    return not [l for l in received if l.startswith("SLOT ")]


SCENARIOS = {
    "basic": scenario_basic,
    "fs": scenario_fs,
    "persist": scenario_persist,
    "pov": scenario_pov,
    "clean": scenario_clean,
}


async def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "basic"
    if which not in SCENARIOS:
        print("사용법: ble_selftest.py [%s]" % "|".join(SCENARIOS))
        return 2

    dev = await find_wand()
    if dev is None:
        return 2

    async with BleakClient(dev) as client:
        await client.start_notify(TX_UUID, _rx)
        await asyncio.sleep(1.2)
        ok = await SCENARIOS[which](client)
        # 하트비트가 계속 오는지 = loop()가 살아있는지
        hb = len([l for l in received if l.startswith("EV HB")])
        alive = client.is_connected

    print("", flush=True)
    print("=== 결과 (%s) ===" % which, flush=True)
    print("  시나리오 판정 : %s" % ("OK" if ok else "실패"), flush=True)
    print("  EV HB 수신    : %d  (0이면 loop() 정지 의심)" % hb, flush=True)
    print("  연결 유지     : %s" % alive, flush=True)
    print("  총 수신 줄    : %d" % len(received), flush=True)
    passed = ok and alive and hb > 0
    print("  → %s" % ("PASS" if passed else "FAIL"), flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))

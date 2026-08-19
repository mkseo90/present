#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""보드에 주인 정보(이름·시리얼·LED색)를 주입한다.

왜 소스가 아니라 기기에 넣는가:
  - 공개 저장소에 사람 실명이 남지 않는다
  - 새 PC로 옮길 때 secrets.h 같은 파일을 들고 다닐 필요가 없다
  - 보드마다 다른 값(이름·색)이 그 보드 안에 있으므로 헷갈릴 일이 없다
주입한 값은 LittleFS(/owner)에 저장되어 재부팅·재플래시 후에도 유지된다.
(전체 칩 지우기를 하면 사라지므로 그때는 다시 주입한다)

준비:  pip install bleak
사용:
    python provision.py "민경" "No.001/001" 00FF66    # 주입
    python provision.py --show                        # 현재 값 조회
    python provision.py --clear                       # 삭제

주의: 이름·시리얼에 공백을 넣을 수 없다 (INFO 응답이 공백으로 토큰을 나눈다).
"""
import asyncio
import re
import sys

from bleak import BleakScanner, BleakClient

DEVICE_NAME = "POV-STICK"
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
CHUNK = 20

_buf = bytearray()
received = []


def _on_notify(_sender, data):
    _buf.extend(data)
    while b"\n" in _buf:
        i = _buf.index(b"\n")
        line = bytes(_buf[:i]).decode("utf-8", "replace").strip()
        del _buf[: i + 1]
        if line and not line.startswith("EV HB"):
            print("   <- " + line, flush=True)
            received.append(line)


async def send(client, line, wait=2.0):
    print("   -> " + line, flush=True)
    data = (line + "\n").encode("utf-8")
    for i in range(0, len(data), CHUNK):
        await client.write_gatt_char(RX_UUID, data[i : i + CHUNK], response=False)
        await asyncio.sleep(0.03)
    await asyncio.sleep(wait)


def show_owner():
    for l in received:
        if not l.startswith("INFO "):
            continue
        kv = dict(t.split("=", 1) for t in l.split()[1:] if "=" in t)
        print("", flush=True)
        print("   현재 주인 정보", flush=True)
        print("     이름   : %s" % kv.get("owner", "?"), flush=True)
        print("     시리얼 : %s" % kv.get("serial", "?"), flush=True)
        print("     LED색  : %s" % kv.get("color", "?"), flush=True)
        print("     LED수  : %s" % kv.get("leds", "?"), flush=True)
        print("     MAC    : %s" % kv.get("mac", "?"), flush=True)
        return kv
    return {}


async def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2

    mode = "set"
    if args[0] == "--show":
        mode = "show"
    elif args[0] == "--clear":
        mode = "clear"
    else:
        if len(args) != 3:
            print("인자 3개가 필요하다: <이름> <시리얼> <색>")
            print('예: python provision.py "민경" "No.001/001" 00FF66')
            return 2
        name, serial, color = args
        for label, v in (("이름", name), ("시리얼", serial)):
            if " " in v:
                print("!! %s에 공백을 넣을 수 없다: %r" % (label, v))
                return 2
        if not re.fullmatch(r"[0-9A-Fa-f]{6}", color):
            print("!! 색은 RRGGBB 6자리 hex 여야 한다: %r" % color)
            return 2
        color = color.upper()

    print("%s 스캔 중 (최대 20s)..." % DEVICE_NAME, flush=True)
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=20.0)
    if dev is None:
        print("!! %s 를 못 찾음 — 전원/연결 상태 확인" % DEVICE_NAME, flush=True)
        return 2
    print("연결: %s" % dev.address, flush=True)

    async with BleakClient(dev) as client:
        await client.start_notify(TX_UUID, _on_notify)
        await asyncio.sleep(1.2)

        if mode == "show":
            await send(client, "INFO", 2.5)
            show_owner()
            return 0

        if mode == "clear":
            await send(client, "CLROWNER")
            received.clear()
            await send(client, "INFO", 2.5)
            show_owner()
            print("\n   삭제 완료 (owner=- 로 보이면 정상)", flush=True)
            return 0

        await send(client, "SETOWNER %s %s %s" % (name, serial, color))
        if not any(l.startswith("OK SETOWNER") for l in received):
            errs = [l for l in received if l.startswith("ERR")]
            print("\n!! 주입 실패: %s" % (errs[0] if errs else "응답 없음"), flush=True)
            return 1
        received.clear()
        await send(client, "INFO", 2.5)
        kv = show_owner()
        ok = kv.get("owner") == name and kv.get("color") == color
        print("", flush=True)
        print("   → %s" % ("주입 완료" if ok else "주입했지만 INFO 확인 실패"), flush=True)
        print("   재부팅 후에도 유지된다. 확인: python provision.py --show", flush=True)
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""보드에 주인 정보(이름·시리얼·LED색)를 주입한다.

왜 소스가 아니라 기기에 넣는가:
  - 공개 저장소에 사람 실명이 남지 않는다
  - 새 PC로 옮길 때 secrets.h 같은 파일을 들고 다닐 필요가 없다
  - 보드마다 다른 값(이름·색)이 그 보드 안에 있으므로 헷갈릴 일이 없다
주입한 값은 LittleFS(/owner)에 저장되어 재부팅·재플래시 후에도 유지된다.
(전체 칩 지우기를 하면 사라지므로 그때는 다시 주입한다)

주인 이름은 **BLE 광고 이름**에도 쓰인다 — "<이름>의 LED".
여러 완드를 동시에 켰을 때 기기 선택창에서 구분하기 위한 것이며,
광고 데이터는 부팅 시 구성되므로 **재부팅 후에 새 이름으로 보인다.**

준비:  pip install bleak
사용:
    python provision.py "민경" "No.001/001" 00FF66    # 주입
    python provision.py --show                        # 현재 값 조회
    python provision.py --clear                       # 삭제
    python provision.py --list                        # 켜져 있는 완드 목록

주의: 이름·시리얼에 공백을 넣을 수 없다 (INFO 응답이 공백으로 토큰을 나눈다).
"""
import asyncio
import re
import sys

from bleak import BleakClient

from povble import LineReader, TX_UUID, find_all_wands, find_wand, send


def print_owner(kv, title="현재 주인 정보"):
    if not kv:
        print("   (INFO 응답을 못 받음)", flush=True)
        return
    print("", flush=True)
    print("   " + title, flush=True)
    print("     이름   : %s" % kv.get("owner", "?"), flush=True)
    print("     시리얼 : %s" % kv.get("serial", "?"), flush=True)
    print("     LED색  : %s" % kv.get("color", "?"), flush=True)
    print("     LED수  : %s" % kv.get("leds", "?"), flush=True)
    print("     MAC    : %s" % kv.get("mac", "?"), flush=True)


async def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2

    if args[0] == "--list":
        wands = await find_all_wands()
        if not wands:
            print("켜져 있는 완드가 없다")
            return 1
        print("켜져 있는 완드 %d대:" % len(wands))
        for name, addr in wands:
            print("   %-24s %s" % (name, addr))
        return 0

    mode = "set"
    name = serial = color = None
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
        if not name:
            print("!! 이름이 비었다")
            return 2
        if not re.fullmatch(r"[0-9A-Fa-f]{6}", color):
            print("!! 색은 RRGGBB 6자리 hex 여야 한다: %r" % color)
            return 2
        color = color.upper()

    dev = await find_wand()
    if dev is None:
        return 2

    rx = LineReader()
    async with BleakClient(dev) as client:
        await client.start_notify(TX_UUID, rx)
        await asyncio.sleep(1.2)

        if mode == "show":
            await send(client, "INFO", 2.5)
            print_owner(rx.info())
            return 0

        if mode == "clear":
            await send(client, "CLROWNER")
            rx.clear()
            await send(client, "INFO", 2.5)
            print_owner(rx.info(), "삭제 후")
            print("\n   owner=- 로 보이면 정상. 재부팅하면 광고 이름도"
                  " POV-STICK 으로 돌아간다", flush=True)
            return 0

        await send(client, "SETOWNER %s %s %s" % (name, serial, color))
        if not any(l.startswith("OK SETOWNER") for l in rx.lines):
            errs = [l for l in rx.lines if l.startswith("ERR")]
            print("\n!! 주입 실패: %s" % (errs[0] if errs else "응답 없음"), flush=True)
            return 1
        rx.clear()
        await send(client, "INFO", 2.5)
        kv = rx.info()
        print_owner(kv, "주입 결과")
        ok = kv.get("owner") == name and kv.get("color") == color
        print("", flush=True)
        print("   → %s" % ("주입 완료" if ok else "주입했지만 INFO 확인 실패"), flush=True)
        print('   BLE 광고 이름은 "%s의 LED" 로 바뀐다 — **재부팅 후 적용**된다' % name,
              flush=True)
        print("   조회: python provision.py --show", flush=True)
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))

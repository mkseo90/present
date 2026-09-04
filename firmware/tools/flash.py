#!/usr/bin/env python3
"""감시 플래셔 — XIAO nRF52840을 리셋 버튼 없이 굽는다.

동작 (USB-C만 꽂으면 됨):
  - 앱 포트(VID 0x2886, PID 상위비트 1)가 보이면 → 1200bps 터치로 부트로더 재부팅
  - 부트로더 포트(PID 상위비트 0)가 보이면 → 즉시 DFU 플래시
  - 이미 부트로더 상태(리셋 2번 눌러둠)여도 그대로 잡아서 굽는다

사용:
  python firmware/tools/flash.py            # 30분 감시
  python firmware/tools/flash.py --once     # 5분 감시

의존: pyserial (pip install pyserial). adafruit-nrfutil은 보드 패키지에서 자동 탐색.
케이스 조립 후에는 앱 관리자 모드에서 BOOTDFU 명령을 보내도 부트로더로 들어간다.
"""
import glob
import os
import subprocess
import sys
import time
from pathlib import Path

import serial
import serial.tools.list_ports

SEEED_VID = 0x2886
ZIP = Path(__file__).resolve().parents[1] / "pov-wand" / "build" / \
    "Seeeduino.nrf52.xiaonRF52840Sense" / "pov-wand.ino.zip"


def find_nrfutil():
    # 1) PATH
    from shutil import which
    w = which("adafruit-nrfutil")
    if w:
        return w
    # 2) Arduino 보드 패키지 동봉본 (버전 무관 탐색)
    roots = [
        os.path.expandvars(r"%LOCALAPPDATA%\Arduino15"),
        os.path.expanduser("~/.arduino15"),
        os.path.expanduser("~/Library/Arduino15"),
    ]
    exe = "adafruit-nrfutil.exe" if os.name == "nt" else "adafruit-nrfutil"
    sub = "win32" if os.name == "nt" else ("macos" if sys.platform == "darwin" else "linux")
    for r in roots:
        hits = glob.glob(os.path.join(r, "packages", "Seeeduino", "hardware", "nrf52",
                                      "*", "tools", "adafruit-nrfutil", sub, exe))
        if hits:
            return sorted(hits)[-1]
    sys.exit("adafruit-nrfutil을 못 찾음 — pip install adafruit-nrfutil 또는 보드 패키지 설치 필요")


NRFUTIL = find_nrfutil()


def scan():
    app, boot = [], []
    for p in serial.tools.list_ports.comports():
        if p.vid != SEEED_VID:
            continue
        (app if (p.pid or 0) & 0x8000 else boot).append(p.device)
    return app, boot


def touch(port):
    print(f"[touch] {port} @1200bps", flush=True)
    try:
        s = serial.Serial(port, 1200)
        s.dtr = True
        time.sleep(0.1)
        s.dtr = False
        s.close()
        return True
    except Exception as e:
        print(f"[touch fail] {port}: {e}", flush=True)
        return False


def flash(port):
    print(f"[flash] {port}", flush=True)
    r = subprocess.run([NRFUTIL, "dfu", "serial", "--package", str(ZIP),
                        "-p", port, "-b", "115200"],
                       capture_output=True, text=True, timeout=180)
    if r.returncode == 0:
        print(f"SUCCESS on {port}", flush=True)
        return True
    tail = (r.stdout + r.stderr).strip().splitlines()
    print(f"[flash fail] {port}: {tail[-1] if tail else 'no output'}", flush=True)
    return False


def main():
    if not ZIP.exists():
        sys.exit(f"빌드 zip 없음: {ZIP}\n먼저 arduino-cli compile -e 로 빌드할 것")
    watch = 300 if "--once" in sys.argv else 1800
    print(f"[armed] XIAO를 USB-C로 연결하세요 ({watch // 60}분 감시)", flush=True)
    deadline = time.time() + watch
    touched_at = 0
    while time.time() < deadline:
        app, boot = scan()
        if boot:
            time.sleep(1.0)  # 열거 안정화
            try:
                if flash(boot[0]):
                    print("done", flush=True)
                    return 0
            except Exception as e:
                print(f"[flash fail] {boot[0]}: {e}", flush=True)
            time.sleep(2.0)
        elif app and time.time() - touched_at > 8:  # 터치 후 재부팅 대기, 그래도 앱이면 재터치
            print(f"[found] 앱 포트 {app[0]}", flush=True)
            touch(app[0])
            touched_at = time.time()
        time.sleep(0.5)
    print("TIMEOUT: XIAO 미발견 — USB 연결/케이블(충전전용 주의) 확인", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())

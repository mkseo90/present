#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_font_js.py — 펌웨어 폰트 헤더를 웹앱용 자산(app/font8x8.js)으로 변환.

왜 필요한가: 웹앱의 잔상 미리보기가 캔버스에 시스템 폰트를 8px로 그려서
알파 임계값으로 잘라내는 방식이었는데, 그러면
  (1) Galmuri7이 앱에 없어서 Malgun Gothic으로 폴백 → 한글이 죽이 되고
  (2) 폰트를 넣어도 캔버스 래스터화라 기기 출력과 일치하지 않는다.
펌웨어가 쓰는 것과 "같은 비트맵 테이블"을 앱에 주면 미리보기가 픽셀 단위로 일치한다.

gen_font.py 와 달리 PIL·Galmuri7.ttf가 필요 없다 — 이미 생성된 .h를 파싱한다.
폰트를 다시 뽑았다면 gen_font.py 를 먼저 돌리고 이걸 돌린다.

형식: 글자당 8바이트, 바이트 = 세로 한 컬럼, bit0 = 맨 위 LED (gen_font.py와 동일)
사용법: python gen_font_js.py
"""
import base64
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.normpath(os.path.join(HERE, "..", "pov-wand"))
APP = os.path.normpath(os.path.join(HERE, "..", "..", "app"))

BYTE_RE = re.compile(r"0[xX]([0-9a-fA-F]{2})")


def parse_header(path, name, expect_glyphs):
    """`static const uint8_t NAME[N][8] = { {..}, {..} };` 에서 바이트를 뽑는다."""
    with open(path, encoding="utf-8") as f:
        src = f.read()

    start = src.index("%s[" % name)
    body = src[src.index("{", start) + 1:]
    body = body[:body.index("\n};")]

    rows = []
    for line in body.splitlines():
        line = line.split("//")[0]           # 주석의 문자 리터럴 제외
        if "{" not in line:
            continue
        vals = [int(h, 16) for h in BYTE_RE.findall(line)]
        if not vals:
            continue
        if len(vals) != 8:
            raise SystemExit("글리프 바이트 수가 8이 아님: %r" % line)
        rows.append(bytes(vals))

    if len(rows) != expect_glyphs:
        raise SystemExit("%s: 글리프 %d개 기대했는데 %d개 파싱됨"
                         % (name, expect_glyphs, len(rows)))
    return b"".join(rows)


def main():
    ascii_bin = parse_header(os.path.join(FW, "font8x8.h"), "FONT8", 95)
    kr_bin = parse_header(os.path.join(FW, "font8x8_kr.h"), "FONT8_KR", 11172)

    out = os.path.join(APP, "font8x8.js")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("// 자동 생성 파일 — firmware/tools/gen_font_js.py. 직접 수정하지 말 것\n")
        f.write("// 펌웨어(font8x8.h / font8x8_kr.h)와 동일한 비트맵 테이블.\n")
        f.write("// 글자당 8바이트, 바이트 = 세로 한 컬럼, bit0 = 맨 위 LED.\n")
        f.write('"use strict";\n')
        f.write("(function () {\n")
        f.write("  function unb64(s) {\n")
        f.write("    const bin = atob(s);\n")
        f.write("    const u8 = new Uint8Array(bin.length);\n")
        f.write("    for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);\n")
        f.write("    return u8;\n")
        f.write("  }\n")
        f.write("  // ASCII 0x20~0x7E (95자)\n")
        f.write('  window.FONT8 = unb64("%s");\n' % base64.b64encode(ascii_bin).decode())
        f.write("  // 한글 음절 U+AC00~U+D7A3 (11172자). 인덱스 = 코드포인트 - 0xAC00\n")
        f.write('  window.FONT8_KR = unb64("%s");\n' % base64.b64encode(kr_bin).decode())
        f.write("})();\n")

    size = os.path.getsize(out)
    print("written: %s" % out)
    print("  ASCII  %d글리프 (%d바이트)" % (len(ascii_bin) // 8, len(ascii_bin)))
    print("  한글   %d글리프 (%d바이트)" % (len(kr_bin) // 8, len(kr_bin)))
    print("  파일   %.1fKB (GitHub Pages가 gzip으로 전송)" % (size / 1024.0))


if __name__ == "__main__":
    main()

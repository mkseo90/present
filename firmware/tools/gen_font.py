# gen_font.py — Galmuri7(픽셀 폰트)을 8x8 도트로 샘플링해 C 헤더 생성
# 출력: font8x8.h (ASCII 95자), font8x8_kr.h (한글 음절 11,172자)
# 형식: 글자당 8바이트, 바이트 = 세로 한 컬럼, bit0 = 맨 위 LED
# 사용법: python gen_font.py [--preview "AB가힣"]
import sys
from PIL import Image, ImageFont, ImageDraw

FONT_PATH = "Galmuri7.ttf"
SIZE = 8      # Galmuri7 네이티브 픽셀 크기
Y_OFF = 1     # 글리프가 그려지는 세로 시작 행 (실측: 1~8행 사용)
H = 8

_font = ImageFont.truetype(FONT_PATH, SIZE)

def render_glyph(ch):
    img = Image.new("1", (16, 12), 0)
    ImageDraw.Draw(img).text((0, 0), ch, fill=1, font=_font)
    px = img.load()
    box = img.getbbox()
    if box is None:
        return [0] * 8  # 공백문자
    x0, x1 = box[0], box[2]
    cols = []
    for x in range(x0, min(x1, x0 + 8)):
        b = 0
        for y in range(H):
            if px[x, y + Y_OFF]:
                b |= 1 << y
        cols.append(b)
    pad = 8 - len(cols)
    return [0] * (pad // 2) + cols + [0] * (pad - pad // 2)

def ascii_art(cols):
    return "\n".join(
        "".join("#" if cols[x] >> y & 1 else "." for x in range(8)) for y in range(H)
    )

def emit(path, name, chars, comment):
    lines = [
        "// 자동 생성 파일 — gen_font.py (Galmuri7, OFL 라이선스). 직접 수정하지 말 것",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"// {comment}",
        f"static const uint8_t {name}[{len(chars)}][8] = {{",
    ]
    for i, ch in enumerate(chars):
        cols = render_glyph(ch)
        hexs = ", ".join(f"0x{b:02X}" for b in cols)
        tag = f" // '{ch}'" if (i % 16 == 0 or len(chars) < 200) else ""
        lines.append(f"  {{ {hexs} }},{tag}")
    lines.append("};")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("written:", path, f"({len(chars)} glyphs)")

def main():
    if "--preview" in sys.argv:
        for ch in sys.argv[sys.argv.index("--preview") + 1]:
            print(f"--- {ch!r}")
            print(ascii_art(render_glyph(ch)))
        return
    base = __file__.replace("tools\\gen_font.py", "pov-wand\\")
    emit(base + "font8x8.h", "FONT8",
         [chr(c) for c in range(0x20, 0x7F)],
         "ASCII 0x20~0x7E. 글자당 8바이트(컬럼), bit0 = 상단 LED")
    emit(base + "font8x8_kr.h", "FONT8_KR",
         [chr(c) for c in range(0xAC00, 0xD7A4)],
         "한글 음절 U+AC00(가)~U+D7A3(힣). 인덱스 = 코드포인트 - 0xAC00")

main()

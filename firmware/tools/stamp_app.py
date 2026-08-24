# -*- coding: utf-8 -*-
"""stamp_app.py — 웹앱 배포 스탬프.

app/index.html 에서:
  1. 빌드 시각(<span id="buildStamp">)을 현재 시각으로 갱신
  2. 캐시버스터 ?v=N 을 전부 N+1 로 증가

배포 전에 항상 이걸 돌리고 커밋한다. 폰에서는 설정 탭 맨 아래 빌드 시각으로
지금 보는 페이지가 최신인지 즉시 확인할 수 있다.
"""
import datetime
import os
import re

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "app", "index.html")
path = os.path.normpath(path)
src = open(path, encoding="utf-8").read()

now = datetime.datetime.now().strftime("%m/%d %H:%M")
src, n1 = re.subn(r'(<span id="buildStamp">)[^<]*(</span>)', r'\g<1>빌드 ' + now + r'\g<2>', src)

vs = sorted({int(m) for m in re.findall(r"\?v=(\d+)", src)})
if vs:
    newv = vs[-1] + 1
    src = re.sub(r"\?v=\d+", "?v=%d" % newv, src)
else:
    newv = "?"

open(path, "w", encoding="utf-8", newline="\n").write(src)
print("stamp: %s / cache v=%s / buildStamp %s" % (now, newv, "OK" if n1 else "태그 없음!"))

# HANDOFF — 브링업 디버깅 인수인계 (2026-08-19)

다른 PC에서 이어서 작업하기 위한 상태 스냅샷. 이 문서를 읽는 Claude/개발자는
여기서부터 시작하면 된다.

## 현재 상태 (한 줄 요약)

**"BLE로 데이터를 수신하는 순간 펌웨어 전체가 얼어붙는(하드폴트 추정) 버그"를
이분탐색 중. 순정 최소 스케치는 정상 → 우리 스케치 문제. 현재 유력 용의자 = LittleFS.
FS 완전 비활성 빌드(USE_FS=0)를 만들어놨고, 이걸 굽고 PING 테스트하는 게 다음 단계.**

## 검증된 사실 (믿어도 됨)

- 보드(XIAO nRF52840 Sense) 2번째 개체, PC USB 연결, 시리얼 115200
- BLE 광고/연결 유지 정상 (nRF Connect로 1분+ 유지)
- 보드→폰 notify 정상 (EV HB 하트비트 수신됨)
- LED 7채널(D0~D6) GPIO 직결 구동 정상 (대기 물결 패턴 동작 확인)
- **순정 최소 에코 스케치(firmware/test-bleuart/)는 수신·에코 완벽 동작, 안 죽음**
- 본 펌웨어(firmware/pov-wand/)는 **어떤 데이터든 수신하면**: 시리얼 로그 정지,
  LED 정지, BLE 링크 사망. `RX:` 파서 로그가 찍히기 **전에** 얼어붙음

## 기각된 가설들 (다시 파지 말 것)

| 가설 | 기각 근거 |
|------|----------|
| 폰 BT 캐시/주소타입 오염 | BT 토글·주소 변조로도 동일, 새 보드(새 주소)에서도 재현 |
| configPrphBandwidth(BANDWIDTH_MAX) | 제거 후에도 재현 (단, 제거 유지 — MTU는 기본 23) |
| 플래시 저장 타이밍 | 저장을 1.5초 지연시켜도 명령 수신 즉시 얼음 |
| BLE 태스크에서 Serial 출력(rx콜백) | 콜백 제거 후에도 재현 |
| LED 전원/브라운아웃 | LED 전부 분리한 보드 단독에서도 재현 |
| Seeed 코어 자체 버그 | 순정 최소 스케치는 정상 동작 |
| 보드 개체 불량 | 보드 2개에서 유사 증상 (단, 1번 보드는 별도 재검 필요) |

## 다음 단계 (여기서부터)

1. 현재 체크아웃된 코드 = `USE_FS 0` (LittleFS 완전 비활성). 컴파일 확인됨.
2. 이 빌드를 굽고 nRF Connect로 `PING` 전송:
   - **안 죽고 PONG 오면** → LittleFS 범인 확정. Seeed 코어 1.1.13의 InternalFS
     플래시 영역이 부트로더/SoftDevice 배치와 충돌하는지 조사
     (Adafruit InternalFileSystem 영역 정의 vs 이 보드 부트로더 버전).
     대응 후보: 코어 버전 변경, FS 영역 주소 수정, 또는 설정 저장을 UICR/파일 대신
     다른 방식으로.
   - **그래도 죽으면** → 남은 차이를 이분탐색: 폰트 테이블(font8x8_kr.h 87KB) 제거 →
     renderText/content 버퍼 제거 → setupBle 옵션들(ScanResponse/FastTimeout/TxPower/콜백)
     하나씩 → 최소 스케치에 도달할 때까지. 매 빌드 PING 테스트.
3. 범인 확정 후: 디버그 코드 정리(하트비트 [hb], [reply>] 추적, TEST 명령은 유지 검토),
   USE_FS 복구, 그 다음 POV 잔상 테스트(SET MODE pov + SET SPEED 10, 흔들기).

## 환경 세팅 (새 PC)

1. Arduino IDE 2.x 설치 → 기본 설정 → 추가 보드 매니저 URL:
   `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`
2. 보드 매니저에서 "Seeed nRF52 Boards" 설치 (**mbed 아닌 것**, v1.1.13 기준)
3. 라이브러리: Adafruit NeoPixel (LED_TYPE 2용, 지금은 불필요)
4. `firmware/pov-wand/secrets.example.h`를 `secrets.h`로 복사 (gitignore된 파일)
5. 보드 선택: Seeed XIAO nRF52840 Sense

## 펌웨어 굽는 절차 (Windows, IDE 업로드가 꼬일 때)

```
# 1) 보드 리셋 버튼 재빨리 2번 (부트로더 진입, 포트 번호가 바뀜)
# 2) 부트로더 포트 확인
arduino-cli board list
# 3) 빌드 & 굽기 (IDE에서 스케치 → 컴파일된 바이너리 내보내기 후)
adafruit-nrfutil dfu serial -pkg <build>/pov-wand.ino.zip -p COM<N> -b 115200 --singlebank
```
- arduino-cli는 Arduino IDE에 내장 (resources/app/lib/backend/resources/)
- adafruit-nrfutil은 보드 패키지 tools/ 안에 있음
- --touch 1200 자동 리셋은 이 환경에서 신뢰 불가 → 수동 더블탭 + 새 포트 확인이 확실

## 테스트 절차 (nRF Connect 앱)

1. 스캔 → `POV-STICK` 연결 (최소 스케치는 `POV-TEST`)
2. Nordic UART Service → TX(…6E400003…) ↓↓↓ 눌러 notify 구독
3. RX(…6E400002…)에 TEXT로 `PING` 전송 (줄바꿈 불필요 — 150ms 유휴 시 줄 완성 처리 있음)
4. 정상 기준: 시리얼에 `RX: PING`, 폰에 `PONG`, [hb] 하트비트 지속, LED 물결 지속

## 참고

- 프로토콜: PROTOCOL.md (웹앱·펌웨어의 기준 문서)
- 웹앱: app/ (GitHub Pages 배포: /present/), 시뮬레이터 모드 내장
- 웹앱 실기기 연결 검증은 펌웨어 안정화 후 진행 (MTU 23 기준 앱의 write 청크를
  180B→20B로 줄여야 할 수 있음 — app.js sendLine 참고)

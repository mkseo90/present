# HANDOFF — 브링업 상태 (2026-08-19)

다른 PC에서 이어서 작업하기 위한 상태 스냅샷.

## 현재 상태 (한 줄 요약)

**"BLE 수신 즉시 프리즈" 버그 해결. 실기기(2번 보드) 검증 완료 — PING 왕복, INFO/LIST
분할 응답, LittleFS 슬롯 저장·조회·삭제·재부팅 영속성, POV 스캔 중 BLE 생존까지 전부 통과.
`USE_FS 1` 상태. 남은 건 IMU 스윙 감지·배터리 ADC 구현과 실물 LED 조립 검증.**

## 원인: 서로 무관한 버그 두 개였다

### 버그 1 — 수신 즉시 전체 사망 (스택 오버플로)

`handleLine()` 안의 `imgRx = {};` 한 줄. `ImgRx`는 `buf[MAX_COLS*24]` 포함 약 6,152B이고
멤버 기본값(NSDMI)이 있어 aggregate가 아니므로 `{}`는 **임시 객체를 만들어 복사대입**한다.
GCC는 그 임시 객체 자리를 **함수 프롤로그에서 미리 잡는다** — IMGB 분기를 타는지와 무관하게
진입 즉시 `sp`가 내려간다.

실측 (`arm-none-eabi-objdump -d`):

```
수정 전  <handleLine(char*)>:
           push  {r4, r5, r6, r7, lr}      ;    20 B
           sub.w sp, sp, #6176             ; 6,176 B
           sub   sp, #4                    ;     4 B
                              합계  6,200 B   ← 첫 Serial.print보다 먼저 실행된다

수정 후  <handleLine(char*)>:
           push  {r4, r5, r6, lr}          ;    16 B
           sub   sp, #32                   ;    32 B
                              합계     48 B
```

코어의 loop 태스크 스택은 `cores/nRF5/main.cpp:42` `#define LOOP_STACK_SZ (256*4)` =
**4,096 B**. 6,200 − 4,096 = **2,104 B 초과**. 그래서 명령 종류와 무관하게, 파서 로그가
찍히기도 전에 죽었다.

**함정**: `Serial`(USB CDC)은 TinyUSB FIFO에 버퍼링되고 USB 태스크가 나중에 내보낸다.
하드폴트가 나면 버퍼에 남은 바이트는 사라지므로, **"로그가 안 찍혔다"는 "거기 도달 전에
죽었다"의 증거가 아니다.** 이분탐색이 헛돌던 이유. 지금은 수신·응답 경로에 `Serial.flush()`가
붙어 있다.

### 버그 2 — 긴 응답이 격번으로 유실 (notify 큐)

버그 1을 고치고 나서야 보였다(그전엔 응답이 나가기 전에 죽었으니까). 실측 예:

```
받은 것  INFO fw=0.0.2 slots=|de=auto bright=80 ow|A:BB:CC:DD:EE:FF
보낸 것  INFO fw=0.0.2 slots=|12 used=0 bat=100 mo|de=auto bright=80 ow|ner=- serial=- mac=D|A:BB:CC:DD:EE:FF
                             ^^^^^ 유실                                 ^^^^^ 유실
```

SoftDevice의 HVN(notify) 송신 큐가 기본 **1칸**이고, 코어는 빈 칸을 100ms
(`BLE_GENERIC_TIMEOUT`)만 기다린 뒤 **조용히 실패하고 그 패킷을 버린다**
(`BLECharacteristic::notify` → `if (!conn->getHvnPacket()) return false;`).
`BLEUart::write()`의 반환값을 확인하지 않으면 유실을 알 수도 없다.

**대응(둘 다 적용)**:
1. `Bluefruit.configPrphConn(..., hvn_qsize = 8, ...)` — 큐를 8칸으로. **이게 주 해결책.**
   MTU는 기본값 23 유지 → 큐 버퍼가 23B라 8칸도 RAM 부담이 없고, 웹앱의 20B 청크 가정도
   그대로 유지된다. (`Bluefruit.begin()` **앞에서** 호출해야 적용된다)
2. `reply()`에서 `write()` 반환값을 보고 재시도 — 2차 방어선. 큐 8칸 적용 후 실측에서는
   한 번도 발동하지 않았다.

`Bluefruit.Periph.setConnInterval(6, 12)`로 짧은 인터벌도 요청한다(중앙장치가 수락할
의무는 없음 — Windows는 60ms로 잡았다).

## 적용한 수정

펌웨어 (`firmware/pov-wand/pov-wand.ino`):

1. `imgRx = {}` → `memset(&imgRx, 0, sizeof(imgRx))` — **버그 1 근본 원인**
2. `struct ImgRx`의 멤버 기본값 제거 — 다시 `ImgRx{}`를 쓰지 못하게 aggregate 유지
   (전역이라 .bss에서 0 초기화됨)
3. `configPrphConn(MTU 23, event_len 6, hvn_qsize 8, ...)` — **버그 2 근본 원인**
4. `reply()`: MTU-3 단위 분할 + 실패 시 재시도 + `Serial.flush()`
5. `handleLine()` 진입 로그에 `Serial.flush()`
6. 하트비트에 `stackFreeWords`(loop 태스크 스택 최저 잔량) 출력 — 스택 상시 감시.
   100워드 밑으로 떨어지면 큰 지역변수를 의심할 것
7. 연결 로그에 협상된 MTU와 연결 인터벌 출력
8. `USE_FS 1` (LittleFS 활성 — 검증 완료)

웹앱 (`app/app.js`):

9. `sendLine()` write 청크 180B → **20B** (MTU 23 기준)
10. 시뮬레이터에 `SHOWSLOT` 처리 추가 (없어서 `ERR 1`이 떴다)

도구:

11. `firmware/tools/ble_selftest.py` 추가 — 폰 없이 PC에서 프로토콜 왕복 검증

## 실기기 검증 결과 (2026-08-19, 2번 보드 AA:BB:CC:DD:EE:FF)

| 항목 | 결과 |
|------|------|
| 부팅·광고·연결 | OK. `MTU=23 connInterval=60ms` |
| `PING` → `PONG` | OK (수신 즉시 프리즈 **재현 안 됨**) |
| `INFO` 긴 응답(5패킷) | OK, 잘림 없음 |
| `ERR 1` / `ERR 2` 오류코드 | OK |
| `SAVE`/`LIST`/`SHOWSLOT`/`SEL`/`DEL` | OK, 한글 미리보기 정상 |
| **LittleFS 재부팅 영속성** | OK (리셋 후 슬롯·설정 유지) |
| POV 스캔 루프 중 BLE | OK (`SET MODE pov` + `SET SPEED 10` 상태에서 PING 3/3) |
| `TEST on/off/chase/end` | OK |
| `[tx] chunk dropped` 발생 | **0회** |
| `stackFreeWords` 최저값 | **570 / 1024 워드** (2,280B 여유) |
| 하드폴트·예기치 않은 끊김 | 없음 |

빌드: 플래시 218,120B(26%) / 전역 26,632B(11%).

**결론: LittleFS는 무죄였다.** FS 영역·부트로더 충돌 조사는 불필요.

## 기각된 가설들 (다시 파지 말 것)

| 가설 | 기각 근거 |
|------|----------|
| 폰 BT 캐시/주소타입 오염 | BT 토글·주소 변조로도 동일, 새 보드(새 주소)에서도 재현 |
| configPrphBandwidth(BANDWIDTH_MAX) | 제거 후에도 재현 (원인은 스택이었음) |
| 플래시 저장 타이밍 | 저장을 1.5초 지연시켜도 명령 수신 즉시 얼음 |
| BLE 태스크에서 Serial 출력(rx콜백) | 콜백 제거 후에도 재현 |
| LED 전원/브라운아웃 | LED 전부 분리한 보드 단독에서도 재현 |
| Seeed 코어 자체 버그 | 순정 최소 스케치는 정상 동작 |
| 보드 개체 불량 | 결정론적 소프트웨어 버그였음 |
| LittleFS / InternalFS 영역 충돌 | `USE_FS 1`로 실기기 전 기능 검증 통과 — 무죄 확정 |

## 다음 단계

1. **`secrets.h`에 실제 값 채우기.** 지금은 `secrets.example.h` 복사본(플레이스홀더)이라
   `OWNERS` MAC이 `00:00:...`이고, 그래서 부팅 크레딧 대신 `HELLO`가 뜬다.
   부팅 시 시리얼에 찍히는 MAC(예: `AA:BB:CC:DD:EE:FF`)과 이름·시리얼을 넣고 다시 빌드할 것.
2. **실물 LED 조립 검증** — LED 7개를 붙인 상태에서 `TEST on` / `TEST chase`로 채널별 확인,
   그 다음 `SET MODE pov` + 흔들어 잔상 확인. 밝기·컬럼 간격 튜닝.
3. **IMU 스윙 감지 구현** — `isSwinging()`/`swingPeriodUs()`가 아직 스텁(각각 false/20000
   고정)이라 `MODE_AUTO`에서 POV가 절대 안 뜬다. LSM6DS3 연결 + 임계값 튜닝 필요.
4. **배터리 ADC** — `readBattery()`가 100 고정.
5. **버튼** — 디바운스/길게누름 TODO.
6. **웹앱 실기기 연결** — 청크는 이미 20B로 맞춰놨다. Android Chrome / iOS Bluefy로 확인.
7. 안정화 확인 후 디버그 코드 정리 여부 판단(하트비트 `[hb]`/`stackFreeWords`,
   `TEST` 명령은 HW 검사용으로 유지 권장).

## LED 미점등 사례 (2026-08-19) — 원인: 공통 GND 단선

"회사에서는 켜졌는데 집에서 전부 암전" 증상. **펌웨어는 무죄였다.**

진단 순서와 근거:

1. **보드 내장 LED 비콘**(`DEBUG_BEACON`)이 1초 주기로 깜빡임 확인
   → MCU·펌웨어·GPIO 정상. 문제는 외부 경로로 한정
2. 시리얼 `up=` 이 계속 증가 → 브라운아웃/리셋 루프 아님
3. `TEST pin 0` (단일 채널) + `TEST drive low`(2mA)로도 미점등
   → 전류 부족 기각
4. **`PROBE up` / `PROBE down`** 으로 전 채널 측정:
   - D0·D1·D3: 풀업 3291mV / 풀다운 0mV = **완전 개방**
   - D2·D4·D5: 풀다운 2442mV = 확장보드 I2C 풀업(4.7k)일 뿐, LED 아님
   - D13(내장 녹색 LED, 양성 대조군): 풀다운 **647mV** = 다이오드 서명
   → D0~D5에 다이오드 서명이 하나도 없음 = **양쪽 극성 모두 LED 미연결**
5. 전 채널이 동시에 개방 → 개별 배선보다 **공통선** 의심 → **GND 단선 확인**

**교훈**: 전 채널 동시 불량이면 공통 GND를 먼저 본다. `PROBE`가 멀티미터 없이
이 판정을 해주므로, 다음에 같은 증상이 나오면 바로 `PROBE sweep`부터 돌릴 것.

### 딸깍 소리의 정체

LED 채널 D4·D5가 **확장보드의 I2C(SDA/SCL)와 공유**되고 있어서, LED 패턴으로
그 라인을 푸시풀 구동할 때 OLED·RTC가 물린 버스에 쓰레기 신호가 들어간다.
GPIO 전환을 멈추면(`TEST off`) 소리가 멎는 것으로 확인됨. 부저(D3)는 아니었다.

**설계 과제**: LED 채널을 I2C 핀(D4/D5)에서 다른 핀으로 옮겨야 한다. 확장보드가
붙어 있는 한 이 2채널은 풀업 저항과 싸우게 되어 밝기·동작이 불안정하다.

## 알려진 소소한 문제 (선물 전에 정리 검토)

- `FW_VERSION`은 `0.0.2`인데 PROTOCOL.md 본문은 `0.0.1`이라고 적혀 있음 (문서 드리프트)
- `replyf()`의 `char buf[120]` — 주인 이름이 길면 `INFO` 응답이 잘릴 수 있음
- `IMG_MAX_BYTES` 계산이 틀렸음(768, 실제 필요값 6144). 현재 아무도 안 씀
- 앱이 `ERR` 수신 시 `alert()` — 송별회 현장에서 팝업이 뜨면 곤란할 수 있음
- 이미지 전송(`IMGB`/`IMGD`/`IMGE`) 경로는 아직 실기기 검증 안 함 (텍스트만 검증)
- **선물용 최종 빌드 전에 `DEBUG_BEACON`을 0으로** 바꿀 것 (완드에서 내장 적색 LED가
  계속 깜빡이면 보기 싫다). 브링업 중에는 1로 두는 게 유용하다
- `TEST`/`PROBE`는 브링업용이라 선물 후에도 남겨두는 편이 수리에 유리하다
  (PROTOCOL.md §3.5 참고)

## 검증된 사실 (믿어도 됨)

- 보드(XIAO nRF52840 Sense) 2번째 개체, PC USB 연결(COM5), 시리얼 115200
- LED 7채널(D0~D6) GPIO 직결 구동 정상 (대기 물결 패턴 동작 확인)
- 순정 최소 에코 스케치(`firmware/test-bleuart/`)도 정상 — 이제 비교용으로만 의미 있음
- 본 펌웨어의 프리즈는 위 "버그 1"이었고, 수정 후 재현되지 않는다 (위 검증 표 참고)
- `Serial`은 DTR을 올려야 출력된다 (TinyUSB CDC가 `tud_cdc_connected()`를 본다).
  DTR 없이 포트만 열면 아무것도 안 찍히므로 "펌웨어가 죽었다"고 오판하기 쉽다

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
| LittleFS / InternalFS 플래시 영역 충돌 | 코드 리뷰로 스택오버플로 원인 특정 — FS는 무죄 (재검 불필요) |

## 다음 단계 (여기서부터)

1. **수정본 그대로 굽고 `PING` 전송** (현재 코드는 아직 `USE_FS 0`).
   기대: 시리얼에 `RX: PING` → `TX: PONG`, 폰에 `PONG`, [hb] 하트비트 지속.
   - 하트비트의 `stackFreeWords` 값을 기록해둘 것. `handleLine()` 호출 후에도 여유가
     남는지가 이 가설의 최종 확인이다.
2. **통과하면 `USE_FS 1`로 되돌리고 재테스트** (`PING` → `SAVE 1 00FF66 사랑해` → `LIST`).
   FS는 무죄로 판단했으므로 여기서 문제가 없어야 정상. `LIST` 응답이 잘리지 않고
   전부 오는지도 같이 확인 (reply 분할 전송 검증).
3. **그래도 수신 즉시 죽으면** 스택이 여전히 부족한 것이므로, 이분탐색 대신 스택을
   직접 계산할 것:
   - 컴파일된 `.lst`/디스어셈블에서 `handleLine`의 `sub sp, #N` 값을 확인
   - `content`(6KB)·`imgRx`(6KB)는 전역이라 문제 없지만, 지역에 큰 배열을 만드는
     코드가 또 있는지 점검 (`loadSlot`의 `text[128]`, `replyf`의 `buf[120]` 등)
   - 최후 수단: `MAX_COLS`를 128로 줄이거나, 이미지 수신 버퍼를 슬롯 저장 스트리밍으로 대체
4. 안정화 후: 디버그 코드 정리(하트비트 [hb]·stackFreeWords, `TEST` 명령은 HW 검사용으로
   유지 검토), 그 다음 POV 잔상 테스트(`SET MODE pov` + `SET SPEED 10`, 흔들기).
5. 그 다음 미구현 항목: IMU 스윙 감지(`isSwinging`/`swingPeriodUs`가 스텁),
   배터리 ADC(`readBattery`가 100 고정), 버튼 디바운스.

## 환경 세팅

### 이 PC(briana)에는 arduino-cli로 이미 설치되어 있음 (2026-08-19)

- CLI: `C:\Users\briana\tools\arduino-cli\arduino-cli.exe` (v1.5.2, 공식 zip 압축해제)
- 보드 패키지: `%LOCALAPPDATA%\Arduino15` (Arduino IDE를 나중에 깔면 그대로 인식됨)
- `Seeeduino:nrf52@1.1.13` + arm-none-eabi-gcc 9-2019q4 + CMSIS 5.7.0
- Arduino IDE(GUI)는 설치 안 됨. CLI만으로 빌드·DFU 가능

```
# 빌드
~/tools/arduino-cli/arduino-cli.exe compile -b Seeeduino:nrf52:xiaonRF52840Sense \
  firmware/pov-wand --output-dir firmware/pov-wand/build
# 보드 확인
~/tools/arduino-cli/arduino-cli.exe board list
```

**주의**: 현재 `secrets.h`는 `secrets.example.h`를 그대로 복사한 **플레이스홀더**다
(`OWNERS` MAC이 `00:00:...`). 선물용 실물을 구울 때는 부팅 시 시리얼에 찍히는 실제 MAC과
이름·시리얼을 채운 뒤 다시 빌드할 것. 지금 `build/`에 있는 zip은 테스트 빌드다.

### 처음부터 세팅할 때 (다른 PC)

1. arduino-cli 다운로드: `https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip`
   (GitHub releases의 `arduino-cli_latest_*` 경로는 404 — 위 주소를 쓸 것)
2. `arduino-cli config init` → `config add board_manager.additional_urls`
   `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`
3. `arduino-cli core update-index` → `arduino-cli core install Seeeduino:nrf52@1.1.13`
   (**mbed 아닌 것**. arm-gcc 146MB + CMSIS 99MB 포함, 총 약 270MB)
4. `firmware/pov-wand/secrets.example.h`를 `secrets.h`로 복사 (gitignore된 파일)
5. FQBN: `Seeeduino:nrf52:xiaonRF52840Sense`
6. 라이브러리: Adafruit NeoPixel (LED_TYPE 2용, 지금은 불필요)

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

## 테스트 절차

### A. PC에서 (폰 없이, 권장)

```
python -m venv .venv
.venv/Scripts/pip install bleak
python firmware/tools/ble_selftest.py          # 기본 왕복 검사
python firmware/tools/ble_selftest.py fs       # 슬롯 저장/조회/삭제
python firmware/tools/ble_selftest.py persist  # 리셋 후 실행 → 영속성
python firmware/tools/ble_selftest.py pov      # POV 스캔 중 BLE 생존
python firmware/tools/ble_selftest.py clean    # 테스트 슬롯 정리
```

시리얼을 같이 보려면 별도 터미널에서 COM5를 115200으로 열고 **DTR을 올릴 것**.
단, 플래시 직전에는 시리얼을 닫아야 한다 — 포트를 잡고 있으면 arduino-cli의
1200-baud 리셋이 실패하고 `Target is not in DFU mode`가 난다.

### B. nRF Connect 앱에서

1. 스캔 → `POV-STICK` 연결 (최소 스케치는 `POV-TEST`)
2. Nordic UART Service → TX(…6E400003…) ↓↓↓ 눌러 notify 구독
3. RX(…6E400002…)에 TEXT로 `PING` 전송 (줄바꿈 불필요 — 150ms 유휴 시 줄 완성 처리 있음)
4. 정상 기준: 시리얼에 `RX: PING` + `TX: PONG`, 폰에 `PONG`, [hb] 하트비트 지속,
   LED 물결 지속

## 참고

- 프로토콜: PROTOCOL.md (웹앱·펌웨어의 기준 문서)
- 웹앱: app/ (GitHub Pages 배포: /present/), 시뮬레이터 모드 내장
- 웹앱 실기기 연결 검증은 펌웨어 안정화 후 진행. 앱의 write 청크는 MTU 23 기준
  20B로 이미 줄여놨다 (app.js `sendLine`)

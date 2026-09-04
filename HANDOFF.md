# HANDOFF — 프로젝트 상태 (2026-08-27 갱신)

다른 PC에서 이어서 작업하기 위한 상태 스냅샷.

## 현재 상태 (2026-08-27)

**핵심 기능 전부 동작**: BLE(LESC 페어링)·잔상 출력(실측 최적 2~8ms/컬럼)·한글+자모+기호
폰트·슬롯 저장·OLED 공존·앱(v27, iPhone Bluefy 실사용 검증). 보드에는 최신 빌드
플래시됨(스윙 반동기화 + 문장 사이 빈 틈 + HW 탭엔진 + `BOOTDFU` 명령).

**Bluefy 사용법 확정**: 주소창 제거는 Bluefy 자체 메뉴의 **Full-Screen Mode**
(웹 전체화면 API는 Bluefy에 없음 — 실기기 채증). 풀스크린 진입은 페이지를 재로드하므로
앱이 로드 시 자동 재연결(getDevices)을 시도한다(v26, Bluefy의 getDevices 지원 여부 검증 중).

**미해결/검증 대기 (우선순위순)**:
1. **좌우반전** — 방향감지 유효성 의심. 사용자 A/B/C 판정 대기
   (A=한쪽 방향만 항상 거울=IMU 방향감지 무효→재설계 / B=양쪽 랜덤 / C=SET FLIP으로 해결)
2. **톡톡(더블탭) 슬롯 전환** — HW 탭엔진 구현·플래시됨, [tap] 디버그 로그 결과 미보고
3. **BOOTDFU 명령** — 플래시됐지만 실전 미검증 (검증되면 케이스 조립 후에도 무선 트리거 굽기 가능)
4. 풀스크린 상태에서 자동 재연결(getDevices) 실기기 검증
5. 선물 최종화: DEBUG_BEACON 0, 실명 SETOWNER, 송별 슬롯 채우기, 안내서에 풀스크린 사용법 추가
6. 백로그: 지선 보드 합체 검증, 박수 폭죽·이스터에그·손글씨 롤링페이퍼, 배터리 확정 시 100mA 충전

## 집 PC 빠른 시작 (환경 셋팅 전체)

설치할 것: **git, Python 3, arduino-cli** 세 개. 컴파일러(arm-none-eabi-gcc)는 따로 깔지
않는다 — 보드 패키지에 동봉되어 arduino-cli가 자동 사용한다. Arduino IDE도 불필요
(이미 깔려 있다면 arduino-cli가 내장되어 있음: `resources\app\lib\backend\resources\arduino-cli.exe`).

```
# 0) 소스
git clone https://github.com/mkseo90/present.git pov-wand
cd pov-wand
pip install pyserial bleak            # flash.py / ble_selftest.py 용

# 1) arduino-cli (공식 zip 풀어서 PATH에 두면 끝. GitHub releases의 latest 경로는 404 주의)
#    https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip

# 2) Seeed 보드 패키지 (arm-gcc 9-2019q4 + CMSIS + adafruit-nrfutil 포함, 약 270MB)
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
arduino-cli core update-index
arduino-cli core install Seeeduino:nrf52@1.1.13     # mbed 버전 아님! 반드시 1.1.13

# 3) 라이브러리 (내장 IMU용 딱 하나. 이 PC 설치본은 2.0.7)
arduino-cli lib install "Seeed Arduino LSM6DS3"

# 4) 빌드 → 굽기 (USB-C만 꽂으면 flash.py가 알아서: 터치→부트로더→DFU)
arduino-cli compile -e --fqbn Seeeduino:nrf52:xiaonRF52840Sense firmware/pov-wand
python firmware/tools/flash.py
```

- `secrets.h` 불필요 (`__has_include` — 없어도 컴파일)
- 그 외 외부 라이브러리 불필요: BLE(bluefruit)·LittleFS는 보드 패키지 내장,
  폰트·SSD1306 OLED 드라이버는 소스에 직접 구현. Adafruit DotStar/NeoPixel은
  LED_TYPE 1/2(RGB 스트립)에서만 필요 — 현재 선물용은 LED_TYPE 3(단색 GPIO 직결)
- **repo에 없는 것** (takehome zip에만 있음): `docs/`(사용안내서 docx/pdf, QR),
  `firmware/pov-wand/secrets.h`(없어도 컴파일됨), `firmware/pov-wand/build/`(재빌드하면 됨)
- 웹앱 수정 → 배포 절차: `python firmware/tools/stamp_app.py` (빌드시각+캐시버스터 자동)
  → bash로 커밋(한글 커밋메시지는 PowerShell이 깨뜨림) → push → main에 ff-merge → push.
  GitHub Pages 캐시 10분 — 폰에서 설정 탭 빌드시각으로 반영 확인
- 폰트 재생성은 `firmware/tools/` 디렉토리 **안에서** `python gen_font.py` (ttf 상대경로)
  → `python gen_font_js.py`

---

이하는 과거 브링업 기록(2026-08-19) — 재조사 금지 목록과 검증 근거로 유지.

## (기록) 브링업 당시 요약

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

1. **주인 정보 주입.** 소스가 아니라 기기 플래시에 넣는다 (`secrets.h` 불필요):

   ```
   python firmware/tools/provision.py "실제이름" "No.001/001" 00FF66
   ```

   조회 `--show`, 삭제 `--clear`, 켜져 있는 완드 목록 `--list`.
   재부팅·재플래시 후에도 유지된다.

   **주인 이름은 BLE 광고 이름에도 쓰인다** — `<이름>의 LED`. 완드를 여러 개 켰을 때
   구분하기 위한 것이며, 광고 데이터는 부팅 시 구성되므로 **재부팅 후 적용**된다.
   현재 이 보드에는 `민경`이 주입되어 `민경의 LED`로 광고 중이다.
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

## (기록) 당시 다음 단계

(전부 완료됨 — 프리즈 재현 없음, USE_FS 1 검증 통과, POV·IMU·배터리 ADC 구현 완료.
현재 할 일은 문서 맨 위 "미해결/검증 대기" 참고)

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

**주인 정보는 빌드와 무관하다** — 기기 플래시에 주입하므로 같은 바이너리를 모든 보드에
구운 뒤 보드별로 `provision.py`만 돌리면 된다. 실명이 소스·바이너리에 들어가지 않는다.

### 처음부터 세팅할 때 (다른 PC)

1. arduino-cli 다운로드: `https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip`
   (GitHub releases의 `arduino-cli_latest_*` 경로는 404 — 위 주소를 쓸 것)
2. `arduino-cli config init` → `config add board_manager.additional_urls`
   `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`
3. `arduino-cli core update-index` → `arduino-cli core install Seeeduino:nrf52@1.1.13`
   (**mbed 아닌 것**. arm-gcc 146MB + CMSIS 99MB 포함, 총 약 270MB)
4. `secrets.h`는 **필요 없다** — 없어도 컴파일된다(`__has_include`). 주인 정보는
   `provision.py`로 기기에 주입한다. (과거 방식으로 쓰려면 `secrets.example.h`를
   `secrets.h`로 복사해 채우면 되고, 기기 주입값이 있으면 그쪽이 우선한다)
5. FQBN: `Seeeduino:nrf52:xiaonRF52840Sense`
6. 라이브러리: Adafruit NeoPixel (LED_TYPE 2용, 지금은 불필요)

## 펌웨어 굽는 절차 (표준: 감시 플래셔, 리셋 버튼 불필요)

```
# 1) 빌드
arduino-cli compile -e --fqbn Seeeduino:nrf52:xiaonRF52840Sense firmware/pov-wand
# 2) 감시 플래셔 실행 → USB-C만 꽂으면 자동으로 (터치 → 부트로더 → 플래시)
python firmware/tools/flash.py
```

flash.py가 하는 일: XIAO 앱 포트(VID 0x2886)가 보이면 **1200bps 터치**로 부트로더 재부팅,
부트로더 포트가 보이면 즉시 DFU. 리셋 2번을 미리 눌러둬도(부트로더 상태) 그대로 잡는다.
adafruit-nrfutil은 보드 패키지 안에서 자동 탐색. 의존성: `pip install pyserial`.

- 케이스 조립 후: 앱 관리자 모드 → 원시 명령 `BOOTDFU` → 보드가 스스로 부트로더 진입
  (펌웨어 v0.0.2+, 페어링된 연결에서만. 이후 flash.py가 이어받음)
- 완드가 USB에 안 잡히면: 충전전용 케이블(데이터선 없음)부터 의심할 것.
  FTDI 등 다른 USB-시리얼과 혼동 금지 — XIAO는 VID 0x2886(Seeed)
- (구방식) 수동 더블탭 + `arduino-cli board list`로 새 포트 확인 후
  `adafruit-nrfutil dfu serial -pkg <build>/pov-wand.ino.zip -p COM<N> -b 115200`
- arduino-cli는 Arduino IDE에 내장 (resources/app/lib/backend/resources/)
- nrfutil 자체의 --touch 1200 옵션은 신뢰 불가(부트로더가 새 COM번호로 뜸) — flash.py가 대체

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

1. 스캔 → **`<주인이름>의 LED`** 연결 (주인 미주입 보드는 `POV-STICK`,
   최소 스케치는 `POV-TEST`). 광고 이름은 주인마다 다르므로 이름을 외우지 말고
   Nordic UART Service를 광고하는 기기를 고르면 된다
2. Nordic UART Service → TX(…6E400003…) ↓↓↓ 눌러 notify 구독
3. RX(…6E400002…)에 TEXT로 `PING` 전송 (줄바꿈 불필요 — 150ms 유휴 시 줄 완성 처리 있음)
4. 정상 기준: 시리얼에 `RX: PING` + `TX: PONG`, 폰에 `PONG`, [hb] 하트비트 지속,
   LED 물결 지속

## 참고

- 프로토콜: PROTOCOL.md (웹앱·펌웨어의 기준 문서)
- 웹앱: app/ (GitHub Pages 배포: /present/), 시뮬레이터 모드 내장
- 웹앱 실기기 연결 검증은 펌웨어 안정화 후 진행. 앱의 write 청크는 MTU 23 기준
  20B로 이미 줄여놨다 (app.js `sendLine`)

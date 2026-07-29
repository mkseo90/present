# 선물 (POV Wand) 🪄

손으로 흔들면 공중에 글자와 그림이 나타나는 잔상(POV) LED 완드.
퇴사하는 동료를 위한 선물로 만들었습니다.

## 구성

| 폴더 | 내용 |
|------|------|
| [`app/`](app/) | 제어용 웹앱 (Web Bluetooth) — [바로 열기](https://USERNAME.github.io/pov-wand/app/) |
| [`firmware/pov-wand/`](firmware/pov-wand/) | Seeed XIAO nRF52840 Sense 펌웨어 (Arduino) |
| [`firmware/tools/`](firmware/tools/) | 폰트 헤더 생성 스크립트 |
| [`PROTOCOL.md`](PROTOCOL.md) | 앱 ↔ 기기 BLE 통신 프로토콜 |

## 사용법 (앱)

- **Android**: Chrome에서 웹앱 열기 → 연결
- **iPhone**: App Store에서 [Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055) 설치 → Bluefy로 웹앱 열기
- 기기가 없어도 설정 탭의 **시뮬레이터 모드**로 체험 가능

## 하드웨어

- MCU: Seeed XIAO nRF52840 Sense (BLE·IMU 내장)
- LED: 어드레서블 RGB 8개 (세로 1열)
- 만든이: 앱·펌웨어 민경 / 하드웨어 지선

## 폰트

한글/영문 도트 폰트로 [Galmuri](https://github.com/quiple/galmuri) (Galmuri7)를
사용했습니다 — SIL Open Font License 1.1.

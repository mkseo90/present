// 선물 (POV Wand) 펌웨어 v0.1 스켈레톤
// 보드: Seeed XIAO nRF52840 Sense (Seeed nRF52 Boards - Adafruit 코어 기반)
// 프로토콜: E:\Study\pov-wand\PROTOCOL.md v0.1
//
// 필요 라이브러리:
//   - Adafruit Bluefruit (보드 패키지에 포함)
//   - Adafruit DotStar (APA102/SK9822 사용 시)
//   - Seeed Arduino LSM6DS3 (내장 IMU)

#include <bluefruit.h>
// FS 격리 실험: 0이면 LittleFS(내장 플래시 저장)를 완전히 비활성화
#define USE_FS 0
#if USE_FS
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#endif
#include "font8x8.h"
#include "font8x8_kr.h"

#if USE_FS
using namespace Adafruit_LittleFS_Namespace;
#endif

// ---------------- HW 설정 (회로 확정 후 조정) ----------------
// LED 타입: 0=없음(시리얼 디버그), 1=APA102/SK9822(SPI 2선), 2=WS2812B(단선),
//           3=단색 LED GPIO 직결 (최종 HW: GREEN LED 8개)
#define LED_TYPE 3
#define NUM_LEDS 7            // HW 확정: GREEN LED 7개, D0~D6 (2026-08-19)
#define PIN_BUTTON 10         // D10: 택트 버튼 (회로 확정 후 변경)

#if LED_TYPE == 1
#include <Adafruit_DotStar.h>
// XIAO 하드웨어 SPI: MOSI=D10, SCK=D8
Adafruit_DotStar strip(NUM_LEDS, DOTSTAR_BGR);
#elif LED_TYPE == 2
#define PIN_WS2812 0          // D0: WS2812 데이터
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel strip(NUM_LEDS, PIN_WS2812, NEO_GRB + NEO_KHZ800);
#elif LED_TYPE == 3
// 위(끝)부터 아래 순서. 폰트는 8px 테이블의 bit0~6 사용 (Galmuri7이 7px 디자인이라
// 글자 본체가 상단 7줄에 들어감 — 영문 디센더 1px만 잘림)
static const uint8_t LED_PINS[NUM_LEDS] = { 0, 1, 2, 3, 4, 5, 6 };
#endif

// ---------------- 상수 ----------------
// 버전 규칙: 1.0.0 미만 = 단색(GREEN) 완드, 1.0.0 이상 = RGB 완드 — 앱이 이걸로 UI를 전환
#define FW_VERSION "0.0.2"
#define MAX_SLOTS 12
#define MAX_COLS 256          // 콘텐츠 최대 컬럼(글자 32자 분량)
#define IMG_MAX_BYTES (MAX_COLS * 3 * 8 / 8)  // 사용 안 함(자리표시)

// ---------------- BLE ----------------
BLEUart bleuart;
// OTA(DFU)는 상시 서비스로 열지 않는다. "DFU <PIN>" 명령이 맞을 때만
// 부트로더로 재부팅해 그 순간에만 무선 업데이트를 허용 (무단 리플래시 방지)

// ---------------- 주인 지정 (MAC → 이름) ----------------
// 실명·PIN 등 비공개 값은 secrets.h에 (공개 repo 제외, secrets.example.h 참고)
#include "secrets.h"
char myMac[18] = "?";
const Owner* owner = nullptr;

void findOwner() {
  uint8_t m[6];
  Bluefruit.getAddr(m);
  snprintf(myMac, sizeof(myMac), "%02X:%02X:%02X:%02X:%02X:%02X",
           m[5], m[4], m[3], m[2], m[1], m[0]);
  Serial.print("MAC: "); Serial.println(myMac);
  for (uint8_t i = 0; i < sizeof(OWNERS) / sizeof(OWNERS[0]); i++) {
    if (strcasecmp(OWNERS[i].mac, myMac) == 0) { owner = &OWNERS[i]; return; }
  }
}

// ---------------- 상태 ----------------
enum Mode { MODE_AUTO, MODE_POV, MODE_IDLE };
struct Config {
  uint8_t bright = 80;        // %
  uint8_t mode = MODE_AUTO;
  uint16_t speedUs = 0;       // 0 = IMU 자동
  uint8_t startSlot = 1;
} cfg;

// 표시 버퍼: 컬럼당 8픽셀 × RGB
struct Content {
  uint16_t cols = 0;
  uint8_t px[MAX_COLS][8][3]; // [컬럼][행][RGB]
} content;

uint8_t currentSlot = 0;
int8_t testHold = -1;  // HW 검사용: -1=해제, 0=전체소등, 1=전체점등, 2=순차점등(체이스)
uint32_t cfgDirtyAt = 0;  // 설정 변경 시각 — 플래시 저장은 1.5초 뒤로 미룸 (통신 중 즉시 쓰기 회피)

// 이미지 수신 상태 (IMGB/IMGD/IMGE)
struct ImgRx {
  bool active = false;
  uint8_t slot = 0;
  uint16_t cols = 0;
  uint16_t received = 0;
  uint16_t expectSeq = 0;
  uint8_t buf[MAX_COLS * 24];
} imgRx;

// ---------------- 유틸 ----------------
void reply(const char* s) {
  Serial.print("[reply>] "); Serial.println(s);   // 어디서 어는지 추적용
  bleuart.print(s);
  Serial.println("[reply: body sent]");
  bleuart.print("\n");
  Serial.println("[reply: done]");
}
void replyf(const char* fmt, ...) {
  char buf[120];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  reply(buf);
}

// ---------------- 색 파싱 ----------------
struct ColorSpec {
  enum { SOLID, RAINBOW, GRAD } type = SOLID;
  uint8_t a[3] = {255, 255, 255};
  uint8_t b[3] = {255, 255, 255};
};

// 아두이노 자동 프로토타입이 ColorSpec 정의보다 앞에 삽입되는 문제 방지용 명시 선언
bool parseColor(const char* s, ColorSpec& c);
void colorAt(const ColorSpec& c, uint16_t x, uint16_t w, uint8_t out[3]);
void renderText(const char* text, const ColorSpec& cs);

bool hex2rgb(const char* h, uint8_t out[3]) {
  if (strlen(h) < 6) return false;
  for (int i = 0; i < 3; i++) {
    char t[3] = { h[i*2], h[i*2+1], 0 };
    out[i] = strtoul(t, nullptr, 16);
  }
  return true;
}

bool parseColor(const char* s, ColorSpec& c) {
  if (strcmp(s, "rainbow") == 0) { c.type = ColorSpec::RAINBOW; return true; }
  if (strncmp(s, "grad:", 5) == 0) {
    c.type = ColorSpec::GRAD;
    char tmp[16]; strncpy(tmp, s + 5, 15); tmp[15] = 0;
    char* dash = strchr(tmp, '-');
    if (!dash) return false;
    *dash = 0;
    return hex2rgb(tmp, c.a) && hex2rgb(dash + 1, c.b);
  }
  c.type = ColorSpec::SOLID;
  return hex2rgb(s, c.a);
}

void colorAt(const ColorSpec& c, uint16_t x, uint16_t w, uint8_t out[3]) {
  switch (c.type) {
    case ColorSpec::RAINBOW: {
      // 간단 HSV→RGB (h: 0~299도 근사)
      uint16_t h = (uint32_t)x * 300 / (w ? w : 1);
      uint8_t reg = h / 60, rem = (h % 60) * 255 / 60;
      uint8_t p = 0, q = 255 - rem, t = rem;
      switch (reg) {
        case 0: out[0]=255; out[1]=t;   out[2]=p;   break;
        case 1: out[0]=q;   out[1]=255; out[2]=p;   break;
        case 2: out[0]=p;   out[1]=255; out[2]=t;   break;
        case 3: out[0]=p;   out[1]=q;   out[2]=255; break;
        default: out[0]=t;  out[1]=p;   out[2]=255; break;
      }
      break;
    }
    case ColorSpec::GRAD:
      for (int i = 0; i < 3; i++)
        out[i] = c.a[i] + (int32_t)(c.b[i] - c.a[i]) * x / (w > 1 ? w - 1 : 1);
      break;
    default:
      memcpy(out, c.a, 3);
  }
}

// ---------------- 텍스트 → 컬럼 렌더 ----------------
// UTF-8 디코드: 다음 코드포인트 반환, *pp 전진
uint32_t nextCp(const char** pp) {
  const uint8_t* p = (const uint8_t*)*pp;
  uint32_t cp = *p;
  int extra = 0;
  if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
  else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
  else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
  while (extra-- && (p[1] & 0xC0) == 0x80) {
    p++;
    cp = cp << 6 | (*p & 0x3F);
  }
  *pp = (const char*)(p + 1);
  return cp;
}

// 코드포인트 → 8컬럼 글리프. 폰트 없는 글자는 ▯
void glyphFor(uint32_t cp, uint8_t out[8]) {
  if (cp >= 0x20 && cp < 0x7F) { memcpy(out, FONT8[cp - 0x20], 8); return; }
  if (cp >= 0xAC00 && cp <= 0xD7A3) { memcpy(out, FONT8_KR[cp - 0xAC00], 8); return; }
  memset(out, 0x81, 8); out[0] = out[7] = 0xFF;  // ▯
}

void renderText(const char* text, const ColorSpec& cs) {
  content.cols = 0;
  // 1차: 전체 폭 계산 (글자 8컬럼 + 간격 1)
  uint16_t total = 0;
  for (const char* p = text; *p && total < MAX_COLS; ) {
    nextCp(&p);
    total += 9;
  }
  uint16_t x = 0;
  for (const char* p = text; *p && content.cols < MAX_COLS - 9; ) {
    uint8_t glyph[8];
    uint32_t cp = nextCp(&p);
    if (cp < 0x20) continue;
    glyphFor(cp, glyph);
    for (int gx = 0; gx < 8; gx++) {
      uint8_t rgb[3];
      colorAt(cs, x, total, rgb);
      for (int y = 0; y < 8; y++) {
        bool on = glyph[gx] >> y & 1;
        content.px[content.cols][y][0] = on ? rgb[0] : 0;
        content.px[content.cols][y][1] = on ? rgb[1] : 0;
        content.px[content.cols][y][2] = on ? rgb[2] : 0;
      }
      content.cols++; x++;
    }
    memset(content.px[content.cols], 0, 24); // 글자 간격 1컬럼
    content.cols++; x++;
  }
}

// ---------------- 슬롯 저장 (LittleFS) ----------------
#if !USE_FS
// FS 격리 실험용 스텁
bool saveSlotText(uint8_t, const char*, const char*) { return false; }
bool saveSlotImage(uint8_t, const uint8_t*, uint16_t) { return false; }
bool loadSlot(uint8_t) { return false; }
void listSlots() { reply("OK LIST"); }
uint8_t usedSlots() { return 0; }
void saveConfig() {}
void loadConfig() {}
#else
// 파일 /s<N>: [type(1B: 'T'/'I')][colorspec\n(TXT)] payload
void slotPath(char* out, uint8_t n) { sprintf(out, "/s%d", n); }

bool saveSlotText(uint8_t n, const char* color, const char* text) {
  char path[8]; slotPath(path, n);
  InternalFS.remove(path);
  File f(InternalFS);
  if (!f.open(path, FILE_O_WRITE)) return false;
  f.write('T');
  f.write((const uint8_t*)color, strlen(color)); f.write('\n');
  f.write((const uint8_t*)text, strlen(text));
  f.close();
  return true;
}

bool saveSlotImage(uint8_t n, const uint8_t* data, uint16_t cols) {
  char path[8]; slotPath(path, n);
  InternalFS.remove(path);
  File f(InternalFS);
  if (!f.open(path, FILE_O_WRITE)) return false;
  f.write('I');
  f.write((uint8_t)(cols & 0xFF)); f.write((uint8_t)(cols >> 8));
  f.write(data, cols * 24);
  f.close();
  return true;
}

bool loadSlot(uint8_t n) {
  char path[8]; slotPath(path, n);
  File f(InternalFS);
  if (!f.open(path, FILE_O_READ)) return false;
  int type = f.read();
  if (type == 'T') {
    char color[24] = {0}, text[128] = {0};
    int ci = 0, c;
    while ((c = f.read()) >= 0 && c != '\n' && ci < 23) color[ci++] = c;
    int ti = f.read((uint8_t*)text, 127);
    (void)ti;
    ColorSpec cs;
    parseColor(color, cs);
    renderText(text, cs);
  } else if (type == 'I') {
    uint16_t cols = f.read() | (f.read() << 8);
    if (cols > MAX_COLS) cols = MAX_COLS;
    content.cols = cols;
    f.read((uint8_t*)content.px, cols * 24);
  } else { f.close(); return false; }
  f.close();
  currentSlot = n;
  return true;
}

void listSlots() {
  for (uint8_t n = 1; n <= MAX_SLOTS; n++) {
    char path[8]; slotPath(path, n);
    File f(InternalFS);
    if (!f.open(path, FILE_O_READ)) continue;
    int type = f.read();
    if (type == 'T') {
      char color[24] = {0}, text[64] = {0};
      int ci = 0, c;
      while ((c = f.read()) >= 0 && c != '\n' && ci < 23) color[ci++] = c;
      f.read((uint8_t*)text, 40);
      replyf("SLOT %d TXT %s", n, text);
    } else if (type == 'I') {
      uint16_t cols = f.read() | (f.read() << 8);
      replyf("SLOT %d IMG %dcol", n, cols);
    }
    f.close();
  }
  reply("OK LIST");
}

uint8_t usedSlots() {
  uint8_t used = 0;
  for (uint8_t n = 1; n <= MAX_SLOTS; n++) {
    char path[8]; slotPath(path, n);
    File f(InternalFS);
    if (f.open(path, FILE_O_READ)) { used++; f.close(); }
  }
  return used;
}

void saveConfig() {
  InternalFS.remove("/cfg");
  File f(InternalFS);
  if (f.open("/cfg", FILE_O_WRITE)) { f.write((uint8_t*)&cfg, sizeof(cfg)); f.close(); }
}
void loadConfig() {
  File f(InternalFS);
  if (f.open("/cfg", FILE_O_READ)) { f.read((uint8_t*)&cfg, sizeof(cfg)); f.close(); }
}
#endif  // USE_FS

// ---------------- base64 디코드 ----------------
int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
int b64decode(const char* in, uint8_t* out, int maxOut) {
  int n = 0; uint32_t acc = 0; int bits = 0;
  for (const char* p = in; *p && *p != '='; p++) {
    int v = b64val(*p);
    if (v < 0) continue;
    acc = acc << 6 | v; bits += 6;
    if (bits >= 8) { bits -= 8; if (n < maxOut) out[n++] = acc >> bits & 0xFF; }
  }
  return n;
}

// CRC32 (표준, 테이블 없이)
uint32_t crc32(const uint8_t* d, uint32_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= d[i];
    for (int k = 0; k < 8; k++) crc = crc >> 1 ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

// ---------------- 프로토콜 파서 ----------------
void handleLine(char* line) {
  Serial.print("RX: "); Serial.println(line);
  char* cmd = strtok(line, " ");
  if (!cmd) return;

  if (strcmp(cmd, "PING") == 0) { reply("PONG"); return; }

  if (strcmp(cmd, "INFO") == 0) {
    const char* m = cfg.mode == MODE_AUTO ? "auto" : cfg.mode == MODE_POV ? "pov" : "idle";
    replyf("INFO fw=%s slots=%d used=%d bat=%d mode=%s bright=%d owner=%s serial=%s mac=%s",
           FW_VERSION, MAX_SLOTS, usedSlots(), readBattery(), m, cfg.bright,
           owner ? owner->name : "-", owner ? owner->serial : "-", myMac);
    return;
  }

  if (strcmp(cmd, "SHOW") == 0) {
    char* color = strtok(nullptr, " ");
    char* text = strtok(nullptr, "");
    if (!color || !text) { reply("ERR 1 usage SHOW <color> <text>"); return; }
    ColorSpec cs;
    if (!parseColor(color, cs)) { reply("ERR 2 bad color"); return; }
    renderText(text, cs);
    currentSlot = 0;
    reply("OK SHOW");
    return;
  }

  if (strcmp(cmd, "SHOWSLOT") == 0) {
    uint8_t n = atoi(strtok(nullptr, " ") ?: "0");
    if (n < 1 || n > MAX_SLOTS) { reply("ERR 2 slot out of range"); return; }
    if (!loadSlot(n)) { reply("ERR 2 empty slot"); return; }
    reply("OK SHOWSLOT");
    return;
  }

  if (strcmp(cmd, "SAVE") == 0) {
    char* ns = strtok(nullptr, " ");
    char* color = strtok(nullptr, " ");
    char* text = strtok(nullptr, "");
    uint8_t n = ns ? atoi(ns) : 0;
    if (n < 1 || n > MAX_SLOTS) { reply("ERR 2 slot out of range"); return; }
    if (!color || !text) { reply("ERR 1 usage SAVE <slot> <color> <text>"); return; }
    ColorSpec cs;
    if (!parseColor(color, cs)) { reply("ERR 2 bad color"); return; }
    if (!saveSlotText(n, color, text)) { reply("ERR 3 storage full"); return; }
    reply("OK SAVE");
    return;
  }

  if (strcmp(cmd, "IMGB") == 0) {
    uint8_t n = atoi(strtok(nullptr, " ") ?: "0");
    uint16_t cols = atoi(strtok(nullptr, " ") ?: "0");
    if (n < 1 || n > MAX_SLOTS) { reply("ERR 2 slot out of range"); return; }
    if (cols < 1 || cols > MAX_COLS) { reply("ERR 2 bad cols"); return; }
    imgRx = {};
    imgRx.active = true; imgRx.slot = n; imgRx.cols = cols;
    reply("OK IMGB");
    return;
  }

  if (strcmp(cmd, "IMGD") == 0) {
    if (!imgRx.active) { reply("ERR 5 no IMGB"); return; }
    uint16_t seq = atoi(strtok(nullptr, " ") ?: "0");
    char* data = strtok(nullptr, " ");
    if (seq != imgRx.expectSeq) { imgRx.active = false; reply("ERR 5 seq mismatch"); return; }
    if (data) imgRx.received += b64decode(data, imgRx.buf + imgRx.received,
                                          sizeof(imgRx.buf) - imgRx.received);
    imgRx.expectSeq++;
    return; // 청크에는 무응답 (프로토콜 §6)
  }

  if (strcmp(cmd, "IMGE") == 0) {
    if (!imgRx.active) { reply("ERR 5 no IMGB"); return; }
    imgRx.active = false;
    uint32_t want = strtoul(strtok(nullptr, " ") ?: "0", nullptr, 16);
    uint32_t got = crc32(imgRx.buf, imgRx.cols * 24);
    if (want != got) { replyf("ERR 4 crc got=%08lX", got); return; }
    if (!saveSlotImage(imgRx.slot, imgRx.buf, imgRx.cols)) { reply("ERR 3 storage full"); return; }
    reply("OK IMGE");
    return;
  }

  if (strcmp(cmd, "SEL") == 0) {
    uint8_t n = atoi(strtok(nullptr, " ") ?: "0");
    if (n < 1 || n > MAX_SLOTS) { reply("ERR 2 slot out of range"); return; }
    cfg.startSlot = n; cfgDirtyAt = millis();
    loadSlot(n);
    reply("OK SEL");
    return;
  }

  if (strcmp(cmd, "DEL") == 0) {
    uint8_t n = atoi(strtok(nullptr, " ") ?: "0");
    if (n < 1 || n > MAX_SLOTS) { reply("ERR 2 slot out of range"); return; }
#if USE_FS
    char path[8]; slotPath(path, n);
    InternalFS.remove(path);
#endif
    reply("OK DEL");
    return;
  }

  if (strcmp(cmd, "LIST") == 0) { listSlots(); return; }

  // OTA(DFU) 명령은 보류 — 실기기 검증 후 재도입 예정 (기록: git 이력 참고)

  // HW 브링업용 LED 테스트: TEST on(전체점등) / off(전체소등) / chase(순차) / end(해제)
  if (strcmp(cmd, "TEST") == 0) {
    char* m = strtok(nullptr, " ");
    if (!m) { reply("ERR 1 usage TEST on|off|chase|end"); return; }
    if (strcmp(m, "on") == 0) testHold = 1;
    else if (strcmp(m, "off") == 0) testHold = 0;
    else if (strcmp(m, "chase") == 0) testHold = 2;
    else if (strcmp(m, "end") == 0) testHold = -1;
    else { reply("ERR 2 bad test mode"); return; }
    reply("OK TEST");
    return;
  }

  if (strcmp(cmd, "SET") == 0) {
    char* what = strtok(nullptr, " ");
    char* val = strtok(nullptr, " ");
    if (!what || !val) { reply("ERR 1 usage SET <key> <val>"); return; }
    if (strcmp(what, "BRIGHT") == 0) {
      cfg.bright = constrain(atoi(val), 0, 100);
    } else if (strcmp(what, "MODE") == 0) {
      if (strcmp(val, "auto") == 0) cfg.mode = MODE_AUTO;
      else if (strcmp(val, "pov") == 0) cfg.mode = MODE_POV;
      else if (strcmp(val, "idle") == 0) cfg.mode = MODE_IDLE;
      else { reply("ERR 2 bad mode"); return; }
    } else if (strcmp(what, "SPEED") == 0) {
      cfg.speedUs = atoi(val) * 1000;
    } else { reply("ERR 1 unknown key"); return; }
    cfgDirtyAt = millis();
    reply("OK SET");
    return;
  }

  reply("ERR 1 unknown command");
}

// BLE 수신 → 라인 조립
char rxLine[300];
uint16_t rxLen = 0;
uint32_t rxLastMs = 0;
void pollBle() {
  while (bleuart.available()) {
    char c = bleuart.read();
    rxLastMs = millis();
    if (c == '\n' || rxLen >= sizeof(rxLine) - 1) {
      rxLine[rxLen] = 0;
      if (rxLen) handleLine(rxLine);
      rxLen = 0;
    } else if (c != '\r') {
      rxLine[rxLen++] = c;
    }
  }
  // 줄바꿈 없이 온 데이터도 150ms 조용하면 한 줄로 처리 (터미널 앱 편의)
  if (rxLen && millis() - rxLastMs > 150) {
    rxLine[rxLen] = 0;
    handleLine(rxLine);
    rxLen = 0;
  }
}

// ---------------- 배터리 (HW 확정 후 구현) ----------------
int readBattery() { return 100; } // TODO: ADC 분압 회로 연결 시 구현

// ---------------- IMU 스윙 감지 (스켈레톤) ----------------
// TODO: LSM6DS3 라이브러리 연결. 보드 도착 후 임계값 튜닝
// #include "LSM6DS3.h"
// LSM6DS3 imu(I2C_MODE, 0x6A);
bool isSwinging() { return false; }       // TODO
uint32_t swingPeriodUs() { return 20000; } // TODO: 스윙 주기 기반 컬럼 간격

// ---------------- LED 출력 ----------------
void outputColumn(uint16_t col) {
#if LED_TYPE == 3
  // 단색 직결: 픽셀에 색이 조금이라도 있으면 ON (색 정보는 무시, 켜짐/꺼짐만)
  for (int y = 0; y < NUM_LEDS; y++) {
    bool on = content.px[col][y][0] | content.px[col][y][1] | content.px[col][y][2];
    digitalWrite(LED_PINS[y], on ? HIGH : LOW);
  }
#elif LED_TYPE
  for (int y = 0; y < NUM_LEDS; y++) {
    uint32_t r = content.px[col][y][0] * cfg.bright / 100;
    uint32_t g = content.px[col][y][1] * cfg.bright / 100;
    uint32_t b = content.px[col][y][2] * cfg.bright / 100;
    strip.setPixelColor(y, r, g, b);
  }
  strip.show();
#endif
}
void ledsOff() {
#if LED_TYPE == 3
  for (int y = 0; y < NUM_LEDS; y++) digitalWrite(LED_PINS[y], LOW);
#elif LED_TYPE
  strip.clear();
  strip.show();
#endif
}

// 대기 모드: 컬러 LED는 느린 무지개 순환, 단색 LED는 물결 점멸
void idleGlow() {
#if LED_TYPE == 3
  static uint16_t phase = 0;
  phase++;
  // 아래에서 위로 흐르는 물결: 한 번에 2~3개씩 켜지며 이동
  for (int y = 0; y < NUM_LEDS; y++) {
    uint8_t d = (y + phase / 6) % NUM_LEDS;
    digitalWrite(LED_PINS[y], d < 3 ? HIGH : LOW);
  }
#elif LED_TYPE
  static uint16_t phase = 0;
  phase++;
  for (int y = 0; y < NUM_LEDS; y++) {
    uint8_t rgb[3];
    ColorSpec cs; cs.type = ColorSpec::RAINBOW;
    colorAt(cs, (phase / 8 + y * 4) % 100, 100, rgb);
    strip.setPixelColor(y, rgb[0] * cfg.bright / 400, rgb[1] * cfg.bright / 400, rgb[2] * cfg.bright / 400);
  }
  strip.show();
#endif
  delay(20);
}

// ---------------- BLE 설정 ----------------
void onBleConnect(uint16_t h) {
  Serial.print("BLE connected, MTU=");
  BLEConnection* conn = Bluefruit.Connection(h);
  Serial.println(conn ? conn->getMtu() : 0);
}
void onBleDisconnect(uint16_t h, uint8_t reason) {
  // 주요 사유: 0x13 원격이 정상 종료, 0x08 신호 끊김(supervision timeout),
  // 0x3E 연결 수립 실패, 0x16 로컬 종료
  Serial.print("BLE disconnected, reason=0x");
  Serial.println(reason, HEX);
}

void bleRxCallback(uint16_t conn_hdl) {
  Serial.print("[cb] rx bytes now=");
  Serial.println(bleuart.available());
}

void setupBle() {
  Bluefruit.begin();
  // (주소 변조 핵 제거 — SoftDevice 가동 중 GAP 주소 변경이 수신 시 하드폴트 유발 의심)
  Bluefruit.setName("POV-STICK");
  Bluefruit.Periph.setConnectCallback(onBleConnect);
  Bluefruit.Periph.setDisconnectCallback(onBleDisconnect);
  Bluefruit.setTxPower(4);
  bleuart.begin();
  // setRxCallback 제거 — BLE 태스크 문맥에서 Serial 출력이 하드폴트 유발 의심.
  // 수신 확인은 loop의 pollBle/RX 로그로 충분
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

// ---------------- 메인 ----------------
void setup() {
  Serial.begin(115200);
  // USB 시리얼이 붙을 때까지 최대 3초 대기 (모니터 열기 전에 부팅 로그가 지나가는 것 방지)
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 3000; ) delay(10);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#if USE_FS
  InternalFS.begin();
#endif
  loadConfig();
#if LED_TYPE == 3
  for (int y = 0; y < NUM_LEDS; y++) {
    pinMode(LED_PINS[y], OUTPUT);
    digitalWrite(LED_PINS[y], LOW);
    // nRF52840 GPIO 고구동(H0H1, ~9mA) 설정 — 기본(2mA)은 LED가 어두움
    uint32_t g = g_ADigitalPinMap[LED_PINS[y]];
    NRF_GPIO_Type* port = g < 32 ? NRF_P0 : NRF_P1;
    port->PIN_CNF[g & 31] = (port->PIN_CNF[g & 31] & ~GPIO_PIN_CNF_DRIVE_Msk)
                            | (GPIO_PIN_CNF_DRIVE_H0H1 << GPIO_PIN_CNF_DRIVE_Pos);
  }
#elif LED_TYPE
  strip.begin();
  strip.show();
#endif
  setupBle();
  findOwner();
  if (owner) {
    // 부팅 크레딧: 첫 스윙에 주인 이름이 뜬다. 버튼/BLE 조작 시 일반 콘텐츠로 전환
    char credit[80];
    snprintf(credit, sizeof(credit), "%s의 선물 %s", owner->name, owner->serial);
    ColorSpec cs; cs.type = ColorSpec::RAINBOW;
    renderText(credit, cs);
  } else if (!loadSlot(cfg.startSlot)) {
    ColorSpec cs; cs.type = ColorSpec::RAINBOW;
    renderText("HELLO", cs);   // 미등록 보드 + 빈 슬롯일 때 기본 콘텐츠
  }
  Serial.println("POV Wand ready");
}

void loop() {
  // 디버그 하트비트: 루프 생존 + BLE 수신 FIFO 상태 (원인 확정 후 제거)
  static uint32_t hb = 0;
  if (millis() - hb > 5000) {
    hb = millis();
    Serial.print("[hb] up="); Serial.print(millis() / 1000);
    Serial.print("s conn="); Serial.print(Bluefruit.connected());
    Serial.print(" rxAvail="); Serial.println(bleuart.available());
    if (Bluefruit.connected()) bleuart.print("EV HB\n");  // 보드→폰 방향 테스트
  }

  pollBle();

  // 미뤄둔 설정 저장 (마지막 변경 1.5초 후)
  if (cfgDirtyAt && millis() - cfgDirtyAt > 1500) {
    saveConfig();
    cfgDirtyAt = 0;
    Serial.println("[cfg] saved");
  }

#if LED_TYPE == 3
  // HW 검사 모드 (TEST 명령): 일반 표시를 멈추고 지정 패턴 유지
  if (testHold >= 0) {
    static uint16_t chase = 0;
    chase++;
    for (int y = 0; y < NUM_LEDS; y++) {
      bool on = testHold == 1 || (testHold == 2 && y == (chase / 15) % NUM_LEDS);
      digitalWrite(LED_PINS[y], on ? HIGH : LOW);
    }
    delay(20);
    return;
  }
#endif

  // 버튼: 짧게 눌러 다음 슬롯 (스켈레톤 — 디바운스/길게누름은 TODO)
  static bool lastBtn = HIGH;
  bool btn = digitalRead(PIN_BUTTON);
  if (lastBtn == HIGH && btn == LOW) {
    uint8_t n = currentSlot;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
      n = n % MAX_SLOTS + 1;
      if (loadSlot(n)) break;
    }
    delay(30); // 임시 디바운스
  }
  lastBtn = btn;

  bool pov = cfg.mode == MODE_POV || (cfg.mode == MODE_AUTO && isSwinging());
  if (pov && content.cols > 0) {
    uint32_t colUs = cfg.speedUs ? cfg.speedUs : swingPeriodUs() / content.cols;
    for (uint16_t c = 0; c < content.cols; c++) {
      outputColumn(c);
      if (colUs >= 1000) delay(colUs / 1000);          // ms 단위는 RTOS 양보(yield)하는 delay로
      delayMicroseconds(colUs % 1000);
      pollBle();
    }
    ledsOff();
    delay(2);  // 프레임 사이 BLE/USB 태스크에 숨 쉴 틈 (busy-wait 독점 방지)
  } else if (cfg.mode != MODE_POV) {
    idleGlow();  // TODO: 일정 시간 후 슬립 + 흔들어 깨우기
  }
}

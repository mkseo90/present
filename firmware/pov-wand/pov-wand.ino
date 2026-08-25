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
#define USE_FS 1
#if USE_FS
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#endif
#include "font8x8.h"
#include "font8x8_kr.h"
#include "font8x8_ext.h"   // 한글 자모(ㅋㅋ/ㅠㅠ) + ♥★♪ 등 기호

#if USE_FS
using namespace Adafruit_LittleFS_Namespace;
#endif

// ---------------- HW 설정 (회로 확정 후 조정) ----------------
// LED 타입: 0=없음(시리얼 디버그), 1=APA102/SK9822(SPI 2선), 2=WS2812B(단선),
//           3=단색 LED GPIO 직결 (최종 HW: GREEN LED 8개)
#define LED_TYPE 3
#define NUM_LEDS 7

// 핀맵 (2026-08-20, 확장보드 최종 채택에 따른 A안):
//   확장보드와의 충돌 회피 — D1(버튼)·D3(부저)·D4/D5(I2C)를 LED에서 제외.
//   버튼은 확장보드 내장 버튼(D1)을 그대로 사용.
//   구형 하네스(D0~D6, 버튼 D10)로 테스트하려면 PINMAP_LEGACY 1
// 2026-08-20: 부저 물리 제거로 소리 문제 해결 → LED는 D0~D6 유지 (사용자 결정)
#define PINMAP_LEGACY 1
#if PINMAP_LEGACY
#define PIN_BUTTON 10
#else
#define PIN_BUTTON 1          // 확장보드 내장 버튼
#endif

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
#if PINMAP_LEGACY
static const uint8_t LED_PINS[NUM_LEDS] = { 0, 1, 2, 3, 4, 5, 6 };
#else
static const uint8_t LED_PINS[NUM_LEDS] = { 0, 2, 6, 7, 8, 9, 10 };
#endif
#endif

// ---------------- 상수 ----------------
// 버전 규칙: 1.0.0 미만 = 단색(GREEN) 완드, 1.0.0 이상 = RGB 완드 — 앱이 이걸로 UI를 전환
#define FW_VERSION "0.0.2"
#define MAX_SLOTS 12
#define MAX_COLS 256          // 콘텐츠 최대 컬럼(글자 32자 분량)
#define IMG_MAX_BYTES (MAX_COLS * 3 * 8 / 8)  // 사용 안 함(자리표시)

// ---------------- 보안 (LESC 페어링) ----------------
// 1이면: 연결 직후 기기가 페어링을 요구하고(슬레이브 보안 요청), 암호화가 성립되기
// 전에는 모든 명령을 ERR 8로 거절한다. nRF52840은 CryptoCell이 있어 이 코어의
// 기본 페어링 파라미터가 이미 LESC(bond=1, mitm=0, lesc=1) — 즉 "LESC Just Works".
//   - 얻는 것: 링크 암호화(도청 방지) + 본딩(재연결 시 자동 암호화) + 명령 게이트
//   - 한계: Just Works라 MITM 방어는 없음 (패스키 표시 수단이 없는 HW의 물리적 한계.
//           setPIN()은 레거시 페어링으로 다운그레이드되므로 사용하지 않는다)
//   - 페어링 정보가 어긋나 연결이 계속 실패하면: 폰에서 기기 삭제 + CLRBOND 명령
// ★ 웹앱(Android Chrome)/Bluefy(iOS) 호환은 실기기 검증 필요 — 문제시 0으로
#define REQUIRE_PAIRING 1

// ---------------- BLE ----------------
BLEUart bleuart;
volatile bool linkSecured = false;
uint32_t pairReqAt = 0;   // 연결 후 이 시각에 페어링 요청 (0=예약 없음)
// OTA(DFU)는 상시 서비스로 열지 않는다. "DFU <PIN>" 명령이 맞을 때만
// 부트로더로 재부팅해 그 순간에만 무선 업데이트를 허용 (무단 리플래시 방지)

// ---------------- 주인 지정 ----------------
// 주인 정보(이름·시리얼·LED색)는 **기기 플래시에 주입**한다 (SETOWNER 명령).
// 실명을 소스에 두지 않으므로 공개 저장소에 사람 이름이 남지 않고, 새 PC에서
// 파일을 옮겨 다닐 필요도 없다 — 보드가 자기 주인을 기억한다.
//
// secrets.h 는 선택사항이다. 있으면 MAC→주인 표를 대체 경로로 쓴다(과거 방식 호환).
// 없어도 컴파일된다 → 새 클론에서 바로 빌드 가능.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#    define HAS_SECRETS 1
#  endif
#endif
#ifndef HAS_SECRETS
#  define HAS_SECRETS 0
#endif

// 주인 미주입 시의 기본 소유자 (SETOWNER로 언제든 덮어씀)
#define DEFAULT_OWNER_NAME   "USER"
#define DEFAULT_OWNER_SERIAL "No.000/000"
#define DEFAULT_OWNER_COLOR  "00FF66"

char myMac[18] = "?";

// 런타임 주인 정보. INFO 응답과 부팅 크레딧이 이걸 본다
struct OwnerRec {
  char name[32];
  char serial[24];
  char color[8];     // RRGGBB
  bool valid;
} ownerRec;

// 플래시 접근 함수는 아래 슬롯 저장 섹션에 있다 (아두이노 자동 프로토타입 순서 회피용 선언)
bool loadOwner();
bool saveOwner(const char* name, const char* serial, const char* color);
void clearOwner();

// 고정 버퍼에 문자열을 담는다. 넘치면 자르되 UTF-8 문자 중간에서 자르지 않는다
// (SETOWNER는 길이를 미리 검사하므로 실제로는 잘리지 않지만, 플래시에서 읽은
//  값이 손상된 경우를 대비한 방어선이다)
void copyUtf8(char* dst, size_t cap, const char* src) {
  size_t n = strlen(src);
  if (n > cap - 1) {
    n = cap - 1;
    while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;   // 연속 바이트면 물러난다
  }
  memcpy(dst, src, n);
  dst[n] = 0;
}

void setOwnerRec(const char* name, const char* serial, const char* color) {
  copyUtf8(ownerRec.name,   sizeof(ownerRec.name),   name);
  copyUtf8(ownerRec.serial, sizeof(ownerRec.serial), serial);
  copyUtf8(ownerRec.color,  sizeof(ownerRec.color),  color);
  ownerRec.valid = true;
}

#if HAS_SECRETS
// OWNERS[].mac 은 전체 MAC이 아니어도 된다 — 뒷부분만 적어도 일치로 본다.
// 콜론 단위로 끊는 것을 권장 (시리얼 출력에서 그대로 잘라 붙일 수 있다).
bool macMatches(const char* pattern, const char* mac) {
  size_t pl = strlen(pattern), ml = strlen(mac);
  if (pl < 2 || pl > ml) return false;   // 너무 짧으면 오인 위험 → 최소 2글자
  return strcasecmp(mac + (ml - pl), pattern) == 0;
}
#endif

void findOwner() {
  uint8_t m[6];
  Bluefruit.getAddr(m);
  snprintf(myMac, sizeof(myMac), "%02X:%02X:%02X:%02X:%02X:%02X",
           m[5], m[4], m[3], m[2], m[1], m[0]);
  Serial.print("MAC: "); Serial.println(myMac);
  Serial.flush();

  // 1순위: 기기에 주입된 정보
  if (loadOwner()) {
    Serial.print("owner(flash): "); Serial.println(ownerRec.name);
    Serial.flush();
    return;
  }
#if HAS_SECRETS
  // 2순위: secrets.h 의 MAC→주인 표 (과거 방식)
  for (uint8_t i = 0; i < sizeof(OWNERS) / sizeof(OWNERS[0]); i++) {
    if (macMatches(OWNERS[i].mac, myMac)) {
      setOwnerRec(OWNERS[i].name, OWNERS[i].serial, OWNERS[i].color);
      Serial.println("owner(secrets.h)");
      Serial.flush();
      return;
    }
  }
#endif
  // 3순위: 기본값 — 광고 이름은 "USER의 LED", INFO owner=USER
  setOwnerRec(DEFAULT_OWNER_NAME, DEFAULT_OWNER_SERIAL, DEFAULT_OWNER_COLOR);
  Serial.println("owner: default (SETOWNER 로 주입 가능)");
  Serial.flush();
}

// ---------------- 광고 이름 ----------------
// 완드를 여러 개 동시에 켰을 때 기기 선택창에서 구분되도록 주인 이름을 쓴다.
//   주인 주입됨  -> "<이름>의 LED"
//   미주입       -> "POV-STICK" (공장 기본값 — 새 보드도 항상 찾을 수 있게)
//
// 이름은 스캔 응답 AD 필드(31바이트)에 들어간다. AD 타입/길이 2바이트를 빼면
// 29바이트가 한계이므로 넘치면 자르되, UTF-8 문자 중간에서 자르지 않는다.
#define BLE_NAME_MAX 29
char bleName[BLE_NAME_MAX + 1] = "POV-STICK";

void composeBleName() {
  if (!ownerRec.valid || !ownerRec.name[0]) {
    snprintf(bleName, sizeof(bleName), "POV-STICK");
    return;
  }
  const char* suffix = "의 LED";
  size_t sufLen = strlen(suffix);
  size_t budget = BLE_NAME_MAX > sufLen ? BLE_NAME_MAX - sufLen : 0;

  size_t n = strlen(ownerRec.name);
  if (n > budget) {
    n = budget;
    // 자른 지점이 UTF-8 연속 바이트(10xxxxxx)면 문자 경계까지 물러난다
    while (n > 0 && ((uint8_t)ownerRec.name[n] & 0xC0) == 0x80) n--;
  }
  snprintf(bleName, sizeof(bleName), "%.*s%s", (int)n, ownerRec.name, suffix);
}

// ---------------- 상태 ----------------
enum Mode { MODE_AUTO, MODE_POV, MODE_IDLE };
struct Config {
  uint8_t bright = 80;        // %
  uint8_t mode = MODE_AUTO;
  uint16_t speedUs = 0;       // 0 = IMU 자동
  uint8_t startSlot = 1;
  uint8_t flip = 0;           // 1 = 컬럼 역순 출력 (스윙 방향에 따른 좌우반전 보정)
  uint8_t swingDps = 70;      // 스윙 감지 문턱(도/초). 낮을수록 살살 흔들어도 잔상 (SET SWING)
} cfg;

// 표시 버퍼: 컬럼당 8픽셀 × RGB
struct Content {
  uint16_t cols = 0;
  uint8_t px[MAX_COLS][8][3]; // [컬럼][행][RGB]
} content;

// 보드 내장 LED(빨강)를 0.5초 주기로 깜빡여 "펌웨어 살아있음"을 눈으로 보여준다.
// 외부 LED 하네스와 전혀 무관한 신호라 배선 문제와 펌웨어 문제를 분리할 수 있다.
// ★ 선물용 최종 빌드에서는 0으로 (완드에서 빨간 불이 깜빡이면 보기 싫으니까)
#define DEBUG_BEACON 1

uint8_t currentSlot = 0;
// HW 검사용: -1=해제, 0=전체소등, 1=전체점등, 2=순차점등(체이스), 3=단일채널(testPin)
int8_t testHold = -1;
int8_t testPin = 0;    // testHold==3 일 때 켤 채널 (0~NUM_LEDS-1)
uint32_t cfgDirtyAt = 0;  // 설정 변경 시각 — 플래시 저장은 1.5초 뒤로 미룸 (통신 중 즉시 쓰기 회피)

// 이미지 수신 상태 (IMGB/IMGD/IMGE)
// 주의: 멤버 기본값(NSDMI)을 넣지 말 것. 넣으면 aggregate가 아니게 되어
// `imgRx = {}` 한 줄이 6KB 임시객체를 스택에 만든다 (BLE 수신 즉시 프리즈의 원인).
// 전역이라 .bss에서 0으로 초기화되므로 기본값은 필요 없다.
struct ImgRx {
  bool active;
  uint8_t slot;
  uint16_t cols;
  uint16_t received;
  uint16_t expectSeq;
  uint8_t buf[MAX_COLS * 24];
} imgRx;

// ---------------- 유틸 ----------------
void reply(const char* s) {
  Serial.print("TX: "); Serial.println(s);
  Serial.flush();   // USB CDC는 비동기 전송 — 폴트가 나면 버퍼에 남은 로그가 유실된다

  char out[244];
  int n = strlen(s);
  if (n > (int)sizeof(out) - 2) n = sizeof(out) - 2;
  memcpy(out, s, n);
  out[n++] = '\n';   // 개행까지 한 덩어리로 (기존엔 notify 2번으로 쪼개져 나갔다)

  // notify()는 한 번에 최대 _max_len(= 협상 가능한 최대 MTU, 여기선 23)까지만 보내고
  // 나머지는 버린다. 그래서 긴 응답(INFO/LIST)은 MTU-3 단위로 직접 나눠 보낸다.
  BLEConnection* conn = Bluefruit.Connection(Bluefruit.connHandle());
  int chunk = conn ? (int)conn->getMtu() - 3 : 20;
  if (chunk < 1) chunk = 20;

  for (int i = 0; i < n; i += chunk) {
    int len = n - i < chunk ? n - i : chunk;
    // 주 방어선은 setupBle()의 hvn_qsize=8. 그래도 큐가 다 차면 코어는 빈 칸을
    // 100ms(BLE_GENERIC_TIMEOUT)만 기다린 뒤 조용히 실패하고 그 패킷을 버린다.
    // (큐 1칸이던 시절 실측: Windows 상대로 INFO 5패킷 중 2패킷 유실)
    // write()는 실패 시 0을 돌려주므로 반환값을 보고 재시도한다 — 2차 방어선.
    bool sent = false;
    for (int retry = 0; retry < 12 && !sent; retry++) {
      sent = (bleuart.write((const uint8_t*)out + i, len) == (size_t)len);
      if (!sent) delay(20);
    }
    if (!sent) {
      Serial.print("[tx] chunk dropped @"); Serial.println(i);
      Serial.flush();
    }
  }
}
void replyf(const char* fmt, ...) {
  char buf[200];   // INFO 응답이 길다 (owner/serial/mac/color/leds). 잘리면 앱 파싱이 깨짐
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
  for (uint16_t i = 0; i < FONT8_EXT_COUNT; i++)   // 자모·기호 (84자, 선형 탐색으로 충분)
    if (FONT8_EXT_CP[i] == cp) { memcpy(out, FONT8_EXT[i], 8); return; }
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
bool loadOwner() { return false; }
bool saveOwner(const char*, const char*, const char*) { return false; }
void clearOwner() {}
void seedDefaultSlots() {}
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
      // 색 줄은 미리보기에 쓰지 않으므로 값을 담지 않고 위치만 넘긴다
      char text[64] = {0};
      int c;
      while ((c = f.read()) >= 0 && c != '\n') { }
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

// 주인 정보 파일 /owner: "이름\n시리얼\n색\n" (사람이 읽을 수 있는 형식)
bool saveOwner(const char* name, const char* serial, const char* color) {
  InternalFS.remove("/owner");
  File f(InternalFS);
  if (!f.open("/owner", FILE_O_WRITE)) return false;
  f.write((const uint8_t*)name, strlen(name));   f.write('\n');
  f.write((const uint8_t*)serial, strlen(serial)); f.write('\n');
  f.write((const uint8_t*)color, strlen(color)); f.write('\n');
  f.close();
  setOwnerRec(name, serial, color);
  return true;
}

bool loadOwner() {
  File f(InternalFS);
  if (!f.open("/owner", FILE_O_READ)) return false;
  char buf[80] = {0};
  int n = f.read((uint8_t*)buf, sizeof(buf) - 1);
  f.close();
  if (n <= 0) return false;

  // 줄 단위로 자른다 (이름 / 시리얼 / 색)
  char* line[3] = { nullptr, nullptr, nullptr };
  int li = 0;
  char* p = buf;
  line[li++] = p;
  for (; *p && li < 3; p++) {
    if (*p == '\n') { *p = 0; line[li++] = p + 1; }
  }
  for (; *p; p++) if (*p == '\n') { *p = 0; break; }   // 세 번째 줄 종료
  if (li < 3 || !line[0][0]) return false;

  setOwnerRec(line[0], line[1], line[2][0] ? line[2] : "-");
  return true;
}

void clearOwner() {
  InternalFS.remove("/owner");
  ownerRec.valid = false;
}

// 공장 기본 메시지 시딩: 이 펌웨어를 처음 켠 기기에서 딱 한 번, 슬롯 1~3에
// [소유자이름 / Hello world! / 그동안 감사했습니다]를 저장한다.
// /seeded 마커를 남기므로 사용자가 지우거나 고친 메시지는 다시 살아나지 않는다.
void seedDefaultSlots() {
  File f(InternalFS);
  if (f.open("/seeded", FILE_O_READ)) { f.close(); return; }
  if (usedSlots() == 0) {
    const char* color = ownerRec.valid ? ownerRec.color : DEFAULT_OWNER_COLOR;
    saveSlotText(1, color, ownerRec.valid ? ownerRec.name : DEFAULT_OWNER_NAME);
    saveSlotText(2, color, "Hello world!");
    saveSlotText(3, color, "그동안 감사했습니다");
    Serial.println("[seed] default slots 1-3 written");
    Serial.flush();
  }
  if (f.open("/seeded", FILE_O_WRITE)) { f.write('1'); f.close(); }
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
  Serial.flush();   // 여기까지 도달했는지를 확실히 눈으로 확인하기 위해
  char* cmd = strtok(line, " ");
  if (!cmd) return;

#if REQUIRE_PAIRING
  // 암호화(페어링) 전에는 명령을 받지 않는다. 오류 응답 자체는 열린 notify로
  // 나가므로 앱이 "페어링 필요"를 알 수 있다
  if (!linkSecured) {
    reply("ERR 8 pairing required");
    return;
  }
#endif

  if (strcmp(cmd, "PING") == 0) { reply("PONG"); return; }

  // IMU 상태 조회 (스윙 감지 튜닝용)
  if (strcmp(cmd, "IMU") == 0) {
#if USE_IMU
    if (!imuOk) { reply("ERR 3 imu init failed"); return; }
    float gx = imu.readFloatGyroX(), gy = imu.readFloatGyroY(), gz = imu.readFloatGyroZ();
    replyf("IMU sw=%d axis=%d dir=%d stroke=%lums g=%d/%d/%d dps",
           sw.swinging ? 1 : 0, sw.axis, sw.dir, (unsigned long)sw.strokeMs,
           (int)gx, (int)gy, (int)gz);
#else
    reply("ERR 3 imu disabled");
#endif
    return;
  }

  if (strcmp(cmd, "INFO") == 0) {
    const char* m = cfg.mode == MODE_AUTO ? "auto" : cfg.mode == MODE_POV ? "pov" : "idle";
    // color/leds: 단색 완드의 실제 하드웨어를 앱에 알려 미리보기를 맞추기 위한 필드.
    // 미등록 보드는 color=- 로 나가고 앱이 기본값으로 대체한다
    replyf("INFO fw=%s slots=%d used=%d bat=%d mode=%s bright=%d owner=%s serial=%s mac=%s color=%s leds=%d",
           FW_VERSION, MAX_SLOTS, usedSlots(), readBattery(), m, cfg.bright,
           ownerRec.valid ? ownerRec.name   : "-",
           ownerRec.valid ? ownerRec.serial : "-", myMac,
           ownerRec.valid ? ownerRec.color  : "-", NUM_LEDS);
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
    // `imgRx = {}` 금지. ImgRx는 6KB짜리라 임시객체가 loop 태스크 스택(기본 6KB)을
    // 통째로 날린다. 그 자리는 handleLine() 프롤로그에서 미리 잡히므로 명령 종류와
    // 무관하게 "수신하는 순간" 하드폴트했다. 반드시 제자리(in-place) 초기화로.
    memset(&imgRx, 0, sizeof(imgRx));
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

  // 주인 주입: SETOWNER <이름> <시리얼> <색>  (예: SETOWNER 민경 No.001/001 00FF66)
  // 실명을 소스에 두지 않기 위해 기기 플래시에 저장한다. 재부팅 후에도 유지된다.
  // 세 인자 모두 공백을 포함할 수 없다 (INFO 응답이 공백으로 토큰을 나누므로)
  if (strcmp(cmd, "SETOWNER") == 0) {
    char* nm = strtok(nullptr, " ");
    char* sr = strtok(nullptr, " ");
    char* co = strtok(nullptr, " ");
    if (!nm || !sr || !co) { reply("ERR 1 usage SETOWNER <name> <serial> <color>"); return; }
    if (strlen(nm) > 31 || strlen(sr) > 23) { reply("ERR 2 name/serial too long"); return; }
    ColorSpec probe;
    if (strlen(co) != 6 || !hex2rgb(co, probe.a)) { reply("ERR 2 color must be RRGGBB"); return; }
    if (!saveOwner(nm, sr, co)) { reply("ERR 3 storage unavailable"); return; }
    composeBleName();   // 다음 부팅부터 이 이름으로 광고한다
    replyf("OK SETOWNER name=%s reboot_to_apply", bleName);
    return;
  }

  if (strcmp(cmd, "CLROWNER") == 0) {
    clearOwner();
    reply("OK CLROWNER");
    return;
  }

  // 본딩 정보 삭제 (페어링이 어긋나 재연결이 계속 실패할 때의 복구 수단)
  if (strcmp(cmd, "CLRBOND") == 0) {
    Bluefruit.Periph.clearBonds();
    reply("OK CLRBOND reconnect_after_forget_on_phone");
    return;
  }

  // 배선 진단: PROBE [up|down] — 각 LED 핀의 전압을 mV로 회신
  if (strcmp(cmd, "PROBE") == 0) {
#if LED_TYPE == 3
    char* m = strtok(nullptr, " ");
    if (m && strcmp(m, "sweep") == 0) { probeSweep(); return; }
    bool up = !(m && strcmp(m, "down") == 0);
    probePins(up);
#else
    reply("ERR 1 PROBE is LED_TYPE 3 only");
#endif
    return;
  }

  // HW 브링업용 LED 테스트:
  //   TEST on      전체 점등
  //   TEST off     전체 소등(홀드)
  //   TEST chase   1개씩 순차 점등 (전류 1/7 — 전원 여유 없을 때 확인용)
  //   TEST pin <n> n번 채널만 계속 점등 (멀티미터 프로빙 / 죽은 채널 특정)
  //   TEST drive low|high  GPIO 구동 세기 (S0S1 ~2mA / H0H1 ~9mA)
  //     주의: SET BRIGHT는 LED_TYPE 3에서 아무 효과가 없다(digitalWrite 켜짐/꺼짐뿐).
  //           단색 직결 LED의 실질적인 밝기 조절 수단은 이 구동 세기다.
  //   TEST end     해제(일반 표시 복귀)
  if (strcmp(cmd, "TEST") == 0) {
    char* m = strtok(nullptr, " ");
    if (!m) { reply("ERR 1 usage TEST on|off|chase|pin <n>|drive low|high|end"); return; }
    if (strcmp(m, "on") == 0) testHold = 1;
    else if (strcmp(m, "off") == 0) testHold = 0;
    else if (strcmp(m, "chase") == 0) testHold = 2;
    else if (strcmp(m, "end") == 0) testHold = -1;
    else if (strcmp(m, "drive") == 0) {
#if LED_TYPE == 3
      char* d = strtok(nullptr, " ");
      if (!d) { reply("ERR 1 usage TEST drive low|high"); return; }
      if (strcmp(d, "low") == 0)  { setLedDrive(false); reply("OK TEST drive low");  return; }
      if (strcmp(d, "high") == 0) { setLedDrive(true);  reply("OK TEST drive high"); return; }
      reply("ERR 2 bad drive");
      return;
#else
      reply("ERR 1 drive is LED_TYPE 3 only");
      return;
#endif
    }
    else if (strcmp(m, "pin") == 0) {
      char* ns = strtok(nullptr, " ");
      int n = ns ? atoi(ns) : -1;
      if (n < 0 || n >= NUM_LEDS) { replyf("ERR 2 pin 0..%d", NUM_LEDS - 1); return; }
      testPin = n; testHold = 3;
      replyf("OK TEST pin %d", n);
      return;
    }
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
    } else if (strcmp(what, "FLIP") == 0) {
      cfg.flip = atoi(val) ? 1 : 0;
    } else if (strcmp(what, "SWING") == 0) {
      cfg.swingDps = constrain(atoi(val), 30, 250);   // 낮을수록 예민 (기본 70)
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

// ---------------- 배터리 ----------------
// XIAO 내장 분압(1M/510k)으로 VBAT 측정. VBAT_ENABLE(P0.14)을 LOW로 해야 분압이 연결된다
// (누설 ~3µA라 setup에서 상시 LOW로 둔다). 충전은 내장 BQ25101이 하드웨어로 알아서 하고,
// 기본 전류 50mA다 — 배터리 용량 확정 후 100mA로 올릴지 결정 (P0.13 LOW = 100mA).
int readBattery() {
  analogReference(AR_INTERNAL);   // 3.6V
  analogReadResolution(12);
  uint32_t acc = 0;
  for (int i = 0; i < 4; i++) acc += analogRead(PIN_VBAT);
  // mV = adc/4095*3600 * (1000k+510k)/510k
  uint32_t mv = (uint64_t)(acc / 4) * 3600 * 1510 / (4095 * 510);
  // LiPo 근사: 3300mV=0%, 4150mV=100% (USB 급전 중엔 ~4.2V 근처로 뜬다)
  int pct = (int)((mv - 3300) * 100 / (4150 - 3300));
  return constrain(pct, 0, 100);
}

// ---------------- IMU 스윙 감지 ----------------
// XIAO Sense 내장 LSM6DS3(가속도+자이로, Wire1, 전원핀은 라이브러리가 처리).
// 자이로로 스윙을 감지한다: 완드를 휘두르면 손목 중심 회전이라 각속도가 크다.
//   - 지배 축 자동 선정: 처음 임계값을 넘은 축을 그 스윙의 기준 축으로 (조립 방향 무관)
//   - 방향 반전 순간 포착 → 반 주기(스트로크) 측정 → 스트로크당 한 번 그리기
//   - 방향에 따라 컬럼 순서를 뒤집어 왕복 양쪽에서 글자가 바로 보이게 한다
#define USE_IMU 1
#if USE_IMU
#include "LSM6DS3.h"
LSM6DS3 imu(I2C_MODE, 0x6A);
#endif
bool imuOk = false;

// 튜닝 파라미터. 스윙 문턱은 cfg.swingDps 로 런타임 조절 (SET SWING <dps>)
#define SWING_ON_DPS  ((float)cfg.swingDps)
#define SWING_OFF_MS  700      // 이 시간 동안 잠잠하면 대기 모드로

struct Swing {
  bool swinging;
  int8_t axis;          // 지배 축 0=X 1=Y 2=Z (-1=미정)
  float rate;           // 지배 축 최근 각속도 (부호 포함)
  int8_t dir;           // 현재 스트로크 방향 +1/-1
  uint32_t lastRevMs;   // 마지막 방향 반전 시각
  uint32_t strokeMs;    // 최근 스트로크(반 주기) 추정
  uint32_t lastActiveMs;
  bool strokePending;   // 반전 감지됨 → 이번 스트로크에 한 번 그릴 것
} sw = { false, -1, 0, 0, 0, 300, 0, false };
int8_t povDir = 1;           // 수동(연속) 잔상용 실시간 방향 — 약한 스윙에서도 추적
uint32_t lastMotionMs = 0;   // 마지막으로 회전 움직임(>60dps)이 있던 시각
uint32_t lastStillMs = 0;    // 마지막으로 "정지 상태"가 확인된 시각 (탭 게이트용 — 탭 자체의
                             // 진동이 판정을 깨지 않도록 '직전'의 정지를 본다)

void imuTick() {
#if USE_IMU
  if (!imuOk) return;
  static uint32_t lastMs = 0;
  if (millis() - lastMs < 5) return;   // ~200Hz 상한
  lastMs = millis();

  float g[3] = { imu.readFloatGyroX(), imu.readFloatGyroY(), imu.readFloatGyroZ() };
  float amax = 0; int ai = 0;
  for (int i = 0; i < 3; i++) { float a = fabsf(g[i]); if (a > amax) { amax = a; ai = i; } }

  // 수동(연속) 잔상 모드용 실시간 방향: 스윙 임계 미만의 움직임에서도 방향만은 따라간다.
  // 축은 스윙 중이면 지배 축을, 아니면 이번 샘플의 최대 축을 쓴다
  {
    int a2 = (sw.swinging && sw.axis >= 0) ? sw.axis : ai;
    if (fabsf(g[a2]) > 40) povDir = g[a2] > 0 ? 1 : -1;
  }

  if (!sw.swinging) {
    if (amax > 60) lastMotionMs = millis();
    if (millis() - lastMotionMs > 250) lastStillMs = millis();
    if (amax > SWING_ON_DPS) {
      sw.swinging = true;
      sw.axis = ai;
      sw.dir = g[ai] > 0 ? 1 : -1;
      sw.lastRevMs = sw.lastActiveMs = millis();
      sw.strokePending = true;   // 첫 스트로크부터 그린다
    }
    return;
  }

  sw.rate = g[sw.axis];
  if (fabsf(sw.rate) > SWING_ON_DPS) sw.lastActiveMs = millis();
  if (amax > 60) lastMotionMs = millis();
  if (millis() - lastMotionMs > 250) lastStillMs = millis();   // 250ms 조용했으면 "정지 확인"
  if (millis() - sw.lastActiveMs > SWING_OFF_MS) {
    sw.swinging = false; sw.axis = -1; sw.strokePending = false;
    return;
  }

  // 방향 반전: 반대 부호로 임계값을 넘는 순간 (히스테리시스 겸용)
  int8_t d = sw.rate > SWING_ON_DPS ? 1 : (sw.rate < -SWING_ON_DPS ? -1 : 0);
  if (d != 0 && d != sw.dir) {
    uint32_t now = millis();
    uint32_t dt = now - sw.lastRevMs;
    if (dt > 80 && dt < 2500) sw.strokeMs = dt;   // 느린 스윙까지 허용 (말도 안 되는 값만 버림)
    sw.lastRevMs = now;
    sw.dir = d;
    sw.strokePending = true;
  }
#endif
}

bool isSwinging() { return sw.swinging; }
uint32_t swingPeriodUs() { return 20000; }  // MODE_POV(수동)에서 SET SPEED 0일 때의 폴백

// ---- 톡톡(더블탭) 제스처: 버튼 없이 슬롯 전환 ----
// 감지는 LSM6DS3 **하드웨어 더블탭 엔진**이 한다 (칩이 1.66kHz로 자체 감시 —
// 소프트웨어 폴링은 1~3ms짜리 탭 충격을 놓치기 일쑤였다).
// 발동 조건 두 가지 모두 충족 (사용자 요구):
//   1. 멈춤 상태 — 스윙 아님 + "탭 직전"에 정지가 확인됐을 것 (탭 자체의 진동이
//      판정을 깨지 않도록 lastStillMs 래치를 본다)
//   2. 더블탭 — 칩의 TAP_SRC 레지스터 DOUBLE_TAP 비트
bool tapCycleReq = false;

void tapEngineInit() {
#if USE_IMU
  if (!imuOk) return;
  imu.writeRegister(0x10, 0x60);  // CTRL1_XL: 가속도 416Hz, ±2g (탭 엔진 요구 조건)
  imu.writeRegister(0x58, 0x8F);  // TAP_CFG: 인터럽트 en + X/Y/Z 탭 감지 + 래치
  imu.writeRegister(0x59, 0x0C);  // TAP_THS_6D: 임계 12/31 (~750mg) — 톡톡 세기 튜닝 포인트
  imu.writeRegister(0x5A, 0x7F);  // INT_DUR2: 더블탭 간격 최대(~540ms), quiet/shock 여유
  imu.writeRegister(0x5B, 0x80);  // WAKE_UP_THS: 더블탭 모드 활성
#endif
}

void tapTick() {
#if USE_IMU
  if (!imuOk || sw.swinging) return;
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 30) return;
  lastPoll = millis();

  uint8_t src = 0;
  imu.readRegister(&src, 0x1C);          // TAP_SRC (래치라 읽으면 해제)
  if (src & 0x30) {                      // bit5=SINGLE_TAP, bit4=DOUBLE_TAP
    Serial.print("[tap] TAP_SRC=0x"); Serial.print(src, HEX);
    Serial.print(" still="); Serial.println(millis() - lastStillMs);
    Serial.flush();
  }
  if (src & 0x10) {                      // DOUBLE_TAP
    // 조건 1: 직전 1초 안에 "정지 확인"이 있었을 것 (더블탭 소요시간 감안한 창)
    if (millis() - lastStillMs < 1000) tapCycleReq = true;
    else Serial.println("[tap] blocked by still-gate");
  }
#endif
}

// ---------------- OLED (확장보드 SSD1306 128x64, I2C 0x3C) ----------------
// 주인 이름·배터리·연결 상태를 표시한다. 부팅 시 주소를 탐지해서 없으면 조용히 비활성.
// ⚠ 확장보드 I2C(D4/D5)가 현재 LED 채널 4·5와 겹친다 — 핀맵 이사(A안) 확정 전까지는
//   LED 하네스와 OLED를 동시에 물리지 말 것.
// OLED 공존 모드 (2026-08-20): SSD1306은 자체 GDDRAM으로 표시를 유지하므로
// I2C는 "그릴 때만" 필요하다. 이를 이용해 LED(D4/D5)와 버스를 시분할 공유한다:
//   - 화면을 다시 그릴 때만 Wire를 켜서 전송(~30ms) 후 즉시 핀을 LED로 반환
//   - LED 토글이 우연히 유효한 I2C 패턴을 만들면 화면이 깨질 수 있으므로
//     대기 상태에서 10초마다 다시 그려 자가 치유한다 (스윙/테스트 중엔 안 건드림)
#define USE_OLED 1
#if USE_OLED
#include <Wire.h>
bool oledOk = false;
uint8_t oledBuf[1024];   // 128x64 / 8

void oledCmd(uint8_t c) {
  Wire.beginTransmission(0x3C);
  Wire.write((uint8_t)0x00);
  Wire.write(c);
  Wire.endTransmission();
}

// I2C 버스 점유/반환 (D4/D5를 LED와 시분할)
void oledBusTake() {
  Wire.begin();
  Wire.setClock(400000);
}
void oledBusRelease() {
  Wire.end();
#if LED_TYPE == 3
  // D4/D5를 LED 출력으로 복귀 (고구동 포함)
  for (int y = 0; y < NUM_LEDS; y++) {
    if (LED_PINS[y] == 4 || LED_PINS[y] == 5) {
      pinMode(LED_PINS[y], OUTPUT);
      digitalWrite(LED_PINS[y], LOW);
    }
  }
  setLedDrive(true);
#endif
}

bool oledInit() {
  oledBusTake();
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() != 0) { oledBusRelease(); return false; }   // 없음
  static const uint8_t seq[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
    0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF };
  // 마지막 명령(0xAF=표시 켜기) 전에 화면 메모리부터 지운다 —
  // GDDRAM은 전원 인가 시 쓰레기값이라 그냥 켜면 노이즈가 그대로 보인다
  for (uint8_t i = 0; i < sizeof(seq) - 1; i++) oledCmd(seq[i]);
  memset(oledBuf, 0, sizeof(oledBuf));
  oledShow();
  oledCmd(0xAF);
  oledBusRelease();
  return true;
}

void oledShow() {
  oledCmd(0x21); oledCmd(0); oledCmd(127);   // 컬럼 범위
  oledCmd(0x22); oledCmd(0); oledCmd(7);     // 페이지 범위
  for (int i = 0; i < 1024; i += 16) {
    Wire.beginTransmission(0x3C);
    Wire.write((uint8_t)0x40);
    Wire.write(oledBuf + i, 16);
    Wire.endTransmission();
  }
}

// 8x8 글리프(우리 폰트 테이블)를 (x, page)에 그린다. scale 1 또는 2
// 폰트 바이트 = 세로 한 컬럼(bit0=위) — SSD1306 페이지 구조와 동일해서 직행 복사 가능
void oledGlyph(int x, int page, uint32_t cp, int scale) {
  uint8_t g[8];
  glyphFor(cp, g);
  if (scale == 1) {
    for (int c = 0; c < 8 && x + c < 128; c++) oledBuf[page * 128 + x + c] |= g[c];
    return;
  }
  // 2배: 각 비트를 세로 2비트로, 컬럼을 가로 2번
  for (int c = 0; c < 8; c++) {
    uint16_t col2 = 0;
    for (int b = 0; b < 8; b++) if (g[c] >> b & 1) col2 |= 3 << (b * 2);
    for (int dx = 0; dx < 2; dx++) {
      int px = x + c * 2 + dx;
      if (px >= 128) break;
      oledBuf[page * 128 + px]       |= col2 & 0xFF;
      oledBuf[(page + 1) * 128 + px] |= col2 >> 8;
    }
  }
}

// UTF-8 문자열을 그린다. scale 2면 16px 높이. 반환: 그린 폭(px)
int oledText(int x, int page, const char* s, int scale) {
  const char* p = s;
  while (*p && x < 128) {
    uint32_t cp = nextCp(&p);
    if (cp < 0x20) continue;
    oledGlyph(x, page, cp, scale);
    x += 8 * scale + scale;   // 글자 폭 + 자간
  }
  return x;
}

int oledTextWidth(const char* s, int scale) {
  int n = 0;
  for (const char* p = s; *p; ) { nextCp(&p); n++; }
  return n * (8 * scale + scale);
}

// 7px 상태 아이콘 (컬럼 바이트, bit0=위)
static const uint8_t ICO_BT[]    = { 0x22, 0x14, 0x7F, 0x36 };   // 블루투스 룬
static const uint8_t ICO_WAVE[]  = { 0x08, 0x14, 0x22, 0x41 };   // 전파 (대기중)
static const uint8_t ICO_CHECK[] = { 0x10, 0x20, 0x10, 0x08, 0x04 }; // 체크 (연결됨)
static const uint8_t ICO_DOTS[]  = { 0x40, 0x00, 0x40, 0x00, 0x40 }; // … (페어링중)

void oledIcon(int x, int page, const uint8_t* d, int n) {
  for (int c = 0; c < n && x + c < 128; c++) oledBuf[page * 128 + x + c] |= d[c];
}

// 배터리 아이콘 (20x7px + 꼭지). 내부를 잔량 비율만큼 채운다
void oledBattIcon(int x, int page, int pct) {
  const int BODY = 18;
  for (int c = 0; c < BODY; c++) {
    uint8_t v = 0x41;                       // 위(bit0)/아래(bit6) 테두리
    if (c == 0 || c == BODY - 1) v = 0x7F;  // 양쪽 벽
    else if (c >= 2 && c < 2 + (pct * 14 + 50) / 100) v |= 0x3E;  // 채움 (14칸)
    if (x + c < 128) oledBuf[page * 128 + x + c] |= v;
  }
  // 꼭지 (+극)
  for (int c = 0; c < 2 && x + BODY + c < 128; c++)
    oledBuf[page * 128 + x + BODY + c] |= 0x1C;
}

// 실제 그리기 (게이팅 없음 — 호출자가 안전한 시점을 보장할 것)
void oledRender() {
  oledBusTake();
  // 매번 풀 재초기화: LED 토글이 만든 쓰레기 I2C가 표시 '설정'을 깨뜨렸어도 복구되게
  static const uint8_t seq[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
    0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF };
  for (uint8_t i = 0; i < sizeof(seq); i++) oledCmd(seq[i]);

  memset(oledBuf, 0, sizeof(oledBuf));
  const char* name = ownerRec.valid ? ownerRec.name : "POV WAND";
  int w = oledTextWidth(name, 2);
  oledText(w < 128 ? (128 - w) / 2 : 0, 1, name, 2);
  if (ownerRec.valid) {
    w = oledTextWidth(ownerRec.serial, 1);
    oledText(w < 128 ? (128 - w) / 2 : 0, 4, ownerRec.serial, 1);
  }
  int pct = readBattery();
  oledBattIcon(4, 6, pct);
  char line[8];
  snprintf(line, sizeof(line), "%d%%", pct);
  oledText(28, 6, line, 1);   // 아이콘 옆 숫자 (빼고 싶으면 이 두 줄 삭제)
  // BLE 상태 아이콘 (오른쪽 정렬): 전파=대기, BT+…=페어링중, BT+✓=연결됨
  if (!Bluefruit.connected()) {
    oledIcon(128 - 4 - 4, 6, ICO_WAVE, 4);
  } else {
    oledIcon(128 - 4 - 10, 6, ICO_BT, 4);
    oledIcon(128 - 4 - 5, 6, linkSecured ? ICO_CHECK : ICO_DOTS, 5);
  }
  oledShow();
  oledBusRelease();   // D4/D5를 LED로 반환
}

// 화면 갱신 게이팅: ①스윙/테스트/수동잔상 중 금지 ②끝난 직후 즉시 복구 ③대기 10초마다
void oledTick() {
  if (!oledOk) return;
  static uint32_t last = 0;
  static bool wasBusy = false;
  if (sw.swinging || testHold >= 0 || cfg.mode == MODE_POV) { wasBusy = true; return; }
  bool justStopped = wasBusy;
  wasBusy = false;
  if (!justStopped && last != 0 && millis() - last < 10000) return;
  if (justStopped) delay(50);   // 종료 직후 잔여 토글이 가라앉을 시간
  last = millis();
  oledRender();
}
#endif  // USE_OLED

// ---------------- LED 출력 ----------------
#if LED_TYPE == 3
// nRF52840 GPIO 구동 세기. 기본 S0S1은 약 2mA로 LED가 어둡고, H0H1은 약 9mA.
// 전원 여유를 의심할 때 low로 낮춰 전류를 1/4로 줄여볼 수 있다 (TEST drive low|high).
void setLedDrive(bool high) {
  for (int y = 0; y < NUM_LEDS; y++) {
    uint32_t g = g_ADigitalPinMap[LED_PINS[y]];
    NRF_GPIO_Type* port = g < 32 ? NRF_P0 : NRF_P1;
    port->PIN_CNF[g & 31] = (port->PIN_CNF[g & 31] & ~GPIO_PIN_CNF_DRIVE_Msk)
        | ((uint32_t)(high ? GPIO_PIN_CNF_DRIVE_H0H1 : GPIO_PIN_CNF_DRIVE_S0S1)
           << GPIO_PIN_CNF_DRIVE_Pos);
  }
}
#endif

#if LED_TYPE == 3
void restorePins();
// nRF52840에서 ADC로 읽을 수 있는 핀인가 (AIN0~AIN7 = P0.02/03/04/05/28/29/30/31)
bool isAnalogCapable(uint8_t d) {
  uint32_t g = g_ADigitalPinMap[d];
  return g == 2 || g == 3 || g == 4 || g == 5 || g == 28 || g == 29 || g == 30 || g == 31;
}

// 배선 진단 (멀티미터 없이 하는 도통·다이오드 시험).
// 핀을 내부 풀 저항(약 13k) 입력으로 바꾼 뒤 SAADC로 패드 전압을 읽는다.
// analogRead()는 PIN_CNF를 건드리지 않고 RESP_Bypass로 읽으므로 풀 설정이 그대로 유지된다.
//
// 풀업(PROBE 또는 PROBE up) 판독:
//   ~3300mV  아무것도 달려있지 않음 = 단선/미연결
//   ~1500~2400mV  LED가 GND 쪽으로 달려 있음 (미세전류에서의 순방향 강하)
//   ~0mV     GND로 쇼트
// 풀다운(PROBE down) 판독:
//   ~0mV     정상(부하 없음 또는 GND 쪽 LED)
//   높은 값  핀이 양전원 쪽에 물려 있음 (공통 애노드 배선 등)
void probePins(bool pullup) {
  analogReference(AR_INTERNAL);   // 0..3600mV
  analogReadResolution(12);
  for (int y = 0; y < NUM_LEDS; y++)
    pinMode(LED_PINS[y], pullup ? INPUT_PULLUP : INPUT_PULLDOWN);
  delay(30);                      // 풀 저항으로 패드가 안정될 시간

  for (int y = 0; y < NUM_LEDS; y++) {
    if (!isAnalogCapable(LED_PINS[y])) {
      replyf("PROBE ch%d D%d n/a", y, LED_PINS[y]);
    } else {
      uint32_t acc = 0;
      for (int k = 0; k < 4; k++) acc += analogRead(LED_PINS[y]);
      uint32_t mv = (acc / 4) * 3600 / 4095;
      replyf("PROBE ch%d D%d %lumV", y, LED_PINS[y], (unsigned long)mv);
    }
    delay(5);
  }

  restorePins();
  replyf("OK PROBE %s", pullup ? "up" : "down");
}

// 진단으로 건드린 핀들을 원래 역할로 되돌린다 (핀맵과 무관하게 동적으로)
bool isLedPin(uint8_t d) {
  for (int y = 0; y < NUM_LEDS; y++) if (LED_PINS[y] == d) return true;
  return false;
}
void restorePins() {
  for (uint8_t d = 0; d <= 10; d++) {
    if (isLedPin(d)) { pinMode(d, OUTPUT); digitalWrite(d, LOW); }
    else if (d == PIN_BUTTON) pinMode(d, INPUT_PULLUP);
    else pinMode(d, INPUT);
  }
  setLedDrive(true);
  pinMode(LED_BLUE, INPUT);
  pinMode(LED_GREEN, INPUT);
#if DEBUG_BEACON
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, !LED_STATE_ON);
#endif
}

// 전체 핀 훑기: LED가 D0~D6이 아닌 다른 핀에 붙어있는지 찾는다.
// 내장 LED 핀(LED_RED/GREEN/BLUE)은 확실히 LED가 달려 있으므로 양성 대조군 역할을 한다.
// 판독: up이 VDD에 가깝고 dn이 0이면 = 아무것도 안 달림.
//       dn이 높게 뜨면 = 양전원 쪽 부하(저항 풀업이면 ~2.4V, 다이오드면 더 낮게 클램프)
void probeSweep() {
  analogReference(AR_INTERNAL);
  analogReadResolution(12);
  static const uint8_t PROBE_PINS[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                        LED_RED, LED_BLUE, LED_GREEN };
  for (uint8_t i = 0; i < sizeof(PROBE_PINS); i++) {
    uint8_t d = PROBE_PINS[i];
    bool ana = isAnalogCapable(d);

    pinMode(d, INPUT_PULLUP);
    delay(15);
    int up = ana ? (int)((uint32_t)analogRead(d) * 3600 / 4095) : (digitalRead(d) ? 1 : 0);

    pinMode(d, INPUT_PULLDOWN);
    delay(15);
    int dn = ana ? (int)((uint32_t)analogRead(d) * 3600 / 4095) : (digitalRead(d) ? 1 : 0);

    if (ana) replyf("SW D%d up=%dmV dn=%dmV", d, up, dn);
    else     replyf("SW D%d up=%c dn=%c (no adc)", d, up ? 'H' : 'L', dn ? 'H' : 'L');
    delay(5);
  }
  restorePins();
  reply("OK PROBE sweep");
}
#endif

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
// 내장 LED 비콘. 보드 리비전마다 폴라리티가 달라서 토글로 처리 — 어느 쪽이든 깜빡인다
void beaconTick() {
#if DEBUG_BEACON
  static uint32_t last = 0;
  static bool on = false;
  if (millis() - last < 500) return;
  last = millis();
  on = !on;
  digitalWrite(LED_RED, on ? LED_STATE_ON : !LED_STATE_ON);
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
  if (oledOk) {
    // OLED가 있으면 대기 표시는 화면이 담당 — LED는 전부 소등 (사용자 결정).
    // I2C 라인에 파형이 전혀 없으므로 화면이 깨질 일도 없고 배터리도 아낀다.
    // 잔상(스윙) 중에만 7줄 전부 쓰고, 스윙이 끝나면 oledTick이 즉시 재초기화한다.
    for (int y = 0; y < NUM_LEDS; y++) digitalWrite(LED_PINS[y], LOW);
  } else {
    // OLED 없는 구성: 기존 물결 유지 (아래에서 위로, 한 번에 2~3개)
    static uint16_t phase = 0;
    phase++;
    for (int y = 0; y < NUM_LEDS; y++) {
      uint8_t d = (y + phase / 6) % NUM_LEDS;
      digitalWrite(LED_PINS[y], d < 3 ? HIGH : LOW);
    }
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
  BLEConnection* conn = Bluefruit.Connection(h);
  Serial.print("BLE connected, MTU=");
  Serial.print(conn ? conn->getMtu() : 0);
  // 실제 협상된 연결 인터벌(1.25ms 단위 → ms). 100ms를 넘으면 notify 재시도가 잦아진다
  Serial.print(" connInterval=");
  Serial.print(conn ? conn->getConnectionInterval() * 125 / 100 : 0);
  Serial.println("ms");
  Serial.flush();
#if REQUIRE_PAIRING
  linkSecured = false;
  // 연결 직후 바로 요청하면 일부 중앙장치가 거부하므로 잠시 뒤 loop에서 요청한다.
  // 본딩된 상대라면 이 요청이 재암호화를 유발해 다이얼로그 없이 secured로 넘어간다
  pairReqAt = millis() + 600;
#endif
}

// 암호화 성립 시 호출 (LESC 페어링 완료 또는 본딩 재암호화)
void onBleSecured(uint16_t h) {
  linkSecured = true;
  pairReqAt = 0;
  Serial.println("BLE link secured (encrypted)");
  Serial.flush();
}
void onBleDisconnect(uint16_t h, uint8_t reason) {
  // 주요 사유: 0x13 원격이 정상 종료, 0x08 신호 끊김(supervision timeout),
  // 0x3E 연결 수립 실패, 0x16 로컬 종료
  Serial.print("BLE disconnected, reason=0x");
  Serial.println(reason, HEX);
  Serial.flush();
  linkSecured = false;
  pairReqAt = 0;
}

// 광고 감시견: 연결이 없는데 광고도 꺼져 있으면 되살린다.
// restartOnDisconnect(true)만으로는 (본딩 도입 후) 재광고가 안 되는 사례가 실기기에서
// 확인됐다 — 끊고 나면 재부팅 전까지 스캔에 안 잡혔음. 코어 자동복구에 의존하지 않는다.
void advWatchdog() {
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();
  if (!Bluefruit.connected() && !Bluefruit.Advertising.isRunning()) {
    Serial.println("[adv] restarting advertising");
    Serial.flush();
    Bluefruit.Advertising.start(0);
  }
}

void bleRxCallback(uint16_t conn_hdl) {
  Serial.print("[cb] rx bytes now=");
  Serial.println(bleuart.available());
}

void setupBle() {
  // ★ begin() 앞에서만 유효. SoftDevice의 notify(HVN) 송신 큐를 1칸 → 8칸으로.
  // 큐가 1칸이면 코어가 빈 칸을 100ms만 기다리고 조용히 포기해서(BLE_GENERIC_TIMEOUT)
  // 연결 인터벌이 긴 중앙장치에선 긴 응답의 패킷이 격번으로 사라진다.
  // MTU는 기본값 23 유지 — 큐 버퍼 하나가 23바이트뿐이라 8칸이어도 RAM 부담이 없고,
  // 웹앱의 20바이트 청크 가정도 그대로 유지된다.
  Bluefruit.configPrphConn(BLE_GATT_ATT_MTU_DEFAULT,     // mtu_max = 23
                           6,                            // event_len 7.5ms (기본 3.75ms)
                           8,                            // hvn_qsize (notify 큐) ← 핵심
                           BLE_GATTC_WRITE_CMD_TX_QUEUE_SIZE_DEFAULT);
  Bluefruit.begin();
  // (주소 변조 핵 제거 — SoftDevice 가동 중 GAP 주소 변경이 수신 시 하드폴트 유발 의심)

  // 광고 이름에 주인 이름을 쓰므로 setName 전에 주인 정보를 읽어야 한다.
  // findOwner()는 Bluefruit.getAddr()을 쓰기 때문에 begin() 뒤여야 한다 — 이 순서가 유일한 정답
  findOwner();
  composeBleName();
  Bluefruit.setName(bleName);
  Serial.print("BLE name: "); Serial.println(bleName);
  Serial.flush();
  // 연결 인터벌 7.5~15ms 요청 (단위 1.25ms). 중앙장치가 긴 인터벌을 쓰면 notify
  // 한 패킷마다 그만큼 걸려서 긴 응답이 느리고 유실 위험도 커진다 (위 reply 주석 참고)
  Bluefruit.Periph.setConnInterval(6, 12);
  Bluefruit.Periph.setConnectCallback(onBleConnect);
  Bluefruit.Periph.setDisconnectCallback(onBleDisconnect);
#if REQUIRE_PAIRING
  Bluefruit.Security.setSecuredCallback(onBleSecured);
#endif
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
#if DEBUG_BEACON
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, !LED_STATE_ON);
#endif
#if USE_FS
  InternalFS.begin();
#endif
  loadConfig();
  // 모드는 저장값과 무관하게 항상 '자동'으로 부팅 (사용자 결정 2026-08-21).
  // pov/idle 강제 모드는 튜닝·시연용 세션 상태일 뿐 — 재부팅하면 장난감 기본 동작으로.
  // (밝기·속도·반전·시작슬롯은 계속 저장 유지)
  cfg.mode = MODE_AUTO;
#if LED_TYPE == 3
  for (int y = 0; y < NUM_LEDS; y++) {
    pinMode(LED_PINS[y], OUTPUT);
    digitalWrite(LED_PINS[y], LOW);
  }
  setLedDrive(true);   // 기본은 고구동(H0H1). TEST drive low 로 낮출 수 있다
#elif LED_TYPE
  strip.begin();
  strip.show();
#endif
  // 배터리 분압 활성 (상시 LOW — 누설 ~3µA)
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
#if USE_OLED
  oledOk = oledInit();
  Serial.print("OLED: "); Serial.println(oledOk ? "ok" : "not found");
  Serial.flush();
#endif
#if USE_IMU
  // 내장 LSM6DS3 (Wire1, 전원핀은 라이브러리 begin()이 켠다)
  imuOk = (imu.begin() == IMU_SUCCESS);
  Serial.print("IMU: "); Serial.println(imuOk ? "ok" : "INIT FAIL");
  Serial.flush();
  tapEngineInit();   // 하드웨어 더블탭 엔진 설정
#endif
  setupBle();   // 내부에서 findOwner() 까지 수행한다 (광고 이름에 필요)
  seedDefaultSlots();   // 최초 1회: 기본 메시지 3종을 슬롯에 (owner 확정 후여야 함)
  if (ownerRec.valid) {
    // 부팅 크레딧: 첫 스윙에 주인 이름이 뜬다. 버튼/BLE 조작 시 일반 콘텐츠로 전환
    char credit[80];
    snprintf(credit, sizeof(credit), "%s의 선물 %s", ownerRec.name, ownerRec.serial);
    ColorSpec cs; cs.type = ColorSpec::RAINBOW;
    renderText(credit, cs);
  } else if (!loadSlot(cfg.startSlot)) {
    ColorSpec cs; cs.type = ColorSpec::RAINBOW;
    renderText("HELLO", cs);   // 미등록 보드 + 빈 슬롯일 때 기본 콘텐츠
  }
#if USE_OLED
  // 부팅 첫 화면은 모드와 무관하게 무조건 그린다 (아직 LED 토글 전이라 항상 안전).
  // 이후 갱신은 oledTick 게이팅을 따른다 — 수동 잔상(pov) 모드에선 이 화면이 그대로 유지됨
  if (oledOk) oledRender();
#endif
  Serial.println("POV Wand ready");
}

void loop() {
  beaconTick();   // 내장 LED 깜빡임 = 펌웨어 생존 (외부 배선과 무관)

  // 디버그 하트비트: 루프 생존 + BLE 수신 FIFO 상태 (원인 확정 후 제거)
  static uint32_t hb = 0;
  if (millis() - hb > 5000) {
    hb = millis();
    Serial.print("[hb] up="); Serial.print(millis() / 1000);
    Serial.print("s conn="); Serial.print(Bluefruit.connected());
    Serial.print(" rxAvail="); Serial.print(bleuart.available());
#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && INCLUDE_uxTaskGetStackHighWaterMark
    // loop 태스크 스택 최저 잔량(워드). 코어 기본 스택은 1536워드(6KB)뿐이라
    // 이 값이 100워드 밑으로 떨어지면 스택오버플로 위험 = 큰 지역변수를 의심할 것
    Serial.print(" stackFreeWords="); Serial.print(uxTaskGetStackHighWaterMark(NULL));
#endif
    Serial.println();
    Serial.flush();
    if (Bluefruit.connected()) bleuart.print("EV HB\n");  // 보드→폰 방향 테스트
  }

  pollBle();
  advWatchdog();
  imuTick();
#if USE_OLED
  oledTick();
#endif

#if REQUIRE_PAIRING
  // 연결 0.6초 후 페어링(암호화) 요청 — 본딩돼 있으면 조용히 재암호화된다
  if (pairReqAt && millis() > pairReqAt) {
    pairReqAt = 0;
    BLEConnection* conn = Bluefruit.Connection(Bluefruit.connHandle());
    if (conn && conn->connected() && !conn->secured()) {
      Serial.println("[sec] requesting pairing");
      Serial.flush();
      conn->requestPairing();
    }
  }
#endif

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
      bool on = testHold == 1
                || (testHold == 2 && y == (chase / 15) % NUM_LEDS)
                || (testHold == 3 && y == testPin);
      digitalWrite(LED_PINS[y], on ? HIGH : LOW);
    }
    delay(20);
    return;
  }
#endif

  // 슬롯 전환: 톡톡(더블탭) 제스처 — 최종 완드에 물리 버튼 없음(사용자 결정).
  // D10 버튼 코드도 유지 (개발 지그에서 택트 물리면 그대로 동작)
  tapTick();
  static bool lastBtn = HIGH;
  bool btn = digitalRead(PIN_BUTTON);
  bool press = (lastBtn == HIGH && btn == LOW);
  lastBtn = btn;
  if (press || tapCycleReq) {
    tapCycleReq = false;
    uint8_t n = currentSlot;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
      n = n % MAX_SLOTS + 1;
      if (loadSlot(n)) break;
    }
    Serial.print("[tap] cycle -> slot "); Serial.println(currentSlot);
    Serial.flush();
    if (press) delay(30); // 임시 디바운스
  }

  if (cfg.mode == MODE_POV && content.cols > 0) {
    // 수동 잔상: SET SPEED 간격으로 반복하되 스윙에 반쯤 동기화한다.
    //  - 방향: 스트로크 단위로 안정된 sw.dir 우선 (왕복 양쪽에서 바로 보이게)
    //  - 방향이 바뀌면 프레임 즉시 중단 → 반쯤 반전된 얼룩 방지, 새 방향으로 재시작
    //  - 문장 사이 빈 틈(문장 폭의 1/3) → 반복 출력이 겹쳐 보이는 것 방지
    uint32_t colUs = cfg.speedUs ? cfg.speedUs : swingPeriodUs() / content.cols;
    int8_t fdir = sw.swinging ? sw.dir : povDir;
    bool rev = (fdir < 0) != (cfg.flip != 0);
    for (uint16_t c = 0; c < content.cols; c++) {
      outputColumn(rev ? content.cols - 1 - c : c);
      if (colUs >= 1000) delay(colUs / 1000);          // ms 단위는 RTOS 양보(yield)하는 delay로
      delayMicroseconds(colUs % 1000);
      pollBle();
      if ((c & 7) == 7) {                              // 8컬럼마다 방향 점검
        imuTick();
        int8_t nd = sw.swinging ? sw.dir : povDir;
        if (nd != fdir) break;                         // 스윙이 반전됨 → 프레임 중단
      }
    }
    ledsOff();
    // 문장 사이 빈 틈 (콘텐츠 폭의 1/3, 8~40컬럼 분량)
    uint16_t gapCols = constrain(content.cols / 3, 8, 40);
    for (uint16_t gc = 0; gc < gapCols; gc++) {
      if (colUs >= 1000) delay(colUs / 1000);
      delayMicroseconds(colUs % 1000);
      pollBle();
      imuTick();
    }
  } else if (cfg.mode == MODE_AUTO && sw.swinging && content.cols > 0) {
    // 자동 잔상: 방향 반전마다 한 번씩, 스트로크 시간의 70%에 맞춰 그린다
    if (sw.strokePending) {
      sw.strokePending = false;
      // 속도 슬라이더(SET SPEED)가 설정돼 있으면 그 고정 간격을 쓴다 (실측: 2~8ms가 최적).
      // 0(자동)일 때만 스트로크 길이에 비례해 계산
      uint32_t colUs = cfg.speedUs ? cfg.speedUs
                                   : (uint32_t)sw.strokeMs * 700 / content.cols;
      colUs = constrain(colUs, 200, 30000);
      // 스트로크 방향에 따라 자동 반전. cfg.flip은 기준(어느 쪽이 정방향인지) 뒤집기
      bool rev = (sw.dir < 0) != (cfg.flip != 0);
      for (uint16_t c = 0; c < content.cols; c++) {
        outputColumn(rev ? content.cols - 1 - c : c);
        if (colUs >= 1000) delay(colUs / 1000);
        delayMicroseconds(colUs % 1000);
        pollBle();
        imuTick();   // 그리는 중에도 다음 반전을 놓치지 않게
      }
      ledsOff();
    } else {
      delay(1);   // 다음 반전 대기 (IMU 샘플링은 loop 상단에서 계속)
    }
  } else if (cfg.mode != MODE_POV) {
    idleGlow();  // TODO: 일정 시간 후 슬립 + 흔들어 깨우기
  }
}

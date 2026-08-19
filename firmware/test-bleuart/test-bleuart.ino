// 최소 재현용: Bluefruit BLEUart 에코 (우리 코드 0%)
// 수신한 바이트를 시리얼에 찍고 그대로 폰에 되돌려준다
#include <bluefruit.h>

BLEUart bleuart;

void setup() {
  Serial.begin(115200);
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 3000; ) delay(10);
  Serial.println("minimal bleuart echo start");

  Bluefruit.begin();
  Bluefruit.setName("POV-TEST");
  bleuart.begin();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);
}

void loop() {
  static uint32_t hb = 0;
  if (millis() - hb > 3000) {
    hb = millis();
    Serial.print("[alive] "); Serial.println(millis() / 1000);
  }
  while (bleuart.available()) {
    char c = bleuart.read();
    Serial.print("rx: "); Serial.println(c);
    bleuart.write(c);   // 에코
  }
  delay(5);
}

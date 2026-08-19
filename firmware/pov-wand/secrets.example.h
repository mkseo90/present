// secrets.h 예시 — 이 파일을 secrets.h로 복사한 뒤 실제 값을 채우세요.
// secrets.h는 .gitignore에 있어 공개 저장소에 올라가지 않습니다.
#pragma once

// OTA 잠금 해제 PIN: 앱/터미널에서 "DFU <PIN>" 명령으로 부트로더 진입
#define DFU_PIN "0000"

// MAC → 주인 매핑 (부팅 시 시리얼에 찍히는 MAC을 등록. 이름에 공백 금지)
struct Owner { const char* mac; const char* name; const char* serial; };
static const Owner OWNERS[] = {
  { "00:00:00:00:00:00", "주인공", "No.001/001" },
};

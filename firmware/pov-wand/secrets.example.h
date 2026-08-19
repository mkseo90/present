// secrets.h 예시 — 이 파일을 secrets.h로 복사한 뒤 실제 값을 채우세요.
// secrets.h는 .gitignore에 있어 공개 저장소에 올라가지 않습니다.
#pragma once

// OTA 잠금 해제 PIN: 앱/터미널에서 "DFU <PIN>" 명령으로 부트로더 진입
// 반드시 길고 추측 어려운 문구로 바꿀 것 (공백 없는 아무 문자열, 예: 단어 3개 조합).
// 짧은 숫자 PIN은 무차별 대입에 약함 — 5회 실패 시 재부팅 전까지 잠기지만, 길이가 근본 방어임
#define DFU_PIN "change-me-to-long-phrase"

// MAC → 주인 매핑. 부팅 시 시리얼에 찍히는 MAC(예: "MAC: AA:BB:CC:DD:EE:FF")을 등록한다.
//
// 전체를 적지 않아도 된다 — 뒷부분만 적으면 그것으로 일치를 판정한다(최소 2글자).
// 콜론 단위로 끊는 것을 권장 (시리얼 출력에서 그대로 잘라 붙일 수 있고 오타가 적다).
//   { "EE:FF",             "이름", "No.001/001" }   // 뒤 2옥텟
//   { "DD:EE:FF",          "이름", "No.001/001" }   // 뒤 3옥텟 (권장)
//   { "AA:BB:CC:DD:EE:FF", "이름", "No.001/001" }   // 전체
//
// 이름에는 공백을 넣지 않는다 (INFO 응답이 공백으로 토큰을 구분하므로).
//
// color: 그 보드에 실제로 달린 LED 색 (RRGGBB 6자리 hex).
//   단색 완드는 펌웨어가 색을 쓰지 않는다(digitalWrite 켜짐/꺼짐뿐). 이 값은
//   INFO로 앱에 전달되어 **앱의 미리보기 색**을 실제 하드웨어와 맞추는 데 쓰인다.
//   보드마다 다른 색 LED를 달 예정이므로 MAC과 함께 여기에 기억해 둔다.
//   예) 초록 00FF66 · 파랑 3399FF · 분홍 FF4FA0 · 노랑 FFD644 · 흰색 FFFFFF
struct Owner { const char* mac; const char* name; const char* serial; const char* color; };
static const Owner OWNERS[] = {
  { "00:00:00:00:00:00", "주인공", "No.001/001", "00FF66" },
};

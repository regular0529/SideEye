# PDR: SideEye (사이드아이)

**주제**: 초경량 엣지 IMU 조향 의도 감지 + 비전 AI 기반 모빌리티 후측방 경보 웨어러블
**결과**: 2026 웨어러블 AI 엣지 컴퓨팅 프로젝트 **1등 수상작**

> 부산대 웨어러블 AI 엣지 컴퓨팅 특강 팀 프로젝트. 이 문서는 최종 제출 기준 설계 근거와 결정 사항을 담는다. 정확한 재현 방법(배선/업로드/모델)은 `REPRODUCTION.md` 참조.

## 1. 한 줄 정의

자전거·킥보드 헬멧 좌우에 XIAO ESP32S3 Sense를 하나씩 부착해, IMU로 몸의 기울임(조향 의도)을 감지하는 즉시 방향지시등을 켜고, 감지된 쪽 카메라로 차량 유무를 확인해 위험하면 부저로 경고하는 **엣지 우선** 웨어러블. 인터넷·폰 연결 없이도 항상 완결 동작하는 것이 최우선 원칙.

## 2. 핵심 원칙: 턴시그널과 위험경보는 서로 다른 트리거로 분리된 별개 출력

```
[마스터 보드] BNO055 IMU 상시 분류 (idle / left / right / stop / helmet_on), 50Hz
   │
   ├─ idle/stop/helmet_on → 각자 표시(3절), 통신 없음
   │
   └─ left/right (신뢰도 ≥0.75, IMU 편향 보정 반영 — REPRODUCTION.md 5절)
         │
         ├─ [즉시, 무조건] 네오픽셀 턴시그널 점등 — 차량 감지 여부와 무관, 실제 방향지시등과 동일 개념
         │
         └─ [병행] 카메라 캐스케이드 트리거
               ├─ right(마스터 자신 쪽) → 마스터에 카메라 모델이 없어(REPRODUCTION.md 3절 참조) 스텁
               └─ left(슬레이브 쪽) → ESPNOW CMD_CAPTURE_REQUEST → 슬레이브가 카메라 캡처+분류 → CMD_RESULT_REPLY 회신
               │
               └─ vehicleFound == true인 경우만 → 마스터 부저 요란하게 경보(턴시그널 지속시간과 맞춤)
```

**불변 조건**: 카메라는 트리거 전까지 상시 꺼져있다(상시 촬영 금지 — 배터리/캐스케이드 원칙의 근본 이유). 부저는 항상 마스터에서만 울린다. 네오픽셀은 마스터에만 있다.

## 3. 확정된 최종 스펙

| 항목 | 확정 내용 | 사유 |
|---|---|---|
| IMU 클래스 | 5클래스(`idle`/`left`/`right`/`stop`/`helmet_on`) | 방향 감지 외에 정지등·착용 확인까지 하나의 모델로 확장 |
| 카메라 클래스 | 2클래스(`vehicle`/`background`), **슬레이브만 탑재** | 마스터에 동시 탑재 시 EI 라이브러리 심볼 충돌 발생 — REPRODUCTION.md 3절 |
| 턴시그널 | 절반 링 앰버(`255,191,0`) 5회 점멸(300ms on/off), 픽셀 0/6 제외 | 실제 자동차 방향지시등 색상·점멸 주기 재현 |
| 정지등 | 풀링 빨강 5회 점멸 | 좌측 턴시그널과 색 구분, 브레이크등 관례 |
| 헬멧착용 확인 | 풀링 초록 순차 채움 애니메이션(신뢰도 ≥0.85) | 착용 여부를 시각적으로 명확히 피드백 |
| 차량감지 경보 | 부저 2500Hz, 좌측에서 실제 vehicleFound==true일 때만, 요란하게 3초 | 우측은 실제 카메라가 없어 가짜 알림을 울리지 않음(정직성 원칙) |
| 통신 두절 알람 | 부팅 시 1회만 체크, 실패 시만 1000Hz 3연타 | 주행 중 매번 알람 울리면 사용자가 무시하게 됨 — REPRODUCTION.md 3절 참조 |

## 4. 하드웨어 구성

| 보드 | 위치 | 부착 부품 |
|---|---|---|
| 마스터 | 헬멧 우측 | XIAO ESP32S3 Sense, BNO055(IMU, I2C: SDA=D4/GPIO5, SCL=D5/GPIO6), 부저(D0/GPIO1, LEDC PWM 구동), 네오픽셀 링(WCMCU-2812B-12, 12구, D1/GPIO2) |
| 슬레이브 | 헬멧 좌측 | XIAO ESP32S3 Sense, 내장 카메라(OV2640)만 사용 |

정확한 배선/전원 공유/안테나 주의사항은 `WIRING.md`, 보드별 업로드 커맨드는 `REPRODUCTION.md` 참조.

## 5. 모듈 간 통신 계약

```cpp
constexpr uint32_t SIDEEYE_MSG_MAGIC = 0x53454331;
constexpr uint8_t SIDEEYE_MSG_VERSION = 1;
constexpr uint8_t CMD_CAPTURE_REQUEST = 1;
constexpr uint8_t CMD_RESULT_REPLY = 2;

struct __attribute__((packed)) EspNowMessage {
  uint32_t magic;
  uint8_t version;
  uint8_t command;
  uint8_t vehicleFound;  // CMD_RESULT_REPLY에서만 유효
  uint32_t sequence;
};
```

원본: `firmware/shared/protocol.h`. 시퀀스번호 매칭 + 타임아웃 1회 재시도 페일세이프 포함(REPRODUCTION.md, `firmware/master/sideeye_master/sideeye_master.ino` 참조).

## 6. 검증된 수치

- ESPNOW 왕복 지연(슬레이브 실제 카메라 캡처+분류 포함): **166ms** (목표 <1000ms 대비 여유)
- 비전 모델 정확도: 약 72% (검증셋 불균형·저품질로 인한 한계, 정직하게 보고)

## 7. 남은 과제 (발표 이후)

- 마스터 우측 실제 비전(EI 심볼 충돌 해결 필요)
- 헬멧 착용/미착용 게이팅(착용 전엔 시스템 대기)
- 절전모드(deep/light sleep) — 설계만 있고 미구현
- 배터리 단독 구동 테스트
- BLE 폰 연동 — 설계만 있고 미구현

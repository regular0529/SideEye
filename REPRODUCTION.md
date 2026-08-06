# SideEye — 재현 가이드

자전거·킥보드 헬멧 좌우에 XIAO ESP32S3 Sense를 하나씩 달아, IMU로 좌/우 기울임(차선변경 의도)을 감지하면 즉시 방향지시등을 켜고, 해당 방향 카메라로 차량 유무를 확인해 위험하면 부저로 경고하는 엣지 우선 웨어러블. 2026 웨어러블 AI 엣지 컴퓨팅 프로젝트 **1등 수상작**.

이 문서 하나로 하드웨어 상태, 아키텍처, 보드별 업로드 파일, 학습된 모델까지 재현에 필요한 전부를 담는다. 세부 설계 배경은 `PDR.md`, 배선은 `WIRING.md` 참고.

## 1. 하드웨어 구성

| 역할 | 장착 위치 | 보드 | 센서/출력 |
|---|---|---|---|
| 마스터 | 헬멧 우측 | XIAO ESP32S3 Sense | BNO055 IMU(I2C: SDA=D4/GPIO5, SCL=D5/GPIO6), NeoPixel 링(WCMCU-2812B-12, D1/GPIO2), 부저(D0/GPIO1, 피에조) |
| 슬레이브 | 헬멧 좌측 | XIAO ESP32S3 Sense | 내장 카메라(OV2640)만 사용, 내장 LED는 디버깅용 |

전원: 마스터 USB-C(또는 보조배터리) → 5V/GND를 슬레이브와 공통 연결(슬레이브는 자체 USB 없이 마스터에서 전원만 받아도 동작). ESPNOW는 WiFi 라디오 직통이라 USB 연결 여부와 무관하게 동작한다.

**중요 — 안테나**: 보드에 외부 안테나 커넥터가 있다면 반드시 꽂을 것. 안테나 없이도 `esp_now_send()`는 `ESP_OK`를 반환하지만(큐잉 성공 == 실제 수신 아님) 실제로는 전파가 안 나가 상대가 영원히 못 받는다 — 이번 프로젝트에서 겪은 가장 오래 걸린 버그였다.

**NeoPixel 배선 주의**: 저항은 5V 라인이 아니라 신호선(DIN)에만 넣을 것. 5V 라인에 넣으면 공급 전압이 3.4V까지 떨어져 오작동한다.

## 2. 아키텍처

```
[마스터] BNO055 상시 저전력 폴링(50Hz, 5샘플/윈도우=100ms)
   │
   ├─ idle/stop/helmet_on → 해당 표시만 (아래 4절), 통신 없음
   │
   └─ left/right (신뢰도 ≥0.75) → 네오픽셀 5회 점멸(300ms on/off, 자동차 방향지시등 패턴)
         ├─ right(마스터 자신 쪽) → 이 보드엔 카메라 모델이 없어(3절 참고) 항상 "차량있음" 취급, 부저 없음
         └─ left(슬레이브 쪽) → ESPNOW CMD_CAPTURE_REQUEST 전송
                                   └─ 슬레이브: 카메라 캡처 → 비전 분류 → CMD_RESULT_REPLY 회신
                                        └─ vehicleFound==true → 마스터 부저 3초간 요란하게 경보
```

## 3. 왜 마스터에 비전이 없는가 (중요한 설계 제약)

원래 설계는 마스터도 자체 카메라로 우측 차량을 확인하는 것이었으나, **Edge Impulse가 내보낸 두 개의 서로 다른 Arduino 라이브러리(IMU 모델 + 비전 모델)를 한 스케치에 동시에 링크할 수 없다**는 걸 확인했다:

1. 두 라이브러리 모두 `model-parameters/model_metadata.h`라는 동일한 상대 경로 + 동일한 include guard(`_EI_CLASSIFIER_MODEL_METADATA_H_`)를 쓴다. Arduino 빌드가 라이브러리 include 경로를 스케치 전체에 대해 하나로 합치기 때문에, 어느 쪽이 먼저 잡히느냐에 따라 다른 쪽 매크로(`EI_CLASSIFIER_RAW_SAMPLE_COUNT` 등)까지 덮어써진다 (`static_assert`로 실증 확인).
2. 이 문제를 파일 분리로 우회해도, 두 라이브러리에 내장된 TFLite Micro 런타임 오브젝트(`.a`)가 동일 심볼명으로 충돌해 링커가 `--allow-multiple-definition`로 강제로 하나만 골라 쓰게 되고, 이게 실제 추론 시 메모리 손상(Guru Meditation)으로 이어진다.

시간 제약상 바이너리 심볼 리네이밍 같은 근본 수정 대신 **마스터=IMU 전용, 슬레이브=비전 전용**으로 역할을 분리해 우회했다. 마스터 우측 차량감지는 스텁(항상 true)이다. 두 EI 모델을 한 보드에서 동시에 돌리려면 다음 중 하나가 필요하다:
- Edge Impulse Studio에서 IMU+비전을 하나의 프로젝트 안 "멀티 임펄스"로 구성 후 C++ 라이브러리로 재export
- 두 라이브러리 중 하나의 정적 라이브러리(`.a`)에 `objcopy --redefine-sym`으로 충돌 심볼 전부 리네이밍

## 4. 네오픽셀 색상/사운드 규약

| 상태 | 네오픽셀 | 부저 |
|---|---|---|
| left/right (턴시그널) | 절반 링 앰버(255,191,0) 5회 점멸, 픽셀 0/6 제외 | 없음(활성화 칩은 제거함) |
| left + 차량감지(vehicleFound) | 위와 동일 | 2500Hz, 3초간 요란하게 (턴시그널 지속시간과 맞춤) |
| stop (신뢰도 무관) | 풀링 빨강(255,0,0) 5회 점멸 | 없음 |
| helmet_on (신뢰도 ≥0.85) | 풀링 초록(0,255,0) 순차 채움 애니메이션 → 유지 → 소등 | 없음 |
| ESPNOW 통신 완전 두절(부팅 시 1회만 체크) | — | 1000Hz 3연타 |

부저는 수동 피에조라 `tone()` 대신 LEDC PWM으로 직접 구동해야 볼륨 조절이 된다. **듀티 50%(128/255)가 최대 음량** — 100%(255/255)는 사각파가 아니라 DC 상태라 오히려 거의 무음이 된다.

## 5. IMU 모델 특이사항

`models/imu_5class.zip` (idle/left/right/stop/helmet_on 5클래스)에는 실제로 `background`/`vehicle` 라벨도 섞여 7클래스로 나온다 — 같은 Edge Impulse 프로젝트(1079933)에 초기 비전 실험(CNN 실험, 이후 폐기) 데이터가 섞여 들어간 흔적. 코드에서 `left`/`right`/`stop`/`helmet_on`만 보고 나머지는 무시하므로 동작엔 지장 없으나, 재학습 시 새 프로젝트로 분리 권장.

추가로 실기 테스트에서 `left` 라벨이 과민하게 튀는 편향이 있어, 코드에서 `left` 점수에 -0.15 페널티를 준 뒤 argmax 하도록 보정했다(`firmware/master/sideeye_master/sideeye_master.ino`의 `LEFT_BIAS_PENALTY`). 또한 `left`/`right` 모두 신뢰도 0.75 미만이면 무시하도록 임계값을 걸었다(`TURN_SIGNAL_MIN_CONFIDENCE`) — 그 이하는 잡음성 오탐이었다.

## 5.1 비전 모델(Edge Impulse Arduino 라이브러리) 온디바이스 필수 수정

`models/vision_vehicle_detection.zip`을 재생성하거나 라이브러리를 다시 받을 일이 있으면, ESP32-S3에서 그대로 돌아가지 않으므로 아래 수정이 필요하다 (현재 `firmware/slave/sideeye_slave/sideeye_slave.ino`에는 이미 반영되어 있음):

1. **텐서 아레나 오버플로 방지**: 라이브러리의 `src/edge-impulse-sdk/porting/ei_classifier_porting.h`에서 `EI_MAX_OVERFLOW_BUFFER_COUNT`를 `30` → `2048`로 변경.
2. **아레나를 PSRAM에 할당** (스케치에 이미 있음):
   ```cpp
   #include "esp_heap_caps.h"
   void *ei_malloc(size_t size) {
     void *p = heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM);
     if (!p) p = heap_caps_aligned_alloc(16, size, MALLOC_CAP_DEFAULT);
     return p;
   }
   void *ei_calloc(size_t n, size_t s) { void *p = ei_malloc(n*s); if (p) memset(p,0,n*s); return p; }
   void ei_free(void *ptr) { heap_caps_free(ptr); }
   ```
3. **컴파일/업로드 둘 다 `PSRAM=opi` 필수** (6.3절 커맨드에 이미 포함됨).
4. **카메라 프레임 패킹**: 픽셀당 float 하나로 `(r<<16)|(g<<8)|b`.
5. **XIAO Sense 카메라는 90도 회전돼서 나옴** — 다운스케일하면서 시계방향 회전 보정 필요 (`sx = y*W/H; sy = H-1-(x*H/W)`). 분류 정확도가 이상하면 회전없음/CW/CCW 세 가지로 직접 테스트해서 방향을 확인할 것.
6. 같은 이름으로 라이브러리를 재설치하는 것이면 **첫 컴파일에 `--clean` 필수** (안 하면 `objs.a ... is not an object` 링크 에러).

## 6. 보드별 업로드

### 6.1 사전 준비

```powershell
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit BNO055" "Adafruit Unified Sensor" "Adafruit BusIO"
```

`models/imu_5class.zip`, `models/vision_vehicle_detection.zip`을 각각 압축 해제해 `<sketchbook>/libraries/SideEYE_inferencing/`, `<sketchbook>/libraries/SideEYEVision_inferencing/`에 넣는다 (`arduino-cli config get directories.user`로 sketchbook 경로 확인).

### 6.2 마스터 (`firmware/master/sideeye_master/`)

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 firmware/master/sideeye_master
arduino-cli upload -p <마스터_COM_PORT> --fqbn esp32:esp32:XIAO_ESP32S3 firmware/master/sideeye_master
```

PSRAM 옵션 불필요(비전 모델을 안 쓰므로).

### 6.3 슬레이브 (`firmware/slave/sideeye_slave/`)

```powershell
arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi" firmware/slave/sideeye_slave
arduino-cli upload -p <슬레이브_COM_PORT> --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi" firmware/slave/sideeye_slave
```

`PSRAM=opi` 필수 — 카메라 프레임버퍼 + 텐서 아레나가 PSRAM에 할당됨.

### 6.4 MAC 주소

두 코드 모두 상대방 MAC이 하드코딩돼 있다(`slaveMac[]` / `masterMac[]`). 보드를 바꾸거나 포트가 재배정되면 **물리적 보드가 그대로여도 MAC은 안 바뀌므로 재측정 불필요** — 다만 처음 페어링할 땐 각 보드에 `WiFi.macAddress()`를 출력하는 임시 스케치로 실측해서 상대방 코드에 입력해야 한다. ESPNOW는 두 보드가 같은 WiFi 채널에 있어야 하며(`ESPNOW_WIFI_CHANNEL = 0`, AP 미접속 시 보통 채널 1로 정착), `esp_now_add_peer()` 반환값과 `WiFi.channel()`을 부팅 로그로 확인하는 습관을 들일 것.

## 7. 검증된 사실

- ESPNOW 왕복 지연: 166ms (좌측 실제 카메라 캡처+분류 포함, 목표 <1000ms 충족)
- 비전 모델 정확도: 약 72% (검증셋 불균형·저품질로 인한 알려진 한계, `vision_harness`에 재학습 스크립트 있었으나 이번 정리에서 제거 — Edge Impulse 프로젝트 1080527에 원본 데이터 보존됨)
- NeoPixel: arduino-esp32 core 3.2.0의 `Adafruit_NeoPixel`이 RMT `allow_pd` 플래그를 초기화 안 해 완전 먹통이 되는 버그 있음 → `driver/rmt_tx.h` 직접 사용으로 우회(코드에 이미 반영됨)

## 8. 남은 과제 (발표 이후)

- 마스터 우측 실제 비전 (3절의 심볼 충돌 해결 필요)
- 헬멧 착용/미착용 게이팅(착용 전엔 시스템 대기)
- 절전모드(deep/light sleep)
- 배터리 단독 구동 테스트
- BLE 폰 연동(설계만 있고 미구현, 팀원 핸드오프 스펙은 폐기함)

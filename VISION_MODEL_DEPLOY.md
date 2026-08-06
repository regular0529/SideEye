# SideEye 비전 모델 배포 가이드 (vehicle / background)

이 문서는 `SideEYEVision`(Edge Impulse 프로젝트 ID `1080527`) 이미지 분류
모델을 XIAO ESP32-S3 Sense에 실제로 올려서 쓰는 방법을 설명한다.

## 1. 이번에 한 일

- `dataset/vehicle_source/`, `dataset/background_source/`: Wikimedia
  Commons에서 자동 다운로드한 학습용 원본 사진 (현대/기아/제네시스 차량
  정면, 차 없는 도로/주차장 등 배경). `dataset/fetch_vehicle_photos.py`,
  `dataset/fetch_background_photos.py`로 재실행 가능.
- `firmware/vision_collector/vision_collector.ino`: XIAO ESP32-S3 Sense가
  WiFi `projectbee`에 STA로 접속해 `/`(수집 웹페이지)와 `/jpg`(카메라 프레
  임)를 서빙. 웹페이지에서 라벨 입력 후 `1장 캡처`로 실제 카메라로 찍은
  사진을 모음.
- `dataset/new_capture_vehicle/`, `dataset/new_capture_background/`: 위
  수집기로 실제 촬영한 사진 (vehicle 55장, background 105장).
- `vision_harness/train_sideeye_vision.py`: 위 사진들을 Edge Impulse
  프로젝트 `1080527`에 업로드하고, train/test 재분배 → 피처 생성 → 학습
  → Arduino 라이브러리 빌드까지 REST API로 자동 실행하는 스크립트.

## 2. 정확도 (정직하게 보고)

마지막 학습 결과, **validation accuracy 약 72%** (학습 도중 최고치는
80.3%, 이후 하락). 오탐/미탐이 꽤 있을 수 있는 수준이다. 원인으로 의심되는
것:

1. vehicle 55장 vs background 105장 — 약 2배 불균형
2. vehicle 사진 다수가 "모니터/폰 화면에 띄운 사진을 카메라로 재촬영"한
   것이라 반사광·모아레 패턴 등 실제 카메라 입력과 질감 차이가 있음
3. 학습 사이클 20회로 적은 편

실사용 전에 실제 도로 환경에서 정확도를 재확인하고, 필요하면 vehicle
데이터를 더 모아 background와 비율을 맞춰 재학습하는 것을 권장한다.

## 3. 학습 재실행 방법

```powershell
python vision_harness/train_sideeye_vision.py `
  --vehicle-dir dataset/new_capture_vehicle `
  --background-dir dataset/new_capture_background
```

`.env`의 `SideEYEVision_EI_API_KEY`를 사용한다 (project 1080527 전용,
admin 권한). 새 사진만 추가로 학습하고 싶으면 새 폴더를 만들어
`--vehicle-dir`/`--background-dir`로 지정하면 된다 (기존 학습 데이터는
Edge Impulse 프로젝트에 계속 누적됨).

실행이 끝나면 `vision_harness/sideeye_vision_arduino.zip`에 Arduino
라이브러리가 저장된다 (이 zip은 매번 재생성되는 빌드 산출물이라 git에는
커밋하지 않았다 — 필요하면 위 명령으로 다시 받으면 된다).

## 4. Arduino 라이브러리 설치

```powershell
arduino-cli config get directories.user   # sketchbook 경로 확인
```

`vision_harness/sideeye_vision_arduino.zip`을
`<sketchbook>\libraries\SideEYEVision_inferencing`에 압축 해제한다. zip이
폴더 한 겹으로 감싸져 있으면 벗겨서 `src\SideEYEVision_inferencing.h`가
바로 보이는 구조로 만든다.

같은 이름으로 라이브러리를 재설치하는 것이면 **첫 컴파일에 `--clean`
필수** (안 하면 `objs.a ... is not an object` 링크 에러).

## 5. 온디바이스 필수 수정 (ESP32-S3)

1. **텐서 아레나 오버플로 방지**:
   `src/edge-impulse-sdk/porting/ei_classifier_porting.h`에서
   `EI_MAX_OVERFLOW_BUFFER_COUNT`를 `30` → `2048`로 변경.
2. **아레나를 PSRAM에 할당** (스케치에 추가):

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

3. **컴파일/업로드 둘 다 `--board-options PSRAM=opi` 필수**.
4. **카메라 프레임 패킹**: 픽셀당 float 하나로 `(r<<16)|(g<<8)|b`.
5. **XIAO Sense 카메라는 90도 회전돼서 나옴** — 다운스케일하면서 시계방향
   회전 보정 필요 (`sx = y*W/H; sy = H-1-(x*H/W)`). 분류 정확도가 이상하면
   회전없음/CW/CCW 세 가지로 직접 테스트해서 방향을 확인할 것.

## 6. 테스트 체크리스트

- `firmware/slave/`(카메라 담당 보드)에 통합
- 실제 차량을 비췄을 때 `vehicle`로, 빈 도로/배경을 비췄을 때
  `background`로 정확히 분류되는지 확인 (특히 차 없는 도로에서 오탐 여부)
- 추론 시간(ms)을 로그로 남겨서 PDR 180ms 기준치와 비교
- 결과(정확도, 추론시간, 스크린샷/영상)를 개발 담당자에게 공유하거나
  `firmware/slave/`에 커밋 + push

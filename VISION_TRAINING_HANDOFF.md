# SideEye 비전 학습 지시서 (LLM 실행용)

팀원의 LLM 코딩 어시스턴트(Claude/GPT 등)에게 이 파일을 통째로 주고 "이대로 임펄스 생성→학습→Arduino 라이브러리 다운로드→ESP32 업로드→테스트까지 진행해줘"라고 시키면 됨. Studio 웹사이트 로그인 필요 없음 — 전부 REST API로 함.

## 0. 사전 정보

- 프로젝트 ID: `1079933`
- API 키: `.env`의 `SideEYE_EI_API_KEY` (관리자 권한, 임펄스 생성/학습 가능)
- 클래스: `vehicle`, `background` (각 20장, 라벨 정리 완료 — 건드리지 마라)
- 같은 프로젝트에 IMU 데이터(`idle`/`left`/`right`)도 있음. 비전 임펄스는 Image 블록만 쓰니까 자동으로 IMU 데이터는 무시됨 — 신경 안 써도 됨
- `edge-impulse-cli` 설치하지 마라(Node/Windows에서 빌드 실패). REST API만 쓴다

참고 스킬: `.claude/skills/xiao-edgeimpulse-train/SKILL.md` (검증된 REST API 워크플로 전체 기록됨)

## 1. 임펄스 생성

```
POST https://studio.edgeimpulse.com/v1/api/1079933/impulse
헤더: x-api-key: <SideEYE_EI_API_KEY>
바디:
{
  "inputBlocks": [{"id":1,"type":"image","name":"Images","title":"Image data","imageWidth":96,"imageHeight":96,"resizeMode":"squash"}],
  "dspBlocks": [{"id":2,"type":"image","name":"Image","axes":["image"],"title":"Image","implementationVersion":1}],
  "learnBlocks": [{"id":3,"type":"keras-transfer-image","name":"Transfer learning","dsp":[2],"title":"Transfer learning (Images)"}]
}
```

## 2. 피처 생성

```
POST https://studio.edgeimpulse.com/v1/api/1079933/jobs/generate-features
바디: {"dspId":2,"calculateFeatureImportance":false}
```

`id`(job id) 받아서 폴링:

```
GET https://studio.edgeimpulse.com/v1/api/1079933/jobs/{jobId}/status
```

`job.finished`가 찍힐 때까지 3~5초 간격으로 반복.

## 3. 학습

```
POST https://studio.edgeimpulse.com/v1/api/1079933/jobs/train/keras/3
바디: {"trainingCycles":20,"learningRate":0.0005}
```

같은 방식으로 job 폴링. 끝나면:

```
GET https://studio.edgeimpulse.com/v1/api/1079933/jobs/{jobId}/stdout
```

에서 `val_accuracy` 값 확인하고 사용자에게 정직하게 보고할 것(과장 금지).

## 4. Arduino 라이브러리 빌드 + 다운로드

```
POST https://studio.edgeimpulse.com/v1/api/1079933/jobs/build-ondevice-model?type=arduino
바디: {"engine":"tflite-eon"}
```

job 폴링 후:

```
GET https://studio.edgeimpulse.com/v1/api/1079933/deployment/download?type=arduino
```

응답을 zip 파일로 저장.

## 5. 설치 + 온디바이스 수정 (필수)

1. `arduino-cli config get directories.user`로 sketchbook 경로 확인, zip을 `<sketchbook>/libraries/<project>_inferencing`에 압축 해제(폴더 한 겹 중첩돼있으면 벗겨서 `src/<project>_inferencing.h`가 바로 보이게)
2. 같은 라이브러리 이름으로 재설치하는 거면 **첫 컴파일에 `--clean` 필수** (안 하면 `objs.a ... is not an object` 에러)
3. 텐서 아레나 오버플로 크래시 방지: `src/edge-impulse-sdk/porting/ei_classifier_porting.h`에서 `EI_MAX_OVERFLOW_BUFFER_COUNT`를 `30`→`2048`로 변경
4. 아레나를 PSRAM에 할당(스케치에 추가):

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

5. 컴파일/업로드 둘 다 `--board-options PSRAM=opi` 필수
6. 카메라 프레임을 `(r<<16)|(g<<8)|b` 한 픽셀당 float 하나로 패킹해서 넣을 것
7. XIAO Sense 카메라는 90도 회전돼서 나옴 — 다운스케일하면서 시계방향 회전 보정 필요(`sx = y*W/H; sy = H-1-(x*H/W)`), 정확도 이상하면 방향 3가지(회전없음/CW/CCW)로 직접 분류 테스트해서 확인

## 6. 테스트

- 슬레이브 보드(카메라 담당, `firmware/slave/`)에 통합
- 실제 차량 사진/영상 보여주면서 `vehicle` 분류가 맞게 나오는지, `background`(빈 배경)일 때 오탐 없는지 확인
- 추론 시간(ms)도 로그로 남겨서 PDR의 180ms 기준치와 비교
- 결과(정확도, 추론시간, 스크린샷/영상)를 나(개발 담당)한테 공유하거나 GitHub `firmware/slave/`에 커밋+push

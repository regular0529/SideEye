# SideEye 비전 데이터셋 자동 수집 — 새 세션 실행용 프롬프트

아래 내용을 새 Claude Code 세션에 그대로 붙여넣으면 된다.

---

SideEye 프로젝트의 비전(차량 감지) 학습 데이터셋을 `vehicle`/`background` 각 50장씩 자동으로 모으는 파이프라인을 만든다.

## 구조

```
컴퓨터: 차량 사진 슬라이드쇼 자동 재생 (일정 간격)
ESP32(XIAO ESP32S3 Sense, AP 모드): 슬라이드 전환 타이밍에 맞춰 자동 캡처
휴대폰: 같은 AP에 접속해서 브라우저로 촬영 잘 되는지 실시간 감시만 함
```

- `vehicle` 50장: 컴퓨터 화면에 자동차 정면 사진을 순차 재생(예: 2초 간격), 그 타이밍에 맞춰 ESP32가 자동으로 캡처해서 `vehicle` 라벨로 저장
- `background` 50장: 화면 재생 없이, 카메라를 실제로 이리저리 돌려가며(벽·복도·책상·하늘 등) 일정 간격으로 자동 캡처해서 `background` 라벨로 저장 — 화면 사진보다 실물이 더 좋은 데이터라 슬라이드쇼 안 씀

## 참고할 기존 자료 (반드시 먼저 읽어라)

- `XIAO_WEBCAM_EDGE_IMPULSE_GUIDE.md` — 기존 카메라 수집기(`C:\arduinoTest\xiao_webcam_ap_collect\xiao_webcam_ap_collect.ino`) 구조. AP 모드(SSID `bkh`), 브라우저 `http://192.168.4.1`, `/jpg`로 프레임 가져오는 방식, 로컬 업로드 브리지(`edge_impulse_upload_proxy.py`) 구조가 이미 검증돼있다. 이 구조를 재사용/확장해라, 처음부터 새로 만들지 마라.
- `VISION_TRAINING_HANDOFF.md` — Edge Impulse REST API로 임펄스 만들고 학습하는 법(이건 사진 다 모은 다음 단계)
- `.claude/skills/xiao-esp32s3/SKILL.md` — arduino-cli 컴파일/업로드 기본
- `.env`의 `SideEYE_EI_API_KEY`, 프로젝트 ID `1079933`, 클래스는 `vehicle`/`background` (다른 이름 쓰지 마라 — 예전에 라벨 꼬였던 적 있음)

## 이미지 소스 (vehicle용 50장)

무료 라이선스 사이트에서 자동차 **정면(front view)** 사진을 구해라. Unsplash의 옛날 `source.unsplash.com` 리다이렉트 방식은 지금 죽어있다(503 확인됨) — 대신 다음 중 하나로:
- Pexels API(무료 API 키 발급, https://www.pexels.com/api/ ) 로 "car front view" 검색 후 다운로드
- Unsplash 공식 API(무료 API 키, https://unsplash.com/developers )로 검색 다운로드
- 안 되면 사용자에게 "API 키가 필요하다"고 알리고 직접 발급받게 하거나, 브라우저 자동화로 검색 결과 페이지에서 이미지 URL 추출 후 다운로드

받은 이미지는 `dataset/vehicle_source/`에 저장(원본 보존용, 나중에도 씀).

## 할 일

1. 차량 정면 사진 50장 확보 → `dataset/vehicle_source/`에 저장
2. 슬라이드쇼 HTML 페이지 작성 — 이미지 순차 자동 재생(2초 간격), 화면에 "N/50" 카운터 표시, 로컬 파일로 열 수 있게(`file://` 또는 간단한 로컬 서버)
3. `xiao_webcam_ap_collect.ino` 수정 — 슬라이드쇼와 타이밍 맞춰 자동 캡처하는 모드 추가 (기존 수동 "1장 캡처"/"20장 버스트" 버튼은 그대로 두고, "자동 캡처 시작(간격 2초, N장, 라벨 고정)" 기능 추가). 컴퓨터 브라우저가 슬라이드 넘길 때마다 ESP32의 `/jpg`를 호출해서 캡처하는 방식이 제일 간단하다 — 슬라이드쇼 페이지 자체에 이 fetch 로직을 넣어라.
4. `background`용: 같은 자동 캡처 기능을 슬라이드쇼 없이 그냥 타이머로만(2초 간격, 50장) 실행 — 사용자가 카메라를 들고 돌아다니면서 촬영
5. 컴파일 → 보드 업로드 → 실제로 컴퓨터는 차량 사진 슬라이드쇼 켜두고, 휴대폰은 같은 AP(`bkh`)에 붙어서 `http://192.168.4.1`로 촬영 상태 실시간 확인
6. 캡처된 사진들을 기존 `edge_impulse_upload_proxy.py` 브리지로 Edge Impulse에 업로드(라벨 정확히 확인)
7. 업로드 끝나면 Edge Impulse API로 카운트 확인(`vehicle` 50, `background` 50 정확히 들어갔는지) — `imu_harness/`에 있던 것과 비슷한 방식으로 REST API 직접 조회해서 확인해라, 짐작하지 마라
8. 완료되면 `firmware/`, `dataset/` 밑에 커밋하고 push (기존 checkpoint 파일들은 건드리지 마라)

막히면 짐작하지 말고 에러 로그 그대로 보여달라고 요청해라. AP 모드라 컴퓨터가 인터넷이 끊긴다 — 촬영 끝나면 인터넷 와이파이로 바꾸고 업로드해야 한다(기존 가이드 문서에 이미 나와있는 2단계 흐름).

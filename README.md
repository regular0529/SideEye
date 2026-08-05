# SideEye

부산대 웨어러블 AI 엣지 컴퓨팅 특강 팀 프로젝트. 자전거·킥보드 헬멧 사각지대 경고 시스템.

## 여기부터 보세요

**[`PDR_SideEye.md`](PDR_SideEye.md)** — 유일한 최신 지침 문서. 기능 스펙, 모듈 분담, 통신 계약, 로드맵/체크포인트가 전부 여기 있습니다. 다른 md 파일과 내용이 다르면 이 문서가 맞습니다.

## 진행 상황 (2026-08-05 기준)

- 체크포인트 1~4 완료(하드웨어, ESPNOW 통신, IMU 학습, 턴시그널+경보로직) — `PDR_SideEye.md` 8절 참조
- 남은 것: 체크포인트 5(비전 학습 — 차량 감지)
- Edge Impulse 프로젝트: https://studio.edgeimpulse.com/studio/1079933 (`vehicle`/`background` 2클래스, 다른 클래스명 쓰지 마세요)

## 폴더 구조

- `firmware/master/`, `firmware/slave/`, `firmware/shared/` — 보드별 펌웨어
- `imu_harness/` — IMU 데이터 수집·업로드 스크립트
- `WIRING_SideEye.md` — 실제 배선도
- `NEOPIXEL_DEBUGGING.md` — 네오픽셀 하드웨어 이슈 진단 기록
- `ESPNOW.md`, `ESPNOW_Peer_Test/` — ESPNOW 통신 참고자료
- `PROJECT_RECORD.md` — ⚠️ SideEye 이전 옛날 카메라 데모 기록(하드웨어 트러블슈팅 참고용일 뿐, 클래스명 등은 무시할 것)

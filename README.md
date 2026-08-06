# SideEye

부산대 웨어러블 AI 엣지 컴퓨팅 특강 팀 프로젝트 — 자전거·킥보드 헬멧 사각지대 경고 시스템.

**2026 웨어러블 AI 엣지 컴퓨팅 프로젝트 1등 수상작.**

## 여기부터 보세요

- **[`REPRODUCTION.md`](REPRODUCTION.md)** — 재현 가이드. 하드웨어 상태, 아키텍처, 보드별 업로드 파일, 학습된 모델까지 전부.
- **[`PDR_SideEye.md`](PDR_SideEye.md)** — 설계 배경/기능 스펙 원본 문서.
- **[`WIRING_SideEye.md`](WIRING_SideEye.md)** — 배선도.

## 폴더 구조

- `firmware/master/sideeye_master/` — 마스터 보드(헬멧 우측, IMU+턴시그널+ESPNOW) 최종 펌웨어
- `firmware/slave/sideeye_slave/` — 슬레이브 보드(헬멧 좌측, 카메라 비전) 최종 펌웨어
- `firmware/shared/` — 두 보드가 공유하는 프로토콜 헤더
- `models/` — 학습된 Edge Impulse 모델(IMU 5클래스, 비전 vehicle/background) Arduino 라이브러리 zip

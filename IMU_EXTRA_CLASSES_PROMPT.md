# SideEye IMU 추가 클래스 학습 — 새 세션 실행용 프롬프트

아래 내용을 새 Claude Code 세션에 그대로 붙여넣으면 된다. 같은 SideEye 저장소(`C:\Dev\WearableProjectEX`)를 열어놓은 상태에서 실행할 것 — 같은 프로젝트 파일을 공유한다.

---

SideEye 프로젝트의 IMU 분류에 새 클래스를 추가한다. 기존 idle/left/right 3클래스는 이미 학습·배포 완료된 상태이니 **절대 건드리지 마라**(별도 임펄스로 작업).

## 배경 / 참고할 기존 자료 (먼저 읽어라)

- `PDR_SideEye.md` — 전체 시스템 설계, 3절이 IMU/전력상태 관련
- `firmware/master/imu_web_collector/imu_web_collector.ino` — 이미 검증된 웹 기반 IMU 수집기(AP 모드, 브라우저에서 라벨 선택+녹화+실시간 그래프, 로컬 브리지로 업로드). **이 구조를 그대로 재사용해라, 새로 만들지 마라** — label select 부분만 아래 4클래스로 바꾸면 된다.
- `imu_harness/imu_upload_bridge.py` — 로컬 업로드 브리지, 그대로 재사용
- `imu_harness/augment_gaussian.py` — 데이터 적으면 가우시안 노이즈로 증강하는 스크립트, 필요하면 재사용(단, train/val 누수로 정확도가 뻥튀기될 수 있다는 거 사용자에게 미리 알릴 것)
- `firmware/master/turn_signal_live/turn_signal_live.ino` — 기존 3클래스 판정+네오픽셀 반응 코드. 새 클래스 통합 시 참고용

## Edge Impulse 프로젝트

- 프로젝트 ID: `1079933` (SideEYE), URL: https://studio.edgeimpulse.com/studio/1079933
- API 키: `.env`의 `SideEYE_EI_API_KEY`
- **주의**: 같은 프로젝트에 비전(vehicle/background) 데이터와 기존 IMU(idle/left/right) 데이터가 이미 있다. 새 임펄스 만들 때 기존 것들 지우거나 설정 건드리지 마라 — raw data category(training/testing) 상태도 그대로 둬라.

## 추가할 4클래스

1. **정지(stop)** — 주행 중 감속/정지하는 동작. 다음 단계 로드맵 기능(네오픽셀 정지등, 흰색 `0xFFFFFF`)과 연결됨(`PDR_SideEye.md` 3절 색상표 참고)
2. **헬멧 착용(helmet_on)** — 헬멧을 머리에 쓰는 동작
3. **헬멧 탈거(helmet_off)** — 헬멧을 벗는 동작. 이 두 클래스는 전력 상태 전환(deep sleep ↔ light sleep, `PDR_SideEye.md` 3.1절)에 쓰일 예정
4. **낙차/충돌(fall)** — 넘어지거나 강한 충격을 받는 동작. BLE_front.md 8절에 언급된 `CrashEvent`용 기반 데이터

## 할 일

1. `imu_web_collector.ino`를 복사해서 라벨 선택지를 위 4개(+필요하면 기존 idle도 대조군으로 포함)로 바꾼 새 수집기 스케치 작성 — 폴더명 예: `firmware/master/imu_collector_extra_classes/`
2. 라벨별로 최소 30~50개씩 실측 데이터 수집(사용자가 직접 동작하면서 수집, 매 녹화마다 그래프로 확인하면서 이상한 샘플 걸러내기 — 오늘 idle/left/right 수집 때 이 방식으로 문제 여러 번 잡아냈다)
3. 원본 데이터 EI에 업로드 확인(REST API로 직접 조회해서 개수 확인, 짐작하지 말 것)
4. 필요시 `augment_gaussian.py`로 증강 (근데 먼저 실측 데이터만으로 학습해보고 정확도 확인한 다음, 부족하면 증강 — 오늘 100ms 창 실험에서 증강만으로는 val_accuracy가 신뢰할 수 없다는 걸 이미 겪었다)
5. **별도 임펄스**로 생성(기존 idle/left/right 임펄스 설정 안 건드림) — window size는 기존과 같은 500ms로 시작해서 정확도 확인, 필요하면 조정
6. 학습 → val_accuracy 정직하게 보고
7. 정확도 괜찮으면 Arduino 라이브러리 빌드+다운로드, 별도 테스트 스케치로 온보드 검증
8. 끝나면 `firmware/`, `imu_harness/`, `PDR_SideEye.md`(3.1절 갱신) 커밋+push — 기존 파일(`turn_signal_live.ino`, `checkpoint4/6` 등)은 건드리지 마라, 이 4클래스를 실제 시스템에 통합하는 건 다음 단계 작업이다

막히면 짐작하지 말고 에러/로그 그대로 사용자에게 보여줘라.

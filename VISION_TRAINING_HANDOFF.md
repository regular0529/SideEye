# SideEye 비전 학습 지시서 (팀원용)

데이터 촬영은 끝났다. 여기서부터 Edge Impulse Studio 웹 화면에서 직접 하면 된다. 코드/API 몰라도 됨.

## 0. 확인 사항 (이미 처리됨, 참고만)

- 프로젝트: https://studio.edgeimpulse.com/studio/1079933 (`SideEYE`)
- 클래스: `vehicle` 20장, `background` 20장, 균형 맞춰짐(라벨 오류 수정 완료)
- IMU 데이터(`idle`/`left`/`right`)도 같은 프로젝트에 있음 — **건드리지 마라**, 비전 임펄스만 새로 만들면 됨

## 1. 임펄스 만들기

1. 왼쪽 메뉴 **Create impulse** 클릭
2. **Add a processing block** → `Image` 선택
3. **Add a learning block** → `Transfer Learning (Images)` 선택
4. Image data 블록 설정:
   - Image width: `96`
   - Image height: `96`
   - Resize mode: `Squash`
5. **Save Impulse** 클릭

## 2. 이미지 특징(feature) 생성

1. 왼쪽 메뉴 **Image** (방금 추가한 processing block) 클릭
2. Color depth: `RGB`
3. **Save parameters** → **Generate features** 클릭
4. 완료될 때까지 기다림(1~2분)
5. Feature explorer에서 `vehicle`/`background`가 색깔별로 잘 갈라져 보이는지 확인(너무 섞여있으면 데이터가 애매하다는 뜻)

## 3. 학습

1. 왼쪽 메뉴 **Transfer learning** 클릭
2. 기본값 그대로 두고(Neural network settings 안 건드려도 됨) **Start training** 클릭
3. 학습 끝나면 아래쪽에 정확도(Accuracy)가 나옴 — 캡처해서 팀 채팅에 공유해줘

## 4. Arduino 라이브러리 다운로드

1. 왼쪽 메뉴 **Deployment** 클릭
2. **Arduino library** 선택
3. Deployment options: **EON Compiler** 체크, Quantized (int8) 선택
4. **Build** 클릭 → 다운로드된 zip 파일을 나(개발 담당)한테 전달하거나, GitHub에 `vision_deploy/` 같은 폴더 만들어서 올려줘 — ESP32에 올려서 실제 카메라 인식 테스트할게

## 5. 참고 — 정확도가 너무 낮게 나오면

- `background 20장 vehicle 20장`이 좀 적은 편이라(기존 mouse/tumbler 데모는 150장씩 써서 94.6% 나왔었음) 정확도 낮으면 각 클래스 20~30장씩 더 촬영해서 업로드하는 게 가장 확실한 개선 방법
- 촬영 방법은 `XIAO_WEBCAM_EDGE_IMPULSE_GUIDE.md` 그대로 반복하면 됨(클래스 선택 꼭 확인 — 이번에 `vehicle` 촬영하면서 `background`로 잘못 찍힌 적 있었음)

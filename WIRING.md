# SideEye 배선도 — 마스터 보드

> PCB/확장보드 미사용. 전부 점프선 직결. `PDR.md` 4절 하드웨어 구성과 대응.
> 핀맵 출처: XIAO ESP32S3 공식 핀아웃 다이어그램, GY-BNO055 핀아웃 다이어그램(mischianti.org).

## 보드: XIAO ESP32S3 Sense (마스터, 헬멧 우측)

```
GY-BNO055 (9축 IMU, I2C)
  VIN  -> 3.3V-OUT
  GND  -> GND
  SCL  -> D5 (GPIO6, I2C1_SCL)
  SDA  -> D4 (GPIO5, I2C1_SDA)
  ADD  -> 비연결(플로팅)   ※ GND에 연결하면 주소 0x28로 바뀜, 코드가 기대하는 0x29과 어긋남
  INT / BOOT / REST -> 비연결(미사용)

NeoPixel (WCMCU-2812B-12, WS2812B 호환 12구 링)
  DI (데이터 입력, DO 아님) -> D1 (GPIO2)
  5V -> XIAO 5V(VBUS) 핀   ※ 3.3V-OUT 아님 — WS2812 최소 동작전압(3.5V) 미달로 부적합, 실측 확인됨
  GND -> GND
  DO -> 비연결(다음 링으로 이어질 때만 사용)

부저 (패시브 피에조)
  신호 -> D0 (GPIO1)
  GND  -> GND (연결 필수 — LEDC PWM으로 구동하므로 GND 없으면 소리 안 남)
```

## 마스터-슬레이브 전원 공유

슬레이브는 자체 USB 없이도 동작 가능 — 마스터의 5V/GND를 슬레이브에 공통 연결하면 마스터 전원(USB-C 또는 보조배터리)만으로 둘 다 구동된다. ESPNOW는 WiFi 라디오 직통이라 USB 연결 여부와 무관하게 통신된다.

**안테나 필수 확인**: 보드에 외부 안테나 커넥터(u.FL)가 있다면 반드시 장착. 안테나 없이도 `esp_now_send()`는 `ESP_OK`를 반환하지만 실제로는 전파가 안 나가 상대가 영원히 못 받는다 — 소프트웨어로는 절대 못 잡아내는 증상이니 배선/설정이 다 맞아 보이는데 통신이 안 되면 제일 먼저 확인할 것.

## 네오픽셀 RMT 버그 우회

`Adafruit_NeoPixel` + arduino-esp32 코어 3.2.0 조합에서 RMT 채널의 `allow_pd` 플래그가 초기화 안 돼 전송 자체가 실패하는 버그가 있다(하드웨어 문제 아님). ESP-IDF `driver/rmt_tx.h`를 직접 써서 `channelConfig.flags.allow_pd = 0`을 명시적으로 지정해 우회 — 현재 코드(`firmware/master/sideeye_master/sideeye_master.ino`)에 이미 반영되어 있다.

**주의사항**:
- 저항을 쓸 거면 5V선이 아니라 **DIN선(D1-DI 사이)**에 넣어야 함. 5V선에 넣으면 전압강하로 모듈이 3.4V밖에 못 받아 정상 동작 안 함.
- 12구라 풀밝기(255)로 켜면 USB 전류 한계 넘을 수 있음 — 코드에서 밝기 64/255로 제한.
- Sense 카메라는 GPIO10~18, 38~40, 47~48만 쓰고 GPIO2(D1)와 무관 — 카메라 핀 충돌 아님.

## 부저 — LEDC PWM, 듀티 50%가 최대음량

패시브 피에조는 `tone()`이 아니라 LEDC PWM으로 직접 구동해야 볼륨 조절이 된다. **듀티 50%(128/255)가 최대 음량** — 100%(255/255)는 사각파가 아니라 DC 상태라 오히려 거의 무음이 된다(진동이 있어야 소리가 남).

## 색상 규약 (PDR.md 3절과 동일)

| 상태 | 색상/패턴 |
|---|---|
| 좌/우 턴시그널 | 절반 링 앰버(`0xFFBF00`) 5회 점멸(300ms on/off), 픽셀 0/6 제외 |
| 정지등 | 풀링 빨강(`0xFF0000`) 5회 점멸 |
| 헬멧착용 확인 | 풀링 초록(`0x00FF00`) 순차 채움 애니메이션 |
| 대기 | 꺼짐 |

## 검증 코드

- `firmware/master/sideeye_master/sideeye_master.ino` — 최종 마스터 펌웨어(IMU 5클래스+턴시그널+정지등+헬멧애니메이션+ESPNOW 페일세이프)
- `firmware/slave/sideeye_slave/sideeye_slave.ino` — 최종 슬레이브 펌웨어(비전 캡처+분류+회신)

업로드 커맨드와 라이브러리 설치는 `REPRODUCTION.md` 참조.

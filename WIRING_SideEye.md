# SideEye 배선도 — 마스터 보드

> PCB/확장보드 미사용. 전부 점프선 직결. `PDR_SideEye.md` 4절 하드웨어 구성과 대응.
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
  GND  -> 의도적으로 미연결 (수업 중 소음 방지)
         실제 데모 전에 GND만 연결하면 됨, 코드/신호선은 이미 완성
```

## 네오픽셀 해결 기록 (2026-08-05)

처음엔 완전히 안 켜져서 모듈이 죽은 줄 알았다 — 점퍼선 교체, 330Ω 저항, 12구 전체 점등, 다른 GPIO(D1→D2) 이동까지 다 해봐도 그대로였음. 진짜 원인은 하드웨어가 아니라 `Adafruit_NeoPixel` + ESP32 Arduino 코어 3.2.0 조합의 RMT 초기화 버그: `rmt_tx_channel_config_t`의 `allow_pd` 플래그가 초기화 안 된 채로 남아서 ESP32-S3에서 전송 자체가 실패함(시리얼에 `rmt_new_tx_channel: not able to power down in light sleep` 경고로 나타남).

**해결**: `Adafruit_NeoPixel` 라이브러리 대신 ESP-IDF `driver/rmt_tx.h`를 직접 써서 `channelConfig.flags.allow_pd = 0`을 명시적으로 지정. 검증 코드: `firmware/master/neopixel_direct_rmt_test/`. 상세 진단 과정은 `NEOPIXEL_DEBUGGING.md` 참조.

**주의사항**:
- 저항을 쓸 거면 5V선이 아니라 **DIN선(D1-DI 사이)**에 넣어야 함. 5V선에 넣으면 전압강하로 모듈이 3.4V밖에 못 받아 정상 동작 안 함(실측: 5V→저항→3.4V, 오작동 원인 중 하나였음).
- 12구라 풀밝기(255)로 켜면 USB 전류 한계 넘을 수 있음 — 밝기 64/255로 제한해서 사용.
- Sense 카메라는 GPIO10~18, 38~40, 47~48만 쓰고 GPIO2(D1)와 무관 — 카메라 핀 충돌 아님, 확인됨.

## 사용하지 않는 핀 (확장 여유)

D6(GPIO43, UART TX), D7(GPIO44, UART RX), D8~D10(SPI), GPIO0(BOOT), GPIO21(내장 LED, 디버깅용으로만 사용 가능) — 슬레이브 통신(ESPNOW)은 WiFi 라디오로 처리되므로 별도 GPIO 불필요.

## 색상 규약 (PDR_SideEye.md 3절과 동일)

| 상태 | 색상 |
|---|---|
| 좌측 기울임(방향지시등, 차량 유무 무관) | 빨강 `0xFF0000` (2026-08-05: 주황→빨강, 저밝기에서 노랑과 구분 안 돼서 변경) |
| 우측 기울임(방향지시등, 차량 유무 무관) | 파랑 `0x0000FF` (2026-08-05: 노랑→파랑) |
| 정지등(다음 단계) | 흰색 `0xFFFFFF` (좌측 색상과 안 겹치게 재지정) |
| 정상 동작 하트비트(checkpoint1 테스트 코드 기준) | 파랑 점멸 |
| 오류 상태 | 빨강 점멸 |

부저는 색상과 무관하게 `vehicleFound == true`일 때만 단일 비프(위험경보 전용, PDR 3절 참조).

## 검증 코드

- `firmware/master/checkpoint1_hw_test/checkpoint1_hw_test.ino` — IMU+네오픽셀+부저 통합 테스트, 시리얼 모니터 없이도 네오픽셀 색상만으로 하드웨어 정상 여부 확인 가능(부팅 흰색 1회 → IMU 정상 시 초록 3회 → 좌/우 색상 데모 → 이후 2초마다 파랑 하트비트, 이상 시 빨강 점멸).
- `firmware/master/neopixel_direct_rmt_test/` — 네오픽셀 단독 검증(직접 RMT 드라이버 최초 성공 코드).
- `firmware/master/neopixel_only_test/` — 네오픽셀 단독 격리 테스트(Adafruit_NeoPixel 버전, 디버깅 과정 기록용으로 유지).
- `firmware/master/d1_gpio_output_test/` — D1(GPIO2) 자체 생존 확인용 DC 전압 토글 테스트.

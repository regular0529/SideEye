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

NeoPixel (WS2812, 턴시그널 겸 정지등 표시용, 1구)
  VCC  -> 3.3V-OUT
  GND  -> GND
  DIN  -> D1 (GPIO2)
  ※ 데이터선 저항 없이 직결(팀 결정, 점퍼선 짧아서 생략)

부저 (패시브 피에조)
  신호 -> D0 (GPIO1)
  GND  -> 의도적으로 미연결 (수업 중 소음 방지)
         실제 데모 전에 GND만 연결하면 됨, 코드/신호선은 이미 완성
```

## 사용하지 않는 핀 (확장 여유)

D6(GPIO43, UART TX), D7(GPIO44, UART RX), D8~D10(SPI), GPIO21(USER_LED), GPIO0(BOOT) — 슬레이브 통신(ESPNOW)은 WiFi 라디오로 처리되므로 별도 GPIO 불필요.

## 색상 규약 (PDR_SideEye.md 3절과 동일)

| 상태 | 색상 |
|---|---|
| 좌측 차량 감지 | 주황 `0xFF8000` |
| 우측 차량 감지 | 노랑 `0xFFFF00` |
| 정지등(다음 단계) | 빨강 `0xFF0000` |
| 정상 동작 하트비트 | 파랑 `0x0000FF` (checkpoint1 테스트 코드 기준) |
| 오류 상태 | 빨강 점멸 |

## 검증 코드

`firmware/master/checkpoint1_hw_test/checkpoint1_hw_test.ino` — 시리얼 모니터 없이도 네오픽셀 색상만으로 하드웨어 정상 여부를 확인할 수 있게 설계됨(부팅 흰색 1회 → IMU 정상 시 초록 3회 → 턴시그널 색상 데모 → 이후 2초마다 파랑 하트비트, 이상 시 빨강 점멸).

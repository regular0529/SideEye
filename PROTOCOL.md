# projectbee 센서 네트워크 — MQTT 프로토콜 규격

여러 대의 ESP32-S3 노드가 공유기에 붙어 하나의 MQTT 브로커로 센서값을 올리고,
브라우저 대시보드가 웹소켓으로 그 값을 실시간으로 읽고 LED를 제어하는 구조입니다.

이 문서만 보고도 **새 노드를 네트워크에 추가**할 수 있도록 작성했습니다.

---

## 1. 전체 구성

```
  ┌─────────────────┐
  │ XIAO ESP32-S3   │ Wi-Fi STA
  │  노드 A         │───────┐
  └─────────────────┘       │
  ┌─────────────────┐       │   MQTT / TCP        ┌──────────────────────┐
  │ XIAO ESP32-S3   │───────┼──── 1883 ──────────▶│  Mosquitto 브로커     │
  │  노드 B         │       │                     │  192.168.0.2         │
  └─────────────────┘       │                     │  (Windows PC)        │
  ┌─────────────────┐       │                     └──────────┬───────────┘
  │ 다른 센서 노드   │───────┘                                │
  └─────────────────┘                          MQTT / WebSocket 9001
                                                              │
                                                   ┌──────────▼───────────┐
                                                   │  대시보드 (브라우저)   │
                                                   │  dashboard/index.html│
                                                   └──────────────────────┘
```

- **노드 → 브로커**: 일반 MQTT (TCP 1883)
- **브라우저 → 브로커**: MQTT over WebSocket (9001)
  브라우저는 raw TCP 소켓을 못 열기 때문에 웹소켓 리스너가 반드시 따로 필요합니다.

---

## 2. 접속 정보

| 항목 | 값 |
|---|---|
| Wi-Fi SSID | `projectbee` |
| Wi-Fi 비밀번호 | `honeybear!` |
| 브로커 주소 | `192.168.0.2` |
| MQTT 포트 (노드용) | `1883` |
| WebSocket 포트 (대시보드용) | `9001` |
| 인증 | 없음 (익명 허용) |
| TLS | 사용 안 함 |

> 브로커 PC가 DHCP로 IP를 받으면 주소가 바뀔 수 있습니다.
> 공유기에서 **고정 IP(DHCP 예약)** 로 잡아두는 것을 권장합니다.

---

## 3. 토픽 규격

모든 토픽은 `bee/` 로 시작합니다.

```
bee/<nodeId>/<채널>
```

### 3.1 노드 ID (`<nodeId>`)

- 네트워크 안에서 **유일**해야 합니다.
- 기본 규칙: MAC 주소 뒤 3바이트를 소문자 16진수로 → `esp32-467660`
- 직접 지정해도 됩니다: `bee-01`, `temp-kitchen`, `seongho-01` …
- 사용 가능 문자: 영소문자, 숫자, `-` (슬래시 `/` 와 공백 금지)

### 3.2 토픽 목록

| 토픽 | 방향 | QoS | Retain | 페이로드 |
|---|---|---|---|---|
| `bee/<id>/status` | 노드 → | 1 | ✅ | `online` / `offline` |
| `bee/<id>/info` | 노드 → | 0 | ✅ | JSON (노드 정보) |
| `bee/<id>/telemetry` | 노드 → | 0 | ❌ | JSON (센서값) |
| `bee/<id>/led/state` | 노드 → | 0 | ✅ | `0` / `1` |
| `bee/<id>/led/set` | → 노드 | 0 | ❌ | `0` / `1` / `toggle` |
| `bee/all/led/set` | → 전체 | 0 | ❌ | `0` / `1` / `toggle` |

**Retain 을 쓰는 이유**: 대시보드를 나중에 열어도 각 노드의 마지막 상태
(살아있는지, LED가 켜져 있는지)를 즉시 볼 수 있습니다.
반대로 `telemetry` 는 계속 흐르는 값이라 retain 하지 않습니다 — 하면
접속하자마자 낡은 값이 한 번 튀어 보입니다.

---

## 4. 페이로드 규격

### 4.1 `status` — 생존 신호

```
online
```

**LWT(Last Will and Testament)를 반드시 설정하세요.** 노드가 전원이 나가거나
Wi-Fi가 끊기면 브로커가 대신 `offline` 을 retained 로 발행해 줍니다.
이게 없으면 대시보드는 죽은 노드를 영원히 "온라인"으로 표시합니다.

```cpp
// clientId, user, pass, willTopic, willQos, willRetain, willMessage
mqtt.connect(nodeId, nullptr, nullptr, topicStatus, 1, true, "offline");
```

### 4.2 `info` — 노드 자기소개 (연결 직후 1회, retained)

```json
{
  "node": "esp32-467660",
  "board": "XIAO_ESP32S3",
  "ip": "192.168.0.147",
  "mac": "68:EE:8F:46:76:60",
  "fw": "beeNode-1.0",
  "sensors": ["touch"]
}
```

| 필드 | 타입 | 필수 | 설명 |
|---|---|---|---|
| `node` | string | ✅ | 노드 ID |
| `board` | string | ✅ | 보드 종류 |
| `ip` | string | ✅ | 노드 IP |
| `mac` | string | | MAC 주소 |
| `fw` | string | | 펌웨어 버전 |
| `sensors` | string[] | | 이 노드가 올리는 센서 종류 |

### 4.3 `telemetry` — 센서값 (주기 발행)

```json
{
  "node": "esp32-467660",
  "touch": 17010,
  "baseline": 17101,
  "touched": false,
  "led": 0,
  "rssi": -43,
  "uptime": 176
}
```

**공통 필드 (모든 노드가 반드시 포함)**

| 필드 | 타입 | 설명 |
|---|---|---|
| `node` | string | 노드 ID (토픽과 동일하게) |
| `rssi` | int | Wi-Fi 신호 세기 (dBm, 음수) |
| `uptime` | int | 부팅 후 경과 초 |
| `led` | 0 \| 1 | 현재 LED 상태 |

**센서 필드 (자유롭게 추가)**

| 필드 | 타입 | 설명 |
|---|---|---|
| `touch` | int | 터치 원시값 |
| `baseline` | int | 미접촉 기준값 |
| `touched` | bool | 접촉 판정 결과 |

새 센서를 붙일 때는 여기에 필드를 추가하면 됩니다. 권장 이름:

| 센서 | 필드 | 단위 |
|---|---|---|
| 온도 | `tempC` | °C |
| 습도 | `humidity` | %RH |
| 조도 | `lux` | lux |
| 기압 | `pressure` | hPa |
| 배터리 | `battery` | V |

> **발행 주기**: 기본 500ms. 노드 수가 늘면(20대 이상) 1000~2000ms 로 늘리세요.
> 노드 1대당 500ms 는 초당 2메시지 = 20대면 초당 40메시지로, 이 정도는 여유입니다.

### 4.4 `led/set` — 제어 명령 (구독)

| 페이로드 | 동작 |
|---|---|
| `"0"` 또는 바이트 `0x00` | LED 끄기 |
| `"1"` 또는 바이트 `0x01` | LED 켜기 |
| `"toggle"` | 반전 |

명령을 처리한 뒤에는 **반드시 `led/state` 를 retained 로 다시 발행**하세요.
그래야 대시보드 버튼 상태와 실제 하드웨어가 어긋나지 않습니다.

`bee/all/led/set` 은 전체 브로드캐스트입니다. 모든 노드가 자기 토픽과 함께
이 토픽도 구독해야 합니다.

> **XIAO ESP32-S3 의 내장 LED(GPIO21)는 Active LOW 입니다.**
> `digitalWrite(LED, LOW)` 가 켜짐, `HIGH` 가 꺼짐입니다. 반대로 짜면 논리가 뒤집힙니다.

---

## 5. 새 노드 추가하기

### 5.1 준비

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install PubSubClient
```

### 5.2 템플릿에서 바꿔야 할 곳

`beeNode/beeNode.ino` 를 복사해서 상단 설정 블록만 고치면 됩니다.

```cpp
const char *WIFI_SSID = "projectbee";
const char *WIFI_PASS = "honeybear!";
const char *MQTT_HOST = "192.168.0.2";
const uint16_t MQTT_PORT = 1883;

// 비워두면 MAC 뒤 3바이트로 자동 생성.
// 여러 대를 눈으로 구분하려면 직접 지정.
const char *NODE_ID_OVERRIDE = "";

const unsigned long TELEMETRY_MS = 500;
```

### 5.3 빌드 / 업로드

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 -u -p COM5 beeNode
```

포트 번호는 `arduino-cli board list` 로 확인하세요.

### 5.4 다른 센서로 바꾸기

`loop()` 안의 telemetry JSON 조립 부분만 고치면 됩니다.

```cpp
// 예: DHT 온습도 센서를 추가하는 경우
snprintf(buf, sizeof(buf),
         "{\"node\":\"%s\",\"tempC\":%.1f,\"humidity\":%.1f,"
         "\"led\":%d,\"rssi\":%d,\"uptime\":%lu}",
         nodeId, temperature, humidity,
         ledOn ? 1 : 0, WiFi.RSSI(), millis() / 1000);
mqtt.publish(topicTelemetry, buf);
```

`node` / `led` / `rssi` / `uptime` 4개 공통 필드만 유지하면 대시보드가 그대로 인식합니다.

---

## 6. 브로커(Mosquitto) 설정

이미 `192.168.0.2` 에 설정을 마쳐두었습니다. **다른 PC에 새로 구축할 때만** 필요한 내용입니다.

`mosquitto.conf` 끝에 추가:

```conf
listener 1883 0.0.0.0
protocol mqtt

listener 9001 0.0.0.0
protocol websockets

allow_anonymous true
```

Windows 기준 적용:

```powershell
# 설정 후 서비스 재시작
Restart-Service mosquitto

# 방화벽 열기 (관리자 권한)
New-NetFirewallRule -DisplayName "Mosquitto MQTT 1883" -Direction Inbound `
  -Protocol TCP -LocalPort 1883 -Action Allow -Profile Private,Domain
New-NetFirewallRule -DisplayName "Mosquitto WebSocket 9001" -Direction Inbound `
  -Protocol TCP -LocalPort 9001 -Action Allow -Profile Private,Domain

# 리스너 확인 — 0.0.0.0 으로 떠 있어야 함 (127.0.0.1 이면 외부에서 못 붙음)
Get-NetTCPConnection -State Listen | Where-Object { $_.LocalPort -in 1883,9001 }
```

> Mosquitto 2.0 이상은 **기본값이 localhost 전용 + 익명 거부** 입니다.
> 위 설정 없이는 ESP32가 절대 붙지 못합니다. 가장 흔한 실패 원인입니다.

### 보안 참고

`allow_anonymous true` 는 **같은 공유기에 붙은 누구나** 값을 읽고 LED를 켤 수 있다는 뜻입니다.
실습망에서는 이걸로 충분하지만, 열린 네트워크에 두지는 마세요. 계정을 걸려면:

```powershell
& "C:\Program Files\mosquitto\mosquitto_passwd.exe" -c "C:\Program Files\mosquitto\pwfile" beeuser
```

```conf
allow_anonymous false
password_file C:\Program Files\mosquitto\pwfile
```

노드 쪽은 `mqtt.connect(nodeId, "beeuser", "비밀번호", ...)`, 대시보드 쪽은
`mqtt.connect(url, { username, password })` 로 바꾸면 됩니다.

---

## 7. 대시보드

`dashboard/index.html` 을 브라우저로 그냥 열면 됩니다. (별도 웹서버 불필요)

- 상단에 브로커 주소/포트를 넣고 **연결**
- 노드는 **자동으로 발견**되어 카드가 생깁니다 — 대시보드에 노드를 등록할 필요 없음
- 카드마다 터치값 실시간 그래프, RSSI, 업타임, LED ON/OFF/토글 버튼
- 상단 **전체 LED ON/OFF** 는 `bee/all/led/set` 브로드캐스트
- 하단 "표로 보기 / 원본 메시지 로그" 에서 값과 원본 MQTT 메시지 확인 가능

폴더 구조 (배포 시 통째로 복사):

```
dashboard/
├── index.html
└── lib/
    └── mqtt.min.js     ← 로컬 포함, 인터넷 없이도 동작
```

노드 상태 표시:

| 표시 | 의미 |
|---|---|
| 온라인 | `status=online` 이고 최근 4초 내 telemetry 수신 |
| 응답없음 | `online` 인데 4초 넘게 telemetry 없음 (Wi-Fi 불안정) |
| 오프라인 | LWT로 `offline` 수신 (전원/연결 끊김) |

---

## 8. 명령줄로 테스트하기

대시보드 없이 브로커만으로 확인할 때 씁니다.

```powershell
# 모든 노드의 모든 메시지 구독
& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h 192.168.0.2 -t "bee/#" -v

# 특정 노드 LED 켜기
& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h 192.168.0.2 -t "bee/esp32-467660/led/set" -m "1"

# 전체 노드 LED 끄기
& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h 192.168.0.2 -t "bee/all/led/set" -m "0"
```

---

## 9. 문제 해결

| 증상 | 확인할 것 |
|---|---|
| 노드가 브로커에 못 붙음 | 리스너가 `0.0.0.0` 인지 (`127.0.0.1` 이면 외부 차단), 방화벽 1883, 브로커 IP 변경 여부 |
| 시리얼에 `rc=-2` | 브로커 주소/포트 오류 또는 방화벽. 같은 PC에서 `mosquitto_sub` 로 먼저 확인 |
| Wi-Fi 접속에서 멈춤 | SSID/비밀번호 오타. **공유기가 5GHz 전용이면 안 됩니다 — ESP32는 2.4GHz만 지원** |
| 대시보드가 "끊김" | 9001 웹소켓 리스너와 방화벽. 브라우저 콘솔(F12)에 WebSocket 에러 확인 |
| 노드 카드가 안 뜸 | `mosquitto_sub -t "bee/#" -v` 로 메시지가 실제로 오는지 먼저 확인 |
| 두 노드가 같은 카드로 겹침 | 노드 ID 중복. `NODE_ID_OVERRIDE` 로 서로 다르게 지정 |
| 노드가 계속 재접속 반복 | clientId 중복. 노드 ID가 곧 clientId이므로 유일해야 함 |
| 터치 판정이 전혀 안 됨 | 부팅 시 기준값 측정 구간에 D0을 만지지 않았는지 확인. 터치 주변장치는 부팅 직후 값이 튀므로 앞쪽 샘플을 버리고 평균 내야 함 (`calibrateTouch()` 참고) |
| LED 논리가 반대 | 내장 LED는 Active LOW. `LOW`가 켜짐 |

---

## 10. 파일 구성

| 경로 | 내용 |
|---|---|
| `beeNode/beeNode.ino` | 노드 펌웨어 (Wi-Fi STA + MQTT) |
| `dashboard/index.html` | 웹소켓 실시간 대시보드 |
| `dashboard/lib/mqtt.min.js` | MQTT.js (로컬 포함) |
| `PROTOCOL.md` | 이 문서 |
| `esp32s3Xiao.md` | XIAO ESP32-S3 하드웨어 / 핀맵 |
| `ble.md` | BLE 사용법 (참고) |

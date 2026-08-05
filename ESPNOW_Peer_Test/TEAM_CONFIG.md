# Team MAC Configuration

팀원은 자신의 보드 MAC과 통신 상대 MAC을 이 표에 기록합니다.

| 역할 | 학생 | 자신의 MAC | `PEER_MAC_TEXT`에 입력할 MAC |
|---|---|---|---|
| A | Kim Jeonggyu | `68:EE:8F:46:73:B0` | `68:EE:8F:46:73:BC` |
| B | 팀원 B | `68:EE:8F:46:73:BC` | `68:EE:8F:46:73:B0` |
| C | 팀원 C | 확인 필요 | 유닛 테스트에서는 사용하지 않음 |

## SideEye 마스터/슬레이브 실측 (2026-08-05, 체크포인트 2 통과)

| 역할 | 자신의 MAC | `PEER_MAC_TEXT`에 입력할 MAC | 포트 |
|---|---|---|---|
| 마스터 | `1C:DB:D4:74:49:E0` | `AC:27:6E:A8:47:80` | COM13 |
| 슬레이브 | `AC:27:6E:A8:47:80` | `1C:DB:D4:74:49:E0` | COM14 |

양방향 통신(터치 → 상대 LED 제어) 왕복 확인됨. `firmware/shared/protocol.h`에 이 MAC 기준으로 실 프로토콜을 얹으면 됨(PDR_SideEye.md 6.2절).

## A-G 원형 설정

최종 원형은 다음과 같습니다.

```text
A -> B -> C -> D -> E -> F -> G -> A
```

| 역할 | 이전 MAC | 다음 MAC |
|---|---|---|
| A | G MAC | `68:EE:8F:46:73:BC` |
| B | `68:EE:8F:46:73:B0` | C MAC |
| C | B MAC | D MAC |
| D | C MAC | `98:A3:16:F7:BA:64` |
| E | `10:B4:1D:E9:08:A0` | F MAC |
| F | `98:A3:16:F7:BA:64` | `C4:0F:08:54:9A:93` |
| G | F MAC | `68:EE:8F:46:73:B0` |

## 설정 예시

보드 A:

```cpp
const char *PEER_MAC_TEXT = "68:EE:8F:46:73:BC";
```

보드 B:

```cpp
const char *PEER_MAC_TEXT = "68:EE:8F:46:73:B0";
```

MAC 주소를 입력한 뒤 두 보드에 같은 `ESPNOW_Peer_Test.ino`를 업로드합니다.

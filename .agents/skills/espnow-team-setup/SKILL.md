---
name: espnow-team-setup
description: Use this skill whenever the user works on this project's ESP-NOW ring, A-G node roles, MAC address configuration, ESP32 LED/touch control, the ESP-NOW Team Hub HTML page, or asks to compile/upload/debug a team node. Treat the Team Hub generated firmware as the source of truth. Do not regenerate or redesign the firmware unless explicitly requested.
compatibility: Windows, PowerShell, OpenCode/Orca, Arduino CLI, ESP32 Arduino core 3.2.0, XIAO ESP32-S3
---

# ESP-NOW Team Setup

Use this skill as the operating manual for the WearableProjectEX team ESP-NOW system.

## Source Of Truth

Read these files before changing behavior:

- `ESPNOW_Ring_Node/ESPNOW_Ring_Node.ino`: canonical A-gateway and B-G ring firmware template.
- `ESPNOW_Ring_Node/README.md`: ring behavior and deployment notes.
- `ESPNOW.md`: Seeed ESP-NOW API, channel, MAC and ESP32 core 3.x guidance.
- `team-hub/server.mjs`: MAC registration, role-specific code generation and live-state API.
- `team-hub/public/index.html`: team registration, code/README copy, and live dashboard.
- `team-hub/data/peers.json`: registered team MAC roster.

Do not use `ESPNOW_Peer_Test` for the final A-G ring. It is only a two-board unit test. Do not use the old MQTT, BLE, or sleep-demo firmware for this workflow.

## Architecture

The ring control path and the web monitoring path are separate:

```text
B-G: touch -> ESP-NOW -> next node LED
B-G: monitor heartbeat -> ESP-NOW -> A
A: touch -> ESP-NOW -> B
G: touch -> ESP-NOW -> A
A: aggregate states -> HTTP POST -> Team Hub -> HTML
```

- ESP-NOW controls LEDs and carries the ring heartbeat.
- Only A sends HTTP state reports to the Team Hub.
- B-G must not be changed to direct HTTP reporters.
- All nodes connect to the same 2.4 GHz Wi-Fi so their ESP-NOW channel is consistent; B-G use Wi-Fi for channel coordination, not web reporting.
- The browser cannot receive ESP-NOW directly.

## Ring Order

The ring has seven roles and no H node:

```text
A -> B -> C -> D -> E -> F -> G -> A
```

| Role | Previous node | Next node | Current MAC |
|---|---|---|---|
| A | G | B | `68:EE:8F:46:73:B0` |
| B | A | C | `68:EE:8F:46:73:BC` |
| C | B | D | `98:A3:16:F7:0E:64` |
| D | C | E | `10:B4:1D:E9:08:A0` |
| E | D | F | `98:A3:16:F7:BA:64` |
| F | E | G | `68:EE:8F:46:74:D4` |
| G | F | A | `C4:0F:08:54:9A:93` |

When a MAC changes, update the Team Hub roster. Do not manually edit every node's peer address.

## Role Configuration

The Team Hub generates role-specific values:

- A: `REPORT_TO_WEB = true`, `GATEWAY_MAC_TEXT = A MAC`, previous G, next B.
- B-G: `REPORT_TO_WEB = false`, `GATEWAY_MAC_TEXT = A MAC`, previous and next from the ring table.
- Every node has `NODE_ROLE`, `NODE_NAME`, `PREVIOUS_MAC_TEXT`, and `NEXT_MAC_TEXT` generated from the roster.
- A uses `TEAM_HUB_URL` and posts the aggregate state.

Never put A's configuration on B. For example, a B firmware must not contain `NODE_ROLE = "A"` or `PREVIOUS_MAC_TEXT = G`.

## Team Hub Workflow

Open the LAN page:

```text
http://192.168.0.104:8080
```

If the server is not running, the host runs `team-hub\START_TEAM_HUB.cmd` or:

```powershell
node .\team-hub\server.mjs
```

The team member workflow is:

1. Select role A-G.
2. Upload the page's `MAC 확인 코드` to the board and read the Wi-Fi STA MAC at 115200 baud.
3. Enter only the role, name, and own MAC in the page.
4. Wait for the page to calculate previous and next MACs.
5. Use `코드 파일 받기` for the exact `.ino` file. `코드만 복사` is also exact; `코드 + README 복사` is for giving context to an LLM and is not an `.ino` file.
6. Do not ask an LLM to rewrite the firmware. Ask it only to compile and upload the downloaded file.

Use this safe OpenCode prompt:

```text
Do not edit or regenerate the .ino file. Compile and upload the existing sketch exactly as it is.
First run arduino-cli board list and use the connected ESP32 port.
Target board: esp32:esp32:XIAO_ESP32S3.
Show the complete compile and upload result.
```

## Arduino CLI Commands

For XIAO ESP32-S3:

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 .\ESPNOW_Ring_Node
arduino-cli upload -p COM번호 --fqbn esp32:esp32:XIAO_ESP32S3 .\ESPNOW_Ring_Node
arduino-cli monitor -p COM번호 -c baudrate=115200
```

Stop the serial monitor with `Ctrl+C` before uploading again. Uploading resets the board.

## Required Serial Evidence

Do not claim that a node works from compilation alone. Capture these logs:

```text
[MAC] self=<own MAC>
[RING] role=<role> previous=<previous MAC> next=<next MAC> gateway=<A MAC>
[ESP-NOW] ready previous=yes next=yes gateway=yes
```

A gateway must also show:

```text
[WEB] gateway posted 200
```

When a previous node sends an LED command, the receiving node must show:

```text
[RX] from=<previous MAC> led=1
```

When a send succeeds, the sender's counters should increase and the log should show `status=success`.

## HTML State Meaning

The Team Hub marks a registered node online after A receives its heartbeat. B-G send heartbeat frames to A every second. The server allows a 15-second grace period for transient Wi-Fi/ESP-NOW delays.

- A online but B-G offline: A is running the gateway, but B-G have not sent the new heartbeat firmware or are not powered/on the same channel.
- All nodes offline: A is not running the gateway or the Team Hub server is unreachable.
- A `rx=0`: A has not received a valid ring or monitor frame.
- B LED does not turn on after A touch: B is probably running the wrong role configuration or an old `ESPNOW_Peer_Test` firmware.

## Common Failure Checks

Check in this order:

1. Confirm the board's printed MAC matches the Team Hub roster.
2. Confirm the role in the generated code matches the student role.
3. Confirm previous and next MACs are not reversed.
4. Confirm all boards use the same `MESSAGE_MAGIC`, message struct and generated firmware version.
5. Confirm every board has its antenna installed and is powered.
6. Confirm all boards can join the same 2.4 GHz Wi-Fi and report the same Wi-Fi channel.
7. Confirm the A gateway's `TEAM_HUB_URL` points to the server PC LAN address, not `localhost`.
8. Confirm the Team Hub server window remains open and TCP port 8080 is allowed on the Windows Private network.

Never diagnose a missing HTML node by changing the 15-second timeout first. A missing node usually means A received no heartbeat.

## Safety And Security

- This is a classroom LAN experiment with ESP-NOW encryption disabled.
- Keep the Team Hub and MAC roster on the private LAN; do not expose port 8080 to the internet.
- Wi-Fi credentials are currently present in project reference material and firmware templates. Do not publish them.
- Do not upload the combined code-plus-README clipboard text as an `.ino` file. Save only the code section, or download the exact file.

## Response Format For Other LLMs

When assisting a team member, report:

1. Detected board model and COM port.
2. Role and own/previous/next MAC values.
3. Exact compile command and result.
4. Exact upload command and result.
5. The first relevant serial logs.
6. Whether the failure is configuration, channel, power, upload, ESP-NOW send, ESP-NOW receive, or Team Hub reporting.

// SideEye BLE protocol -- shared by master (peripheral) and phone (central)
// Change here requires phone-app owner sign-off (protocol.h와 동일한 원칙).
#pragma once

// ---- Service ----
#define SIDEEYE_BLE_SERVICE_UUID       "7a2e0001-4b6f-4a3e-9c1a-4f7b6c8d9e01"

// ---- Characteristics (모두 notify, 폰은 read/write 안 함) ----
#define SIDEEYE_LEAN_STATE_UUID        "7a2e0002-4b6f-4a3e-9c1a-4f7b6c8d9e01"
#define SIDEEYE_ALERT_EVENT_UUID       "7a2e0003-4b6f-4a3e-9c1a-4f7b6c8d9e01"
#define SIDEEYE_DEVICE_STATUS_UUID     "7a2e0004-4b6f-4a3e-9c1a-4f7b6c8d9e01"

// ---- LeanState payload: 1 byte ----
//   0 = idle, 1 = left, 2 = right
// classifyLean()/applyTurnSignal()의 label과 동일한 의미, 실제 값이 바뀔 때만 notify

// ---- AlertEvent payload: 2 bytes ----
//   byte[0] direction: 0 = right, 1 = left
//   byte[1] vehicleFound: 0 = false, 1 = true
// vehicleFound == true일 때만 notify (부저 울리는 바로 그 순간)

// ---- DeviceStatus payload: 1 byte (지금은 미확정, 자리만) ----
//   추후 배터리 퍼센트(0-100) 등에 사용 예정. 하드웨어 미정이라 지금은 0xFF(unknown) 고정 가능.

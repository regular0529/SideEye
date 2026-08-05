// SideEye ESPNOW protocol -- shared by master and slave (PDR_SideEye.md 6.2)
// Change here requires master module owner sign-off (PDR section 7).
#pragma once
#include <cstdint>

constexpr uint32_t SIDEEYE_MSG_MAGIC = 0x53454331;  // "SEC1"
constexpr uint8_t SIDEEYE_MSG_VERSION = 1;

constexpr uint8_t CMD_CAPTURE_REQUEST = 1;
constexpr uint8_t CMD_RESULT_REPLY = 2;

struct __attribute__((packed)) EspNowMessage {
  uint32_t magic;
  uint8_t version;
  uint8_t command;
  uint8_t vehicleFound;  // valid on CMD_RESULT_REPLY only (0/1)
  uint32_t sequence;
};

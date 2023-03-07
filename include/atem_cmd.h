#pragma once

#include <esp_event.h>
#include <stdint.h>

#define ATEM_CMD(s) (s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]

namespace atem {

// Default time to wait
static const auto TTW = 10 / portTICK_PERIOD_MS;

struct ProgramInput {
  uint8_t ME;
  uint16_t source;
};

struct PreviewInput {
  uint8_t ME;
  uint16_t source;
  bool visable;
};

}  // namespace atem

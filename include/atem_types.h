#pragma once

#include <esp_event.h>
#include <stdint.h>

#define ATEM_CMD(s) (s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]

namespace atem {

// Default time to wait
static const auto TTW = 10 / portTICK_PERIOD_MS;

enum Source : uint16_t {
  BLACK,
  INPUT_1,
  INPUT_2,
  INPUT_3,
  INPUT_4,
  INPUT_5,
  INPUT_6,
  INPUT_7,
  INPUT_8,
  INPUT_9,
  INPUT_10,
  INPUT_11,
  INPUT_12,
  INPUT_13,
  INPUT_14,
  INPUT_15,
  INPUT_16,
  INPUT_17,
  INPUT_18,
  INPUT_19,
  INPUT_20,
  INPUT_21,
  INPUT_22,
  INPUT_23,
  INPUT_24,
  INPUT_25,
  INPUT_26,
  INPUT_27,
  INPUT_28,
  INPUT_29,
  INPUT_30,
  INPUT_31,
  INPUT_32,
  INPUT_33,
  INPUT_34,
  INPUT_35,
  INPUT_36,
  INPUT_37,
  INPUT_38,
  INPUT_39,
  INPUT_40,
  COLOR_BARS = 1000,
  COLOR_GEN_1 = 2001,
  COLOR_GEN_2,
  MEDIAPLAYER_1 = 3010,
  MEDIAPLAYER_1_KEY,
  MEDIAPLAYER_2 = 3020,
  MEDIAPLAYER_2_KEY,
  UKEY_1 = 4010,
  UKEY_2 = 4020,
  UKEY_3 = 4030,
  UKEY_4 = 4040,
  DSK_1_MASK = 5010,
  DSK_2_MASK = 5020,
  SUPER_SOURCE = 6000,
  CLEAN_FEED_1 = 7001,
  CLEAN_FEED_2,
  AUX_1 = 8001,
  AUX_2,
  AUX_3,
  AUX_4,
  AUX_5,
  AUX_6,
  AUX_7,
  AUX_8,
  AUX_9,
  AUX_10,
  AUX_11,
  AUX_12,
  AUX_13,
  AUX_14,
  AUX_15,
  AUX_16,
  AUX_17,
  AUX_18,
  AUX_19,
  AUX_20,
  AUX_21,
  AUX_22,
  AUX_23,
  AUX_24,
  MULTIVIEW_1 = 9001,
  MULTIVIEW_2,
  MULTIVIEW_3,
  MULTIVIEW_4,
  ME1_PROGRAM = 10010,
  ME1_PREVIEW,
  ME2_PROGRAM = 10020,
  ME2_PREVIEW,
  ME3_PROGRAM = 10030,
  ME3_PREVIEW,
  ME4_PROGRAM = 10040,
  ME4_PREVIEW,
};

struct ProgramInput {
  uint8_t ME;
  Source source;
};

struct PreviewInput {
  uint8_t ME;
  Source source;
  bool visable;
};

struct AuxInput {
  uint8_t channel;
  Source source;
};

struct InputProperty {
  Source source;
  char name_long[21];
  char name_short[5];
};

struct ProtocolVerion {
  uint16_t major;
  uint16_t minor;
};

struct TransitionPosition {
  uint8_t ME;
  bool in_transition;
  uint16_t position;
};

struct Topology {
  uint8_t num_me;
  uint8_t num_sources;
  uint8_t num_aux;
};

}  // namespace atem

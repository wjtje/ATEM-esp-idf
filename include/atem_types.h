/**
 * @file atem_types.h
 * @author Wouter (atem_esp_idf@wjt.je)
 * @brief Provides different types used by other parts of the protocol.
 *
 * @copyright Copyright (c) 2023 - Wouter (wjtje)
 */
#pragma once

#include <cstdint>
#include <vector>

#include "atem_state.h"

namespace atem {

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
  WEB_CAM_OUT = 8200,
  MULTIVIEW_1 = 9001,
  MULTIVIEW_2,
  MULTIVIEW_3,
  MULTIVIEW_4,
  RECORDING_STATUS = 9101,
  STREAMING_STATUS,
  AUDIO_STATUS,
  ME1_PROGRAM = 10010,
  ME1_PREVIEW,
  ME2_PROGRAM = 10020,
  ME2_PREVIEW,
  ME3_PROGRAM = 10030,
  ME3_PREVIEW,
  ME4_PROGRAM = 10040,
  ME4_PREVIEW,
  UNDEFINED = 0xFFFF
};

enum TransitionStyle : uint8_t {
  MIX = 0,
  DIP = 1,
  WIPE = 2,
};

struct InputProperty {
  /// @warning This string is not NULL terminated
  char name_long[20];

  /// @warning This string is not NULL terminated
  char name_short[4];
};

struct TransitionPosition {
  bool in_transition;
  uint16_t position;
};

struct TransitionState {
  TransitionStyle style;
  uint8_t next;
};

struct Topology {
  uint8_t me;
  uint8_t sources;
  uint8_t dsk;
  uint8_t aux;
  uint8_t mixminus_outputs;
  uint8_t mediaplayers;
  uint8_t multiviewers;
  uint8_t rs485;
  uint8_t hyperdecks;
  uint8_t dve;
  uint8_t stingers;
  uint8_t supersources;
  uint8_t talkback_channels;
  uint8_t camera_control;
};

enum class UskDveKeyFrame : uint8_t { A = 1, B, FULL, RUN_TO_INF };

struct DveState {
  int size_x;
  int size_y;
  int pos_x;
  int pos_y;
  int rotation;
};

enum class UskDveProperty { SIZE_X, SIZE_Y, POS_X, POS_Y, ROTATION };

struct DveProperty {
  UskDveProperty property;
  int value;
};

struct ProtocolVersion {
  uint16_t major;
  uint16_t minor;
};

struct MediaPlayer {
  uint8_t still;
  uint8_t clip;
};

struct MediaPlayerSource {
  uint8_t type;
  uint8_t still_index;
  uint8_t clip_index;
};

enum class UskKeyerType : uint8_t {
  LUMA = 0,
  CHROMA = 1,
  PATTERN = 2,
  DVE = 3,
};

struct UskState {
  UskKeyerType type;

  bool flying_key_enabled;

  Source fill;
  Source key;

  bool mask_enabled;
  int16_t top;
  int16_t bottom;
  int16_t left;
  int16_t right;
};

struct Usk {
  AtemState<UskState> state;
  /**
   * @brief At which keyframe the DVE is currently
   * 0 - No keyframe
   * 1 - A
   * 2 - B
   * 4 - Full
   * 8 - Run-to-inf
   */
  AtemState<uint8_t> at_key_frame;
  AtemState<DveState> dve;
};

struct DskState {
  bool on_air;
  bool in_transition;
  bool is_auto_transitioning;
};

struct DskSource {
  Source fill;
  Source key;
};

struct DskProperties {
  bool tie;
};

struct Dsk {
  AtemState<DskState> state;
  AtemState<DskSource> source;
  AtemState<DskProperties> properties;
};

struct FadeToBlack {
  bool fully_black;
  bool in_transition;
};

struct MixEffect {
  AtemState<Source> program;
  AtemState<Source> preview;
  AtemState<uint16_t> usk_on_air;
  struct {
    AtemState<TransitionPosition> position;
    AtemState<TransitionState> state;
  } transition;
  AtemState<FadeToBlack> ftb;
  std::vector<Usk> keyer;
};

enum class StreamState { IDLE = 1, STARTING = 2, STREAMING = 4 };

}  // namespace atem

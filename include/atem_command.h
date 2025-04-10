/**
 * @file atem_command.h
 * @author Wouter (atem_esp_idf@wjt.je)
 * @brief Provides all the different command that can be send by or send to an
 * ATEM.
 *
 * @copyright Copyright (c) 2023 - Wouter (wjtje)
 */
#pragma once
#include <lwip/inet.h>
#include <stdint.h>
#include <stdlib.h>

#include <initializer_list>
#include <optional>
#include <tuple>
#include <utility>

#include "atem_types.h"

#define ATEM_CMD(s)                                                    \
  ((uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | \
   (uint32_t)s[3])

namespace atem {

/**
 * @brief Base class of all commands that can be send to the ATEM.
 *
 */
class AtemCommand {
 public:
  AtemCommand(void *data) : has_alloc_(false), data_(data) {}
  /**
   * @brief Construct a new Atem Command object
   *
   * @param cmd
   * @param length Length is calculated by combining the header (8 bytes) + the
   * data
   */
  AtemCommand(const char *cmd, uint16_t length);
  virtual ~AtemCommand();

  /**
   * @brief Prepair the command for sending, this can be used when some protocol
   * version requires a different syntax.
   *
   * This will automaticly be executes when sending commands.
   *
   * @param version
   */
  virtual void PrepairCommand(const ProtocolVersion &ver) {}

  /**
   * @brief Get the length of the command, this include the 8 bytes for the
   * header
   *
   * @return uint16_t
   */
  uint16_t GetLength() { return ntohs(((uint16_t *)this->data_)[0]); }
  /**
   * @brief Get the cmd, this isn't null terminated
   *
   * @return void*
   */
  void *GetCmd() { return (uint8_t *)this->data_ + 4; }
  /**
   * @brief Get the data from the command (excluding the header)
   *
   * @tparam T
   * @return T
   */
  template <typename T>
  T GetData() {
    return (T)((uint8_t *)this->data_ + 8);
  }
  /**
   * @brief Get the data from the command (excluding the header)
   *
   * @param i
   * @return The byte at i position
   */
  template <typename T = uint8_t>
  T GetData(size_t i) {
    return T(((uint8_t *)this->data_)[8 + i]);
  }
  /**
   * @brief Get the data from the command (excluding the header), short (2
   * bytes) allinged. This function auto converts from network order to host
   * order.
   *
   * @tparam T
   * @param i
   * @return T
   */
  template <typename T>
  T GetDataS(size_t i) {
    return (T)ntohs(((uint16_t *)this->data_)[4 + i]);
  }
  /**
   * @brief Get the data from the command (excluding the header), long (4
   * bytes) allinged. This function auto converts from network order to host
   * order.
   *
   * @tparam T
   * @param i
   * @return T
   */
  template <typename T>
  T GetDataL(size_t i) {
    return (T)ntohl(((uint32_t *)this->data_)[2 + i]);
  }
  /**
   * @brief Get the access to the raw buffer, use GetLength to get the size of
   * the buffer
   *
   * @return const void*
   */
  const void *GetRawData() { return this->data_; }

 protected:
  bool has_alloc_{true};
  void *data_;
};

namespace cmd {

class Auto : public AtemCommand {
 public:
  /**
   * @brief Perform an AUTO transition on a MixEffect
   *
   * @param me[in] Which MixEffect to perform this action on
   */
  Auto(uint8_t me) : AtemCommand("DAut", 12) { GetData<uint8_t *>()[0] = me; }
};

class AuxInput : public AtemCommand {
 public:
  /**
   * @brief Change the source on a specific AUX channel
   *
   * @param source[in] The new source for the AUX channel
   * @param channel[in] Which AUX channel to change
   */
  AuxInput(Source source, uint8_t channel) : AtemCommand("CAuS", 12) {
    GetData<uint8_t *>()[0] = 1;
    GetData<uint8_t *>()[1] = channel;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class CaptureStill : public AtemCommand {
 public:
  CaptureStill() : AtemCommand("Capt", 8) {}
};

class Cut : public AtemCommand {
 public:
  /**
   * @brief Perform a CUT transition on a MixEffect
   *
   * @param me[in] Which MixEffect to perform the action on
   */
  Cut(uint8_t me) : AtemCommand("DCut", 12) { GetData<uint8_t *>()[0] = me; }
};

class DskAuto : public AtemCommand {
 public:
  /**
   * @brief Perform a AUTO transition on a Downstream Keyer
   *
   * @param keyer[in] Which keyer to perform the action on
   */
  DskAuto(uint8_t keyer) : AtemCommand("DDsA", 12), keyer_(keyer) {}
  void PrepairCommand(const ProtocolVersion &ver) override {
    if (ver.major <= 2 && ver.minor <= 27) {
      GetData<uint8_t *>()[0] = this->keyer_;
    } else {
      GetData<uint8_t *>()[1] = this->keyer_;
    }
  }

 protected:
  uint8_t keyer_;
};

class DskOnAir : public AtemCommand {
 public:
  /**
   * @brief Change the on air state of a Downstream Keyer
   *
   * @param state[in] The new stata
   * @param keyer[in] Which keyer to perform the action on
   */
  DskOnAir(bool state, uint8_t keyer) : AtemCommand("CDsL", 12) {
    GetData<uint8_t *>()[0] = keyer;
    GetData<uint8_t *>()[1] = state;
  }
};

class DskFill : public AtemCommand {
 public:
  /**
   * @brief Change the fill source on a Downstream Keyer
   *
   * @param source[in] The new source
   * @param keyer[in] Which keyer to perform the action on
   */
  DskFill(Source source, uint8_t keyer) : AtemCommand("CDsF", 12) {
    GetData<uint8_t *>()[0] = keyer;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class DskKey : public AtemCommand {
 public:
  /**
   * @brief Change the key source on a Downstream Keye
   *
   * @param source[in] The new source
   * @param keyer[in] Which keyer to perform the action on
   */
  DskKey(Source source, uint8_t keyer) : AtemCommand("CDsC", 12) {
    GetData<uint8_t *>()[0] = keyer;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class DskTie : public AtemCommand {
 public:
  /**
   * @brief Change the tie state of a Downstream Keyer
   *
   * @param state[in] The new state
   * @param keyer[in] Which keyer to perform the action on
   */
  DskTie(bool state, uint8_t keyer) : AtemCommand("CDsT", 12) {
    GetData<uint8_t *>()[0] = keyer;
    GetData<uint8_t *>()[1] = state;
  }
};

class FadeToBlack : public AtemCommand {
 public:
  /**
   * @brief Perform a Fade to Black action on a specific MixEffect
   *
   * @param me[in] Which MixEffect to perform this action on
   */
  FadeToBlack(uint8_t me) : AtemCommand("FtbA", 12) {
    GetData<uint8_t *>()[0] = me;
  }
};

class MediaPlayerSource : public AtemCommand {
 public:
  /**
   * @brief Change the source of a mediaplayer
   *
   * @param [in] mediaplayer Which media player to change
   * @param [in] mask Which fields are valid (3 for type and still)
   * @param [in|optional] type Which type the media player is in (1 for still)
   * @param [in|optional] still The new still source
   * @param [in|optional] clip The new clip source
   */
  MediaPlayerSource(
      uint8_t mediaplayer, uint8_t mask, uint8_t type, uint8_t still,
      uint8_t clip
  )
      : AtemCommand("MPSS", 16) {
    GetData<uint8_t *>()[0] = mask;
    GetData<uint8_t *>()[1] = mediaplayer;
    GetData<uint8_t *>()[2] = type;
    GetData<uint8_t *>()[3] = still;
    GetData<uint8_t *>()[4] = clip;
  }
};

class UskDveKeyFrameProperties : public AtemCommand {
 public:
  /**
   * @brief Change the DVE properties of an Upstream Keyer.
   *
   * @tparam Container
   * @tparam Args
   * @param [in] me Which MixEffect to perform this action on
   * @param [in] keyer Which Upstream Keyer to perform this action on
   * @param [in] key_frame Which frame to change (A or B)
   * @param [in] p A list of properties to change
   */
  template <template <class...> class Container, class... Args>
  UskDveKeyFrameProperties(
      uint8_t me, uint8_t keyer, UskDveKeyFrame key_frame,
      Container<std::pair<UskDveProperty, int>, Args...> &p
  )
      : AtemCommand("CKFP", 64) {
    uint32_t mask = 0;

    for (auto c : p) {
      const auto &[property, value] = c;
      mask |= 1 << (uint8_t)property;
      ((uint32_t *)this->data_)[4 + (uint8_t)property] = htonl(value);
    }

    GetData<uint32_t *>()[0] = htonl(mask);
    GetData<uint8_t *>()[4] = me;
    GetData<uint8_t *>()[5] = keyer;
    GetData<uint8_t *>()[6] = std::to_underlying(key_frame);
  }
};

class UskDveRunFlyingKey : public AtemCommand {
 public:
  /**
   * @brief Perform a run to keyframe a keyer
   *
   * @param [in] me Which MixEffect to perform this action on
   * @param [in] keyer Which Upstream Keyer to perform this action on
   * @param [in] key_frame Which frame to change (A or B)
   * @param [in] run_to_inf_i Run to infinite index
   */
  UskDveRunFlyingKey(
      uint8_t me, uint8_t keyer, UskDveKeyFrame key_frame,
      std::optional<uint8_t> run_to_inf_i = std::nullopt
  )
      : AtemCommand("RFlK", 16) {
    GetData<uint8_t *>()[0] = 0;
    GetData<uint8_t *>()[1] = me;
    GetData<uint8_t *>()[2] = keyer;
    GetData<uint8_t *>()[4] = std::to_underlying(key_frame);
    GetData<uint8_t *>()[5] = run_to_inf_i.value_or(0);
  }
};

class UskDveProperties : public AtemCommand {
 public:
  /**
   * @brief Change the current state of the DVE on a Upstream Keyer
   *
   * @tparam Container
   * @tparam Args
   * @param [in] me Which MixEffect to perform this action on
   * @param [in] keyer Which Upstream Keyer to perform this action on
   * @param [in] p List of properties to change
   */
  template <template <class...> class Container, class... Args>
  UskDveProperties(
      uint8_t me, uint8_t keyer,
      Container<std::pair<UskDveProperty, int>, Args...> &p
  )
      : AtemCommand("CKDV", 72) {
    uint32_t mask = 0;

    for (auto c : p) {
      const auto &[property, value] = c;
      mask |= 1 << (uint8_t)property;
      ((uint32_t *)this->data_)[4 + (uint8_t)property] = htonl(value);
    }

    GetData<uint32_t *>()[0] = htonl(mask);
    GetData<uint8_t *>()[4] = me;
    GetData<uint8_t *>()[5] = keyer;
  }
};

class UskFill : public AtemCommand {
 public:
  /**
   * @brief Change the fill source on a Upstream Keyer
   *
   * @param [in] me Which MixEffect to perform this action on
   * @param [in] keyer Which Upstream Keyer to perform this action on
   * @param [in] source The new source
   */
  UskFill(uint8_t me, uint8_t keyer, Source source) : AtemCommand("CKeF", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint8_t *>()[1] = keyer;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class UskKey : public AtemCommand {
 public:
  /**
   * @brief Change the key source on a Upstream Keyer
   * @warning This only works if the type is a LUMA keyer
   *
   * @param [in] me Which MixEffect to perform this action on
   * @param [in] keyer Which Upstream Keyer to perform this action on
   * @param [in] source The new source
   */
  UskKey(uint8_t me, uint8_t keyer, Source source) : AtemCommand("CKeC", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint8_t *>()[1] = keyer;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class UskType : public AtemCommand {
 public:
  /**
   * @brief Change the type of the Upstream Keyer
   *
   * @param [in] me Which MixEffect to perform this action on
   * @param [in] keyer Which Upstream Keyer to perform this action on
   * @param [in] type The type of the USK
   * @param [in] flying_key_enabled
   */
  UskType(
      uint8_t me, int8_t keyer, std::optional<UskKeyerType> type,
      std::optional<bool> flying_key_enabled
  )
      : AtemCommand("CKTp", 16) {
    uint8_t mask = 0;

    GetData<uint8_t *>()[1] = me;
    GetData<uint8_t *>()[2] = keyer;

    if (type.has_value()) {
      mask |= 0x01;
      GetData<uint8_t *>()[3] = std::to_underlying(*type);
    }

    if (flying_key_enabled.has_value()) {
      mask |= 0x02;
      GetData<uint8_t *>()[4] = *flying_key_enabled ? 1 : 0;
    }

    GetData<uint8_t *>()[0] = mask;
  }
};

class UskOnAir : public AtemCommand {
 public:
  /**
   * @brief Change the On air state of a Upstream Keyer
   *
   * @param [in] me Which MixEffect to perform this action on
   * @param [in] keyer Which Upstream Keyer to perform this action on
   * @param [in] enabled The new state of the USK
   */
  UskOnAir(uint8_t me, uint8_t key, bool enabled) : AtemCommand("CKOn", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint8_t *>()[1] = key;
    GetData<uint8_t *>()[2] = enabled;
  }
};

class PreviewInput : public AtemCommand {
 public:
  /**
   * @brief Change the preview source on a MixEffect
   *
   * @param source[in] The new preview source
   * @param me[in] Which MixEffect to perform this action on
   */
  PreviewInput(Source source, uint8_t me) : AtemCommand("CPvI", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class ProgramInput : public AtemCommand {
 public:
  /**
   * @brief Change the program source on a MixEffect
   *
   * @param source[in] The new program source
   * @param me[in] Which MixEffect to perform this action on
   */
  ProgramInput(Source source, uint8_t me) : AtemCommand("CPgI", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class SaveStartupState : public AtemCommand {
 public:
  /**
   * @brief Save the current state of the ATEM as its startup state
   */
  SaveStartupState() : AtemCommand("SRsv", 12) { GetData<uint32_t *>()[0] = 0; }
};

class Stream : public AtemCommand {
 public:
  /**
   * @brief Start or stop streaming.
   *
   * @param state[in] The new state
   */
  Stream(bool state) : AtemCommand("StrR", 12) {
    GetData<uint8_t *>()[0] = state;
  }
};

class TransitionPosition : public AtemCommand {
 public:
  /**
   * @brief Change the AUTO transition position
   *
   * @param position[in] The new position (between 0 and 10000)
   * @param me[in] Which MixEffect to perform this action on
   */
  TransitionPosition(uint16_t position, uint8_t me) : AtemCommand("CTPs", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint16_t *>()[1] = htons(position);
  }
};

class TransitionState : public AtemCommand {
 public:
  /**
   * @brief The transition state of the MixEffect
   *
   * @param next[in] A bitmask of the Keyers active
   * @param me[in] Which MixEffect to perform this action on
   */
  TransitionState(
      std::optional<TransitionStyle> style, std::optional<uint8_t> next,
      uint8_t me
  )
      : AtemCommand("CTTp", 12) {
    GetData<uint8_t *>()[0] =
        (style.has_value() ? (1 << 0) : 0) | (next.has_value() ? (1 << 1) : 0);
    GetData<uint8_t *>()[1] = me;
    GetData<uint8_t *>()[2] = style.value_or(TransitionStyle::MIX);
    GetData<uint8_t *>()[3] = next.value_or(0);
  }
};

}  // namespace cmd

}  // namespace atem

/**
 * @file atem_command.h
 * @author Wouter van der Wal (me@wjtje.dev)
 * @brief Provides all the different command that can be send by or send to an
 * ATEM.
 *
 * @copyright Copyright (c) 2023 - Wouter van der Wal
 */
#pragma once
#include <lwip/inet.h>
#include <stdint.h>
#include <stdlib.h>

#include <initializer_list>
#include <tuple>

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
  virtual void PrepairCommand(types::ProtocolVersion ver) {}

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
  uint8_t GetData(size_t i) { return ((uint8_t *)this->data_)[8 + i]; }
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
    return (T)ntohl(((uint16_t *)this->data_)[2 + i]);
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
  AuxInput(types::Source source, uint8_t channel) : AtemCommand("CAuS", 12) {
    GetData<uint8_t *>()[0] = 1;
    GetData<uint8_t *>()[1] = channel;
    GetData<uint16_t *>()[1] = htons(source);
  }
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
  void PrepairCommand(types::ProtocolVersion ver) override {
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
  DskFill(types::Source source, uint8_t keyer) : AtemCommand("CDsF", 12) {
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
  DskKey(types::Source source, uint8_t keyer) : AtemCommand("CDsC", 12) {
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
   * @param mediaplayer[in] Which media player to change
   * @param mask[in] Which fields are valid (3 for type and still)
   * @param type[in|optional] Which type the media player is in (1 for still)
   * @param still[in|optional] The new still source
   * @param clip[in|optional] The new clip source
   */
  MediaPlayerSource(uint8_t mediaplayer, uint8_t mask, uint8_t type,
                    uint8_t still, uint8_t clip)
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
   * @param key_frame[in] Which frame to change (A or B)
   * @param p[in] A list of properties to change
   * @param keyer[in] Which Upstream Keyer to perform this action on
   * @param me[in] Which MixEffect to perform this action on
   */
  UskDveKeyFrameProperties(
      types::UskDveKeyFrame key_frame,
      std::initializer_list<std::tuple<types::UskDveProperty, int>> p,
      uint8_t keyer, uint8_t me)
      : AtemCommand("CKFP", 64) {
    uint32_t mask = 0;

    for (auto c : p) {
      const auto [property, value] = c;
      mask |= 1 << (uint8_t)property;
      ((uint32_t *)this->data_)[4 + (uint8_t)property] = htonl(value);
    }

    GetData<uint32_t *>()[0] = htonl(mask);
    GetData<uint8_t *>()[4] = me;
    GetData<uint8_t *>()[5] = keyer;
    GetData<uint8_t *>()[6] = (uint8_t)key_frame;
  }
};

class UskDveRunFlyingKey : public AtemCommand {
 public:
  /**
   * @brief Perform a Run to INF on a specific Upsteam Keyer
   *
   * @param key_frame[in] Which frame to change (A or B)
   * @param run_to_inf_i[in] Run to infinite index
   * @param keyer[in] Which Upstream Keyer to perform this action on
   * @param me[in] Which MixEffect to perform this action on
   */
  UskDveRunFlyingKey(types::UskDveKeyFrame key_frame, uint8_t run_to_inf_i,
                     uint8_t keyer, uint8_t me)
      : AtemCommand("RFlK", 16) {
    GetData<uint8_t *>()[0] = 0;
    GetData<uint8_t *>()[1] = me;
    GetData<uint8_t *>()[2] = keyer;
    GetData<uint8_t *>()[4] = (uint8_t)key_frame;
    GetData<uint8_t *>()[5] = run_to_inf_i;
  }
};

class UskDveProperties : public AtemCommand {
 public:
  /**
   * @brief Change the current state of the DVE on a Upstream Keyer
   *
   * @param p[in] List of properties to change
   * @param keyer[in] Which Upstream Keyer to perform this action on
   * @param me[in] Which MixEffect to perform this action on
   */
  UskDveProperties(
      std::initializer_list<std::tuple<types::UskDveProperty, int>> p,
      uint8_t keyer, uint8_t me)
      : AtemCommand("CKDV", 72) {
    uint32_t mask = 0;

    for (auto c : p) {
      const auto [property, value] = c;
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
   * @param source[in] The new source
   * @param keyer[in] Which Upstream Keyer to perform this action on
   * @param me[in] Which MixEffect to perform this action on
   */
  UskFill(types::Source source, uint8_t keyer, uint8_t me)
      : AtemCommand("CKeF", 12) {
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
   * @param type[in] The type of the USK (2 is DVE)
   * @param keyer[in] Which Upstream Keyer to perform this action on
   * @param me[in] Which MixEffect to perform this action on
   */
  UskType(uint8_t type, uint8_t keyer, uint8_t me) : AtemCommand("CKTp", 16) {
    GetData<uint8_t *>()[0] = 1;  // Mask
    GetData<uint8_t *>()[1] = me;
    GetData<uint8_t *>()[2] = keyer;
    GetData<uint8_t *>()[3] = type;
  }
};

class UskOnAir : public AtemCommand {
 public:
  /**
   * @brief Change the On air state of a Upstream Keyer
   *
   * @param enabled[in] The new state of the USK
   * @param keyer[in] Which Upstream Keyer to perform this action on
   * @param me[in] Which MixEffect to perform this action on
   */
  UskOnAir(bool enabled, uint8_t key, uint8_t me) : AtemCommand("CKOn", 12) {
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
  PreviewInput(types::Source source, uint8_t me) : AtemCommand("CPvI", 12) {
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
  ProgramInput(types::Source source, uint8_t me) : AtemCommand("CPgI", 12) {
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
  TransitionState(uint8_t next, uint8_t me) : AtemCommand("CTTp", 12) {
    GetData<uint8_t *>()[0] = 0x2;  // Mask
    GetData<uint8_t *>()[1] = me;
    GetData<uint8_t *>()[3] = next;
  }
};

}  // namespace cmd

}  // namespace atem

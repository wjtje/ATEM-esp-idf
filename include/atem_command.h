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
  Auto(uint8_t me = 0) : AtemCommand("DAut", 12) {
    GetData<uint8_t *>()[0] = me;
  }
};

class AuxInput : public AtemCommand {
 public:
  AuxInput(types::Source source, uint8_t channel = 0)
      : AtemCommand("CAuS", 12) {
    GetData<uint8_t *>()[0] = 1;
    GetData<uint8_t *>()[1] = channel;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class Cut : public AtemCommand {
 public:
  Cut(uint8_t me = 0) : AtemCommand("DCut", 12) {
    GetData<uint8_t *>()[0] = me;
  }
};

class DskAuto : public AtemCommand {
 public:
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
  DskOnAir(bool state, uint8_t keyer) : AtemCommand("CDsL", 12) {
    GetData<uint8_t *>()[0] = keyer;
    GetData<uint8_t *>()[1] = state;
  }
};

class DskFill : public AtemCommand {
 public:
  DskFill(types::Source source, uint8_t keyer) : AtemCommand("CDsF", 12) {
    GetData<uint8_t *>()[0] = keyer;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class DskKey : public AtemCommand {
 public:
  DskKey(types::Source source, uint8_t keyer) : AtemCommand("CDsC", 12) {
    GetData<uint8_t *>()[0] = keyer;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class DskTie : public AtemCommand {
 public:
  DskTie(bool state, uint8_t keyer) : AtemCommand("CDsT", 12) {
    GetData<uint8_t *>()[0] = keyer;
    GetData<uint8_t *>()[1] = state;
  }
};

class FadeToBlack : public AtemCommand {
 public:
  FadeToBlack(uint8_t me) : AtemCommand("FtbA", 12) {
    GetData<uint8_t *>()[0] = me;
  }
};

class MediaPlayerSource : public AtemCommand {
 public:
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
  UskDveKeyFrameProperties(
      types::UskDveKeyFrame key_frame,
      std::initializer_list<std::tuple<types::UskDveProperty, int>> p,
      uint8_t keyer = 0, uint8_t me = 0)
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
   * @brief Construct a new Usk Dve Run Flying Key object
   *
   * @param key_frame
   * @param run_to_inf_i Run to infinite index
   * @param keyer Which keyer to use
   * @param me On which me
   */
  UskDveRunFlyingKey(types::UskDveKeyFrame key_frame, uint8_t run_to_inf_i = 0,
                     uint8_t keyer = 0, uint8_t me = 0)
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
  UskDveProperties(
      std::initializer_list<std::tuple<types::UskDveProperty, int>> p,
      uint8_t keyer = 0, uint8_t me = 0)
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
  UskFill(types::Source source, uint8_t keyer = 0, uint8_t me = 0)
      : AtemCommand("CKeF", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint8_t *>()[1] = keyer;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class UskType : public AtemCommand {
 public:
  UskType(uint8_t type, uint8_t keyer = 0, uint8_t me = 0)
      : AtemCommand("CKTp", 16) {
    GetData<uint8_t *>()[0] = 1;  // Mask
    GetData<uint8_t *>()[1] = me;
    GetData<uint8_t *>()[2] = keyer;
    GetData<uint8_t *>()[3] = type;
  }
};

class UskOnAir : public AtemCommand {
 public:
  UskOnAir(bool enabled, uint8_t key = 0, uint8_t me = 0)
      : AtemCommand("CKOn", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint8_t *>()[1] = key;
    GetData<uint8_t *>()[2] = enabled;
  }
};

class PreviewInput : public AtemCommand {
 public:
  PreviewInput(types::Source source, uint8_t me = 0) : AtemCommand("CPvI", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class ProgramInput : public AtemCommand {
 public:
  ProgramInput(types::Source source, uint8_t me = 0) : AtemCommand("CPgI", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint16_t *>()[1] = htons(source);
  }
};

class SaveStartupState : public AtemCommand {
 public:
  SaveStartupState() : AtemCommand("SRsv", 12) { GetData<uint32_t *>()[0] = 0; }
};

class TransitionPosition : public AtemCommand {
 public:
  TransitionPosition(uint16_t position, uint8_t me = 0)
      : AtemCommand("CTPs", 12) {
    GetData<uint8_t *>()[0] = me;
    GetData<uint16_t *>()[1] = htons(position);
  }
};

class TransitionState : public AtemCommand {
 public:
  TransitionState(uint8_t next, uint8_t me = 0) : AtemCommand("CTTp", 12) {
    GetData<uint8_t *>()[0] = 0x2;  // Mask
    GetData<uint8_t *>()[1] = me;
    GetData<uint8_t *>()[3] = next;
  }
};

}  // namespace cmd

}  // namespace atem

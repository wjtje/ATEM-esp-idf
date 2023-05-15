#pragma once
#include <lwip/inet.h>
#include <stdint.h>
#include <stdlib.h>

#include <initializer_list>
#include <tuple>

#include "atem_types.h"

namespace atem {

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
  ~AtemCommand();

  bool operator==(const char *b) { return !memcmp(this->GetCmd(), b, 4); }

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
   * @return void*
   */
  void *GetData() { return (uint8_t *)this->data_ + 8; }
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
    ((uint8_t *)this->data_)[8] = me;
  }
};

class AuxInput : public AtemCommand {
 public:
  AuxInput(Source source, uint8_t channel = 0) : AtemCommand("CAuS", 12) {
    ((uint8_t *)this->data_)[8] = 1;
    ((uint8_t *)this->data_)[9] = channel;
    ((uint16_t *)this->data_)[5] = htons(source);
  }
};

class Cut : public AtemCommand {
 public:
  Cut(uint8_t me = 0) : AtemCommand("DCut", 12) {
    ((uint8_t *)this->data_)[8] = me;
  }
};

class UskDveKeyFrameProperties : public AtemCommand {
 public:
  UskDveKeyFrameProperties(
      std::initializer_list<std::tuple<DveProperty, int>> p, uint8_t kf = 1,
      uint8_t keyer = 0, uint8_t me = 0)
      : AtemCommand("CKFP", 64) {
    uint32_t mask = 0;

    for (auto c : p) {
      const auto [property, value] = c;
      mask |= 1 << (uint8_t)property;
      ((uint32_t *)this->data_)[4 + (uint8_t)property] = htonl(value);
    }

    ((uint32_t *)this->data_)[2] = htonl(mask);
    ((uint8_t *)this->data_)[12] = me;
    ((uint8_t *)this->data_)[13] = keyer;
    ((uint8_t *)this->data_)[14] = kf;
  }
};

class UskDveKeyFrameRun : public AtemCommand {
 public:
  UskDveKeyFrameRun(uint8_t kf = 1, uint8_t keyer = 0, uint8_t me = 0)
      : AtemCommand("RFlK", 16) {
    ((uint8_t *)this->data_)[8] = 0;
    ((uint8_t *)this->data_)[9] = me;
    ((uint8_t *)this->data_)[10] = keyer;
    ((uint8_t *)this->data_)[12] = kf;
  }
};

class UskDveProperties : public AtemCommand {
 public:
  UskDveProperties(std::initializer_list<std::tuple<DveProperty, int>> p,
                   uint8_t keyer = 0, uint8_t me = 0)
      : AtemCommand("CKDV", 72) {
    uint32_t mask = 0;

    for (auto c : p) {
      const auto [property, value] = c;
      mask |= 1 << (uint8_t)property;
      ((uint32_t *)this->data_)[4 + (uint8_t)property] = htonl(value);
    }

    ((uint32_t *)this->data_)[2] = htonl(mask);
    ((uint8_t *)this->data_)[12] = me;
    ((uint8_t *)this->data_)[13] = keyer;
  }
};

class UskOnAir : public AtemCommand {
 public:
  UskOnAir(bool enabled, uint8_t key = 0, uint8_t me = 0)
      : AtemCommand("CKOn", 12) {
    ((uint8_t *)this->data_)[8] = me;
    ((uint8_t *)this->data_)[9] = key;
    ((uint8_t *)this->data_)[10] = enabled;
  }
};

class PreviewInput : public AtemCommand {
 public:
  PreviewInput(Source source, uint8_t me = 0) : AtemCommand("CPvI", 12) {
    ((uint8_t *)this->data_)[8] = me;
    ((uint16_t *)this->data_)[5] = htons(source);
  }
};

class ProgramInput : public AtemCommand {
 public:
  ProgramInput(Source source, uint8_t me = 0) : AtemCommand("CPgI", 12) {
    ((uint8_t *)this->data_)[8] = me;
    ((uint16_t *)this->data_)[5] = htons(source);
  }
};

}  // namespace cmd

}  // namespace atem
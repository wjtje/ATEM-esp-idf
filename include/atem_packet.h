/**
 * @file atem_packet.h
 * @author Wouter (atem_esp_idf@wjt.je)
 * @brief Contains function to parse a UDP packet create by an ATEM or create a
 * valid one.
 *
 * @copyright Copyright (c) 2023 - Wouter (wjtje)
 */
#pragma once
#include <cstring>

#include "atem_command.h"

namespace atem {

/**
 * @brief This class is a wrapper for a raw buffer, that can decode ATEM data.
 *
 */
class AtemPacket {
 public:
  AtemPacket(void* data) : has_alloc_(false), data_(data) {}
  AtemPacket(uint8_t flags, uint16_t session, uint16_t length);
  ~AtemPacket();

  /**
   * @brief Get access to the raw data buffer, advance use only
   *
   * @return void*
   */
  void* GetData() { return this->data_; }
  const void* GetData() const { return this->data_; }
  /**
   * @brief Get the flags of the packet. e.g. What kind of data this packet
   * contains.
   *
   * @note 0x1 - ACK (request)
   * @note 0x2 - INIT
   * @note 0x4 - RETRANSMISSION
   * @note 0x8 - RESEND
   * @note 0x10 - ACK (response)
   *
   * @return uint8_t
   */
  uint8_t GetFlags() const { return ((uint8_t*)this->data_)[0] >> 3; }
  /**
   * @brief Get the length of the packet, this includes the length of all
   * headers
   *
   * @return uint16_t
   */
  uint16_t GetLength() const { return ntohs(*(uint16_t*)this->data_) & 0x07FF; }
  /**
   * @brief Get the uniqe ID for this session with the ATEM.
   *
   * @return uint16_t
   */
  uint16_t GetSessionId() const { return ntohs(((uint16_t*)this->data_)[1]); }
  /**
   * @brief Get the id of the packet that has been ACKed. This is only valid
   * when this->GetFlags() & 0x10.
   *
   * @return int16_t
   */
  int16_t GetAckId() const { return ntohs(((int16_t*)this->data_)[2]); }
  /**
   * @brief Get the id of the packet that needs to be resend. This is only
   * valid when this->GetFlags() & 0x8.
   *
   * @return int16_t
   */
  int16_t GetResendId() const { return ntohs(((int16_t*)this->data_)[3]); }
  int16_t GetId() const { return ntohs(((int16_t*)this->data_)[5]); }

  void SetFlags(uint8_t flags) {
    const uint8_t byte = ((uint8_t*)this->data_)[0];
    ((uint8_t*)this->data_)[0] = flags << 3 | (byte & 0x7);
  }
  void SetAckId(int16_t id) { ((int16_t*)this->data_)[2] = htons(id); }
  void SetResendId(int16_t id) { ((int16_t*)this->data_)[3] = htons(id); }
  void SetUnknown(int16_t id) { ((int16_t*)this->data_)[4] = htons(id); }
  void SetId(int16_t id) { ((int16_t*)this->data_)[5] = htons(id); }

  struct Iterator {
    Iterator(void* buff, uint16_t i) : buff_(buff), i_(i) {}

    AtemCommand operator*() const {
      return AtemCommand((uint8_t*)this->buff_ + this->i_);
    }
    Iterator& operator++() {
      this->i_ += this->operator*().GetLength();
      return *this;
    }
    Iterator operator++(int) {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    friend bool operator==(const Iterator& a, const Iterator& b) {
      return a.buff_ == b.buff_ && a.i_ == b.i_;
    }
    friend bool operator!=(const Iterator& a, const Iterator& b) {
      return a.buff_ != b.buff_ || a.i_ != b.i_;
    }

    void* buff_;
    uint16_t i_;
  };

  /**
   * @brief Returns an Iterator for all command inside this packet.
   *
   * @return Iterator
   */
  Iterator begin() { return Iterator(this->data_, 12); }
  Iterator end() { return Iterator(this->data_, this->GetLength()); }

 protected:
  bool has_alloc_{true};
  void* data_{nullptr};
};

}  // namespace atem

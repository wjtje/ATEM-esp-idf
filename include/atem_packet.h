#pragma once
#include <lwip/udp.h>

#include "atem_command.h"

namespace atem {

class AtemPacket {
 public:
  AtemPacket(void* dataptr);
  AtemPacket(uint8_t flags, uint16_t session, uint16_t length);
  ~AtemPacket() { free(this->data_); }

  /**
   * @brief Get access to the raw data buffer, advance use only
   *
   * @return void*
   */
  void* GetData() { return this->data_; }
  /**
   * @brief Get the flags of the packet
   *
   * @return uint8_t
   */
  uint8_t GetFlags() { return ((uint8_t*)this->data_)[0] >> 3; }
  /**
   * @brief Get the length of the packet, this includes the length of all
   * headers
   *
   * @return uint16_t
   */
  uint16_t GetLength() { return ntohs(*(uint16_t*)this->data_) & 0x07FF; }
  uint16_t GetSessionId() { return ntohs(((uint16_t*)this->data_)[1]); }
  uint16_t GetAckId() { return ntohs(((uint16_t*)this->data_)[2]); }
  uint16_t GetResendId() { return ntohs(((uint16_t*)this->data_)[3]); }
  uint16_t GetLocalId() { return ntohs(((uint16_t*)this->data_)[4]); }
  uint16_t GetRemoteId() { return ntohs(((uint16_t*)this->data_)[5]); }

  void SetAckId(uint16_t id) { ((uint16_t*)this->data_)[2] = htons(id); }
  void SetResendId(uint16_t id) { ((uint16_t*)this->data_)[3] = htons(id); }
  void SetLocalId(uint16_t id) { ((uint16_t*)this->data_)[4] = htons(id); }
  void SetRemoteId(uint16_t id) { ((uint16_t*)this->data_)[5] = htons(id); }

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

  Iterator begin() { return Iterator(this->data_, 12); }
  Iterator end() { return Iterator(this->data_, this->GetLength()); }

 protected:
  void* data_{nullptr};
};

}  // namespace atem

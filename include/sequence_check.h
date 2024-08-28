/**
 * @file sequence_check.h
 * @author Wouter (atem_esp_idf@wjt.je)
 * @brief A simple class that can detect if a packet id is out
 * of sequence while using minimal memory. (e.g. 6 bytes)
 *
 * @copyright Copyright (c) 2024 - Wouter (wjtje)
 */
#pragma once

#include <math.h>
#include <stdint.h>

#include "stdio.h"

namespace atem {

class SequenceCheck {
 public:
  SequenceCheck() {}

  /**
   * @brief Add a new ID to the sequece check
   *
   * @param id[in]
   * @return true When the id has been added to the buffer
   * @return false When the id was already received before.
   */
  bool Add(int16_t id) {
    last_id_ = id;

    // The size of the received_ buffer in bits
    const uint16_t recv_len = sizeof(this->received_) * 8;

    // Make room in the buffer
    uint16_t offset = (id - this->offset_) & INT16_MAX;
    if (offset < recv_len) {
      this->received_ <<= offset;
      this->offset_ = id;
    }

    // Offset maybe moved, recalculate
    offset = abs(id - this->offset_);

    // Check if already received before
    if (this->received_ & 1 << offset) return false;

    // Add the id to the received buffer
    this->received_ |= 1 << offset;
    return true;
  }
  /**
   * @brief Returns the ID of the missing packet
   *
   * @return int16_t The ID of the missing packet, -1 if there is nothing
   * missing
   */
  int16_t GetMissing() const {
    if (this->received_ == UINT32_MAX) return -1;

    // The size of the received_ buffer in bits
    const uint16_t recv_len = sizeof(this->received_) * 8;

    for (int16_t i = recv_len - 1; i != 0; i--) {
      if (!(this->received_ & 1 << i)) {
        return ((this->offset_ - i) & INT16_MAX);
      }
    }

    return -1;  // How did we get here?
  }
  /**
   * @brief Returns the last id received (or added) using the Add function.
   *
   * @return int16_t
   */
  inline int16_t GetLastId() const { return this->last_id_; }
  /**
   * @brief Check weather or not the sequence contains this id
   *
   * @param id[in]
   * @return true
   * @return false
   */
  bool Contains(int16_t id) const {
    const uint16_t recv_len = sizeof(this->received_) * 8;
    const uint16_t offset = (this->offset_ - id) & INT16_MAX;
    return (offset < recv_len);
  }
  /**
   * @brief Returns weather the id is new than the last id
   *
   * @param id[in]
   * @return true When the parameter id is newer
   * @return false
   */
  inline bool IsNewer(int16_t id) const { return this->last_id_ < id; }

 protected:
  int16_t offset_{1}, last_id_{0};
  uint32_t received_{UINT32_MAX - 1};
};

}  // namespace atem

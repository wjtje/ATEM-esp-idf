/**
 * @file atem_state.h
 * @author Wouter (atem_esp_idf@wjt.je)
 * @brief A wrapper around variables to store extra information
 *
 * @copyright Copyright (c) 2024 - Wouter (wjtje)
 */
#pragma once

#include <stdint.h>

#include "sequence_check.h"

namespace atem {

template <typename T>
class AtemState {
 public:
  AtemState(const T& initial_state = T())
      : last_change_id_(0), state_(initial_state) {}

  ~AtemState() {}

  inline bool operator==(int16_t id) { return this->last_change_id_ == id; }
  inline bool operator<(const AtemState<T>& rhs) {
    return this.last_change_id_ < rhs.last_change_id_;
  }

  /**
   * @brief Returns a reference to the state.
   *
   * @return const T&
   */
  inline const T& Get() const { return this->state_; }
  /**
   * @brief Set the state to a new value. It will only be changed
   *
   * @param id[in] The packet id of the ATEM when this change has been made
   * @param state[in] The new state
   * @return bool This returns true when the state has been changed.
   */
  inline bool Set(SequenceCheck sequence, const T& state) {
    // Check if the current data is
    if (sequence.IsNewer(this->last_change_id_)) return false;
    this->state_ = state;
    return true;
  }
  /**
   * @brief This will "reset" the last change id. This can be executed when a
   * rollover has happened.
   */
  inline void ResetLastChangeId() { this->last_change_id_ = 0; }

 protected:
  int16_t last_change_id_;
  const T state_;
};

}  // namespace atem

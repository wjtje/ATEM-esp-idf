/**
 * @file atem_state.h
 * @author Wouter (atem_esp_idf@wjt.je)
 * @brief A wrapper around variables to store extra information
 *
 * @copyright Copyright (c) 2024 - Wouter (wjtje)
 */
#pragma once

#include <esp_log.h>
#include <stdint.h>

#if CONFIG_COMPILER_CXX_RTTI
#include <typeinfo>
#endif

#include "sequence_check.h"

namespace atem {

template <typename T>
class AtemState {
 public:
  AtemState(const SequenceCheck &sequence, const T &state)
      : last_change_id_(sequence.GetLastId()), state_(state) {}

  AtemState(const T &state = T()) : last_change_id_(INT16_MIN), state_(state) {}

  ~AtemState() = default;

  inline bool operator==(int16_t id) const {
    return this->last_change_id_ == id;
  }

  inline bool operator<(const AtemState<T> &rhs) const {
    return this.last_change_id_ < rhs.last_change_id_;
  }

  inline const T &operator*() const { return this->state_; }
  inline const T *operator->() const { return std::addressof(this->state_); }

  inline operator bool() const { return this->IsValid(); }

  /**
   * @brief Returns weather or not this variable is valid.
   *
   * @return true
   * @return false
   */
  inline bool IsValid() const { return this->last_change_id_ != INT16_MIN; }

  /**
   * @brief Returns true when the packet id is newer (or the same) as the last
   * change id of this state
   *
   * E.g. returns true when the state has newer data than the given id
   *
   * @param id
   * @return true
   * @return false
   */
  inline bool IsNewer(const int16_t id) const {
    if (!this->IsValid()) return false;
    if (id == 0) return true;
    return this->last_change_id_ > 0 && id <= this->last_change_id_;
  }

  /**
   * @brief Returns a reference to the state.
   *
   * @return const T&
   */
  inline const T &Get() const { return this->state_; }

  /**
   * @brief Set the state to a new value. It will only be changed
   *
   * @param [in] sequence The packet id of the ATEM when this change has been
   * made
   * @param [in] state The new state
   * @return bool This returns true when the state has been changed.
   */
  bool Set(const SequenceCheck &sequence, const T &state) {
    // Check if the current data is newer
    if (sequence.IsNewer(this->last_change_id_)) {
#if CONFIG_COMPILER_CXX_RTTI
      ESP_LOGD(
        "AtemState", "%s: %u > %u", typeid(T).name(), this->last_change_id_,
        sequence.GetLastId()
      );
#endif
      return false;
    }

    this->last_change_id_ = sequence.GetLastId();
    this->state_ = state;
    return true;
  }

  /**
   * @brief This will "reset" the last change id. This can be executed when a
   * rollover has happened.
   */
  inline void ResetLastChangeId() { this->last_change_id_ = INT16_MIN + 1; }

  /**
   * @brief Returns the packet id when this state was last changed.
   *
   * This can be INT16_MIN when it has not been set,
   * or INT16_MIN + 1 when there was a rollover.
   */
  inline int16_t GetPacketId() const { return this->last_change_id_; }

 protected:
  int16_t last_change_id_;
  T state_;
};

}  // namespace atem

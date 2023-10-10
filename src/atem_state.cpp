#include "atem.h"

namespace atem {

bool Atem::GetAuxOutput(types::Source* source, uint8_t channel) {
  bool valid = false;

  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    if (this->aux_out_ != nullptr && this->top_.aux > channel) {
      *source = this->aux_out_[channel];
      valid = true;
    }
    xSemaphoreGive(this->state_mutex_);
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetAuxOutput'");
  }

  return valid;
}

bool Atem::GetDskState(types::DskState* state, uint8_t keyer) {
  bool valid = false;

  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    if (this->dsk_ != nullptr && this->top_.dsk > keyer) {
      *state = this->dsk_[keyer];
      valid = true;
    }
    xSemaphoreGive(this->state_mutex_);
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetDskState'");
  }

  return valid;
}

bool Atem::GetMediaPlayer(types::MediaPlayer* state) {
  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    *state = this->mpl_;
    xSemaphoreGive(this->state_mutex_);
    return true;
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetMediaPlayer'");
  }

  return false;
}

bool Atem::GetMediaPlayerSource(types::MediaPlayerSource* state,
                                uint8_t mediaplayer) {
  bool valid = false;

  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    if (this->mps_ != nullptr && this->top_.mediaplayers > mediaplayer) {
      *state = this->mps_[mediaplayer];
      valid = true;
    }
    xSemaphoreGive(this->state_mutex_);
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetMediaPlayerSource'");
  }

  return valid;
}

bool Atem::GetPreviewInput(types::Source* source, uint8_t me) {
  bool valid = false;

  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    if (this->me_ != nullptr && this->top_.me > me) {
      *source = this->me_[me].preview;
      valid = true;
    }
    xSemaphoreGive(this->state_mutex_);
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetPreviewInput'");
  }

  return valid;
}

bool Atem::GetProgramInput(types::Source* source, uint8_t me) {
  bool valid = false;

  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    if (this->me_ != nullptr && this->top_.me > me) {
      *source = this->me_[me].program;
      valid = true;
    }
    xSemaphoreGive(this->state_mutex_);
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetProgramInput'");
  }

  return valid;
}

bool Atem::GetProtocolVersion(types::ProtocolVersion* version) {
  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    *version = this->ver_;
    xSemaphoreGive(this->state_mutex_);
    return true;
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetProtocolVersion'");
  }

  return true;
}

bool Atem::GetTopology(types::Topology* topology) {
  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    *topology = this->top_;
    xSemaphoreGive(this->state_mutex_);
    return true;
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetTopology'");
  }

  return false;
}

bool Atem::GetTransitionState(types::TransitionState* state, uint8_t me) {
  bool valid = false;

  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    if (this->me_ != nullptr && this->top_.me > me) {
      *state = this->me_[me].trst_;
      valid = true;
    }
    xSemaphoreGive(this->state_mutex_);
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetTransitionState'");
  }

  return valid;
}

bool Atem::GetUskState(types::UskState* state, uint8_t keyer, uint8_t me) {
  bool valid = false;

  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    if (this->usk_ != nullptr && this->top_.me > me && this->top_.usk > keyer) {
      *state = this->usk_[me * this->top_.usk + keyer];
      valid = true;
    }
    xSemaphoreGive(this->state_mutex_);
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetUskState'");
  }

  return valid;
}

bool Atem::GetUskOnAir(bool* state, uint8_t keyer, uint8_t me) {
  bool valid = false;

  if (xSemaphoreTake(this->state_mutex_, 20 / portTICK_PERIOD_MS)) {
    if (this->me_ != nullptr && this->top_.me > me && this->top_.usk > keyer) {
      *state = this->me_[me].usk_on_air & (0x1 << keyer);
      valid = true;
    }
    xSemaphoreGive(this->state_mutex_);
  } else {
    ESP_LOGD(TAG, "Failed to lock mutex for 'GetUskOnAir'");
  }

  return valid;
}

}  // namespace atem

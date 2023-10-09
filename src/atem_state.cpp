#include "atem.h"

namespace atem {

types::Source Atem::GetAuxOutput(uint8_t channel) {
  types::Source source = types::Source::UNDEFINED;

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    if (this->aux_inp_ != nullptr && this->top_.aux > channel)
      source = this->aux_inp_[channel];
    xSemaphoreGive(this->state_mutex_);
  }

  return source;
}

types::DskState Atem::GetDskState(uint8_t keyer) {
  types::DskState state;
  memset(&state, 0, sizeof(state));

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    if (this->dsk_ != nullptr && this->top_.dsk > keyer)
      state = this->dsk_[keyer];
    xSemaphoreGive(this->state_mutex_);
  }

  return state;
}

types::MediaPlayer Atem::GetMediaPlayer() {
  types::MediaPlayer state;
  memset(&state, 0, sizeof(state));

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    state = this->mpl_;
    xSemaphoreGive(this->state_mutex_);
  }

  return state;
}

types::MediaPlayerSource Atem::GetMediaPlayerSource(uint8_t mediaplayer) {
  types::MediaPlayerSource state;
  memset(&state, 0, sizeof(state));

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    if (this->mps_ != nullptr && this->top_.mediaplayers > mediaplayer)
      state = this->mps_[mediaplayer];
    xSemaphoreGive(this->state_mutex_);
  }

  return state;
}

types::Source Atem::GetPreviewInput(uint8_t me) {
  types::Source source = types::Source::UNDEFINED;

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    if (this->me_ != nullptr && this->top_.me > me)
      source = this->me_[me].preview;
    xSemaphoreGive(this->state_mutex_);
  }

  return source;
}

types::Source Atem::GetProgramInput(uint8_t me) {
  types::Source source = types::Source::UNDEFINED;

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    if (this->me_ != nullptr && this->top_.me > me)
      source = this->me_[me].program;
    xSemaphoreGive(this->state_mutex_);
  }

  return source;
}

types::ProtocolVersion Atem::GetProtocolVersion() {
  types::ProtocolVersion version;
  memset(&version, 0, sizeof(version));

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    version = this->ver_;
    xSemaphoreGive(this->state_mutex_);
  }

  return version;
}

types::Topology Atem::GetTopology() {
  types::Topology topology;
  memset(&topology, 0, sizeof(topology));

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    topology = this->top_;
    xSemaphoreGive(this->state_mutex_);
  }

  return topology;
}

types::TransitionState Atem::GetTransitionState(uint8_t me) {
  types::TransitionState state;
  memset(&state, 0, sizeof(state));

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    if (this->me_ != nullptr && this->top_.me > me) state = this->me_[me].trst_;
    xSemaphoreGive(this->state_mutex_);
  }

  return state;
}

types::UskState Atem::GetUskState(uint8_t keyer, uint8_t me) {
  types::UskState state;
  memset(&state, 0, sizeof(state));

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    if (this->usk_ != nullptr && this->top_.me > me && this->top_.usk > keyer)
      state = this->usk_[me * this->top_.usk + keyer];
    xSemaphoreGive(this->state_mutex_);
  }

  return state;
}

bool Atem::GetUskOnAir(uint8_t keyer, uint8_t me) {
  bool state = false;

  if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
    if (this->me_ != nullptr && this->top_.me > me && this->top_.usk > keyer)
      state = this->me_[me].usk_on_air & (0x1 << keyer);
    xSemaphoreGive(this->state_mutex_);
  }

  return state;
}

}  // namespace atem

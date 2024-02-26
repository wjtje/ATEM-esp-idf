#include "atem.h"

#ifdef CONFIG_ATEM_DEBUG_MUTEX_CHECK
static const char* TAG{"AtemState"};
#define ATEM_MUTEX_OWER_CHECK()                                        \
  if (xQueuePeek(this->state_mutex_, NULL, 0) == pdTRUE) {             \
    char* task_name = pcTaskGetName(NULL);                             \
    ESP_LOGE(TAG, "Task '%s' doesn't have the mutex while calling %s", \
             task_name, __ASSERT_FUNC);                                \
    return false;                                                      \
  }
#else
#define ATEM_MUTEX_OWER_CHECK() \
  {}
#endif

namespace atem {

bool Atem::GetAuxOutput(types::Source* source, uint8_t channel) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->aux_out_ != nullptr && this->top_.aux > channel) {
    *source = this->aux_out_[channel];
    return true;
  }
  return false;
}

bool Atem::GetDskState(types::DskState* state, uint8_t keyer) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->dsk_ != nullptr && this->top_.dsk > keyer) {
    *state = this->dsk_[keyer];
    return true;
  }
  return false;
}

bool Atem::GetFtbState(types::FadeToBlack* state, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_ != nullptr && this->top_.me > me) {
    *state = this->me_[me].ftb;
    return true;
  }
  return false;
}

bool Atem::GetMediaPlayer(types::MediaPlayer* state) {
  ATEM_MUTEX_OWER_CHECK();
  *state = this->mpl_;
  return true;
}

bool Atem::GetMediaPlayerSource(types::MediaPlayerSource* state,
                                uint8_t mediaplayer) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mps_ != nullptr && this->top_.mediaplayers > mediaplayer) {
    *state = this->mps_[mediaplayer];
    return true;
  }
  return false;
}

bool Atem::GetPreviewInput(types::Source* source, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_ != nullptr && this->top_.me > me) {
    *source = this->me_[me].preview;
    return true;
  }
  return false;
}

bool Atem::GetProgramInput(types::Source* source, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_ != nullptr && this->top_.me > me) {
    *source = this->me_[me].program;
    return true;
  }
  return false;
}

bool Atem::GetProtocolVersion(types::ProtocolVersion* version) {
  ATEM_MUTEX_OWER_CHECK();
  *version = this->ver_;
  return true;
}

bool Atem::GetTopology(types::Topology* topology) {
  ATEM_MUTEX_OWER_CHECK();
  *topology = this->top_;
  return true;
}

bool Atem::GetTransitionState(types::TransitionState* state, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_ != nullptr && this->top_.me > me) {
    *state = this->me_[me].transition;
    return true;
  }
  return false;
}

bool Atem::GetUskState(types::UskState* state, uint8_t me, uint8_t keyer) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_ != nullptr && this->top_.me > me &&
      this->me_[me].num_keyers > keyer) {
    *state = *(this->me_[me].keyer + keyer);
    return true;
  }
  return false;
}

bool Atem::GetUskNumber(uint8_t* number, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_ != nullptr && this->top_.me > me) {
    *number = this->me_[me].num_keyers;
    return true;
  }
  return false;
}

bool Atem::GetUskOnAir(bool* state, uint8_t me, uint8_t keyer) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_ != nullptr && this->top_.me > me && 16 > keyer) {
    *state = this->me_[me].usk_on_air & (0x1 << keyer);
    return true;
  }
  return false;
}

}  // namespace atem

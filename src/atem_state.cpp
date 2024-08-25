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

bool Atem::GetAuxOutput(Source* source, uint8_t channel) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->aux_out_.size() <= channel) return false;

  *source = this->aux_out_[channel];
  return true;
}

bool Atem::GetDskState(DskState* state, uint8_t keyer) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->dsk_.size() <= keyer) return false;

  *state = this->dsk_[keyer];
  return true;
}

bool Atem::GetFtbState(FadeToBlack* state, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_.size() <= me) return false;

  *state = this->me_[me].ftb;
  return true;
}

bool Atem::GetStreamState(StreamState* state) {
  ATEM_MUTEX_OWER_CHECK();
  *state = this->stream_;
  return true;
}

bool Atem::GetMediaPlayer(MediaPlayer* state) {
  ATEM_MUTEX_OWER_CHECK();
  *state = this->mpl_;
  return true;
}

bool Atem::GetMediaPlayerSource(MediaPlayerSource* state, uint8_t mediaplayer) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mps_.size() <= mediaplayer) return false;

  *state = this->mps_[mediaplayer];
  return true;
}

bool Atem::GetPreviewInput(Source* source, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_.size() <= me) return false;

  *source = this->me_[me].preview;
  return true;
}

bool Atem::GetProgramInput(Source* source, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_.size() <= me) return false;

  *source = this->me_[me].program;
  return true;
}

bool Atem::GetProtocolVersion(ProtocolVersion* version) {
  ATEM_MUTEX_OWER_CHECK();
  *version = this->ver_;
  return true;
}

bool Atem::GetTopology(Topology* topology) {
  ATEM_MUTEX_OWER_CHECK();
  *topology = this->top_;
  return true;
}

bool Atem::GetTransitionState(TransitionState* state, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_.size() <= me) return false;

  *state = this->me_[me].transition;
  return true;
}

bool Atem::GetUskState(UskState* state, uint8_t me, uint8_t keyer) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_.size() <= me) return false;
  if (this->me_[me].keyer.size() <= keyer) return false;

  *state = this->me_[me].keyer[keyer];
  return false;
}

bool Atem::GetUskNumber(uint8_t* number, uint8_t me) {
  ATEM_MUTEX_OWER_CHECK();
  if (this->me_.size() <= me) return false;

  *number = this->me_[me].keyer.size();
  return true;
}

bool Atem::GetUskOnAir(bool* state, uint8_t me, uint8_t keyer) {
  ATEM_MUTEX_OWER_CHECK();

  if (this->me_.size() <= me || keyer > 15) return false;

  *state = this->me_[me].usk_on_air & (0x1 << keyer);
  return true;
}

}  // namespace atem

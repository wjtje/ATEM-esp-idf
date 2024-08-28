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

bool Atem::GetAuxOutput(Source& source, uint8_t channel) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->aux_out_.size() <= channel) return false;
  if (!this->aux_out_[channel].IsValid()) return false;
  source = this->aux_out_[channel].Get();
  return true;
}

bool Atem::GetDskState(DskState& state, uint8_t keyer) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].state.IsValid()) return false;
  state = this->dsk_[keyer].state.Get();
  return true;
}

bool Atem::GetDskSource(DskSource& source, uint8_t keyer) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].source.IsValid()) return false;
  source = this->dsk_[keyer].source.Get();
  return true;
}

bool Atem::GetDskProperties(DskProperties& properties, uint8_t keyer) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].properties.IsValid()) return false;
  properties = this->dsk_[keyer].properties.Get();
  return true;
}

bool Atem::GetFtbState(FadeToBlack& state, uint8_t me) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].ftb.IsValid()) return false;
  state = this->mix_effect_[me].ftb.Get();
  return true;
}

bool Atem::GetStreamState(StreamState& state) const {
  ATEM_MUTEX_OWER_CHECK();
  if (!this->stream_.IsValid()) return false;
  state = this->stream_.Get();
  return true;
}

bool Atem::GetMediaPlayer(MediaPlayer& media_player) const {
  ATEM_MUTEX_OWER_CHECK();
  if (!this->media_player_.IsValid()) return false;
  media_player = this->media_player_.Get();
  return true;
}

bool Atem::GetMediaPlayerSource(MediaPlayerSource& state,
                                uint8_t mediaplayer) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->media_player_source_.size() <= mediaplayer) return false;
  if (!this->media_player_source_[mediaplayer].IsValid()) return false;
  state = this->media_player_source_[mediaplayer].Get();
  return true;
}

bool Atem::GetPreviewInput(Source& source, uint8_t me) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].preview.IsValid()) return false;
  source = this->mix_effect_[me].preview.Get();
  return true;
}

bool Atem::GetProgramInput(Source& source, uint8_t me) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].program.IsValid()) return false;
  source = this->mix_effect_[me].program.Get();
  return true;
}

bool Atem::GetProtocolVersion(ProtocolVersion& version) const {
  ATEM_MUTEX_OWER_CHECK();
  if (!this->version_.IsValid()) return false;
  version = this->version_.Get();
  return true;
}

bool Atem::GetTopology(Topology& topology) const {
  ATEM_MUTEX_OWER_CHECK();
  if (!this->topology_.IsValid()) return false;
  topology = this->topology_.Get();
  return true;
}

bool Atem::GetTransitionState(TransitionState& state, uint8_t me) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].transition.state.IsValid()) return false;
  state = this->mix_effect_[me].transition.state.Get();
  return true;
}

bool Atem::GetTransitionPosition(TransitionPosition& position,
                                 uint8_t me) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].transition.position.IsValid()) return false;
  position = this->mix_effect_[me].transition.position.Get();
  return true;
}

bool Atem::GetUskState(UskState& state, uint8_t me, uint8_t keyer) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (this->mix_effect_[me].keyer.size() <= keyer) return false;
  if (!this->mix_effect_[me].keyer[keyer].state.IsValid()) return false;
  state = this->mix_effect_[me].keyer[keyer].state.Get();
  return true;
}

bool Atem::GetUskNumber(uint8_t& number, uint8_t me) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  number = this->mix_effect_[me].keyer.size();
  return true;
}

bool Atem::GetUskOnAir(bool& state, uint8_t me, uint8_t keyer) const {
  ATEM_MUTEX_OWER_CHECK();

  if (this->mix_effect_.size() <= me || keyer > 15) return false;
  if (!this->mix_effect_[me].usk_on_air.IsValid()) return false;

  state = this->mix_effect_[me].usk_on_air.Get() & (0x1 << keyer);
  return true;
}

bool Atem::GetUskDveState(DveState& state, uint8_t me, uint8_t keyer) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (this->mix_effect_[me].keyer.size() <= keyer) return false;
  if (!this->mix_effect_[me].keyer[keyer].dve.IsValid()) return false;
  state = this->mix_effect_[me].keyer[keyer].dve.Get();
  return true;
}

}  // namespace atem

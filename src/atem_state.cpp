#include "atem.h"

#ifdef CONFIG_ATEM_DEBUG_MUTEX_CHECK
static const char* TAG{"AtemState"};
#define ATEM_MUTEX_OWER_CHECK()                                              \
  if (xQueuePeek(this->state_mutex_, NULL, 0) == pdTRUE) {                   \
    char* task_name = pcTaskGetName(NULL);                                   \
    ESP_LOGE(                                                                \
        TAG, "Task '%s' doesn't have the mutex while calling %s", task_name, \
        __ASSERT_FUNC                                                        \
    );                                                                       \
    return false;                                                            \
  }
#else
#define ATEM_MUTEX_OWER_CHECK() \
  {                             \
  }
#endif

namespace atem {

bool Atem::GetAuxOutput(uint8_t channel, Source& source) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->aux_out_.size() <= channel) return false;
  if (!this->aux_out_[channel].IsValid()) return false;
  source = this->aux_out_[channel].Get();
  return true;
}

bool Atem::GetDskState(uint8_t keyer, DskState& state) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].state.IsValid()) return false;
  state = this->dsk_[keyer].state.Get();
  return true;
}

bool Atem::GetDskSource(uint8_t keyer, DskSource& source) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].source.IsValid()) return false;
  source = this->dsk_[keyer].source.Get();
  return true;
}

bool Atem::GetDskProperties(uint8_t keyer, DskProperties& properties) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].properties.IsValid()) return false;
  properties = this->dsk_[keyer].properties.Get();
  return true;
}

bool Atem::GetFtbState(uint8_t me, FadeToBlack& state) const {
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

bool Atem::GetMediaPlayerSource(
    uint8_t mediaplayer, MediaPlayerSource& state
) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->media_player_source_.size() <= mediaplayer) return false;
  if (!this->media_player_source_[mediaplayer].IsValid()) return false;
  state = this->media_player_source_[mediaplayer].Get();
  return true;
}

bool Atem::GetPreviewInput(uint8_t me, Source& source) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].preview.IsValid()) return false;
  source = this->mix_effect_[me].preview.Get();
  return true;
}

bool Atem::GetProgramInput(uint8_t me, Source& source) const {
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

bool Atem::GetTransitionState(uint8_t me, TransitionState& state) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].transition.state.IsValid()) return false;
  state = this->mix_effect_[me].transition.state.Get();
  return true;
}

bool Atem::GetTransitionPosition(
    uint8_t me, TransitionPosition& position
) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].transition.position.IsValid()) return false;
  position = this->mix_effect_[me].transition.position.Get();
  return true;
}

bool Atem::GetUskState(uint8_t me, uint8_t keyer, UskState& state) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (this->mix_effect_[me].keyer.size() <= keyer) return false;
  if (!this->mix_effect_[me].keyer[keyer].state.IsValid()) return false;
  state = this->mix_effect_[me].keyer[keyer].state.Get();
  return true;
}

bool Atem::GetUskNumber(uint8_t me, uint8_t& count) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  count = this->mix_effect_[me].keyer.size();
  return true;
}

bool Atem::GetUskOnAir(uint8_t me, uint8_t keyer, bool& state) const {
  ATEM_MUTEX_OWER_CHECK();

  if (this->mix_effect_.size() <= me || keyer > 15) return false;
  if (!this->mix_effect_[me].usk_on_air.IsValid()) return false;

  state = this->mix_effect_[me].usk_on_air.Get() & (0x1 << keyer);
  return true;
}

bool Atem::GetUskDveState(uint8_t me, uint8_t keyer, DveState& state) const {
  ATEM_MUTEX_OWER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  if (this->mix_effect_[me].keyer.size() <= keyer) return false;
  if (!this->mix_effect_[me].keyer[keyer].dve.IsValid()) return false;
  state = this->mix_effect_[me].keyer[keyer].dve.Get();
  return true;
}

}  // namespace atem

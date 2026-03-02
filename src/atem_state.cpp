#include "atem.h"

#ifdef CONFIG_ATEM_DEBUG_MUTEX_CHECK
static const char *TAG{"AtemState"};
#define ATEM_MUTEX_OWNER_CHECK()                                           \
  if (xQueuePeek(this->state_mutex_, NULL, 0) == pdTRUE) {                 \
    char *task_name = pcTaskGetName(NULL);                                 \
    ESP_LOGE(                                                              \
      TAG, "Task '%s' doesn't have the mutex while calling %s", task_name, \
      __ASSERT_FUNC                                                        \
    );                                                                     \
    return false;                                                          \
  }
#else
#define ATEM_MUTEX_OWNER_CHECK() \
  {                              \
  }
#endif

namespace atem {

bool Atem::GetAuxOutput(
  uint8_t channel, Source &source, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->aux_out_.size() <= channel) return false;
  if (!this->aux_out_[channel].IsNewer(packet_id)) return false;

  source = this->aux_out_[channel].Get();
  return true;
}

bool Atem::GetDskState(
  uint8_t keyer, DskState &state, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].state.IsNewer(packet_id)) return false;

  state = this->dsk_[keyer].state.Get();
  return true;
}

bool Atem::GetDskSource(
  uint8_t keyer, DskSource &source, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].source.IsNewer(packet_id)) return false;

  source = this->dsk_[keyer].source.Get();
  return true;
}

bool Atem::GetDskProperties(
  uint8_t keyer, DskProperties &properties, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->dsk_.size() <= keyer) return false;
  if (!this->dsk_[keyer].properties.IsNewer(packet_id)) return false;

  properties = this->dsk_[keyer].properties.Get();
  return true;
}

bool Atem::GetFtbState(
  uint8_t me, FadeToBlack &state, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].ftb.IsNewer(packet_id)) return false;

  state = this->mix_effect_[me].ftb.Get();
  return true;
}

bool Atem::GetStreamState(StreamState &state, uint16_t packet_id) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (!this->stream_.IsNewer(packet_id)) return false;

  state = this->stream_.Get();
  return true;
}

bool Atem::GetMediaPlayer(MediaPlayer &media_player, uint16_t packet_id) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (!this->media_player_.IsNewer(packet_id)) return false;

  media_player = this->media_player_.Get();
  return true;
}

bool Atem::GetMediaPlayerSource(
  uint8_t media_player, MediaPlayerSource &state, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->media_player_source_.size() <= media_player) return false;
  if (!this->media_player_source_[media_player].IsNewer(packet_id))
    return false;

  state = this->media_player_source_[media_player].Get();
  return true;
}

bool Atem::GetPreviewInput(
  uint8_t me, Source &source, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].preview.IsNewer(packet_id)) return false;

  source = this->mix_effect_[me].preview.Get();
  return true;
}

bool Atem::GetProgramInput(
  uint8_t me, Source &source, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].program.IsNewer(packet_id)) return false;

  source = this->mix_effect_[me].program.Get();
  return true;
}

bool Atem::GetProtocolVersion(
  ProtocolVersion &version, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (!this->version_.IsNewer(packet_id)) return false;

  version = this->version_.Get();
  return true;
}

bool Atem::GetTopology(Topology &topology, uint16_t packet_id) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (!this->topology_.IsNewer(packet_id)) return false;

  topology = this->topology_.Get();
  return true;
}

bool Atem::GetTransitionState(
  uint8_t me, TransitionState &state, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].transition.state.IsNewer(packet_id)) return false;

  state = this->mix_effect_[me].transition.state.Get();
  return true;
}

bool Atem::GetTransitionPosition(
  uint8_t me, TransitionPosition &position, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->mix_effect_.size() <= me) return false;
  if (!this->mix_effect_[me].transition.position.IsNewer(packet_id))
    return false;

  position = this->mix_effect_[me].transition.position.Get();
  return true;
}

bool Atem::GetUskState(
  uint8_t me, uint8_t keyer, UskState &state, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->mix_effect_.size() <= me) return false;
  if (this->mix_effect_[me].keyer.size() <= keyer) return false;
  if (!this->mix_effect_[me].keyer[keyer].state.IsNewer(packet_id))
    return false;

  state = this->mix_effect_[me].keyer[keyer].state.Get();
  return true;
}

bool Atem::GetUskNumber(uint8_t me, uint8_t &count) const {
  ATEM_MUTEX_OWNER_CHECK();
  if (this->mix_effect_.size() <= me) return false;
  count = this->mix_effect_[me].keyer.size();
  return true;
}

bool Atem::GetUskOnAir(
  uint8_t me, uint8_t keyer, bool &state, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->mix_effect_.size() <= me || keyer > 15) return false;
  if (!this->mix_effect_[me].usk_on_air.IsNewer(packet_id)) return false;

  state = this->mix_effect_[me].usk_on_air.Get() & (0x1 << keyer);
  return true;
}

bool Atem::GetUskDveState(
  uint8_t me, uint8_t keyer, DveState &state, uint16_t packet_id
) const {
  ATEM_MUTEX_OWNER_CHECK();

  if (this->mix_effect_.size() <= me) return false;
  if (this->mix_effect_[me].keyer.size() <= keyer) return false;
  if (!this->mix_effect_[me].keyer[keyer].dve.IsNewer(packet_id)) return false;

  state = this->mix_effect_[me].keyer[keyer].dve.Get();
  return true;
}

}  // namespace atem

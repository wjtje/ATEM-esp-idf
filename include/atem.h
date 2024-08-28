/**
 * @file atem.h
 * @author Wouter (atem_esp_idf@wjt.je)
 * @brief Provides the main class that used to communicate to an ATEM.
 *
 * @copyright Copyright (c) 2023 - Wouter (wjtje)
 */
#pragma once
#include <arpa/inet.h>
#include <esp_event.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "atem_command.h"
#include "atem_packet.h"
#include "atem_state.h"
#include "atem_types.h"
#include "sequence_check.h"

namespace atem {

ESP_EVENT_DECLARE_BASE(ATEM_EVENT);

/**
 * @brief Atem events
 *
 * Events are commands clusted by type, so it's easer to consume (that's my
 * opinion).
 */
enum : int32_t {
  /**
   * @brief AuxS
   */
  ATEM_EVENT_AUX,
  /**
   * @brief DskB / DskP / DskS
   */
  ATEM_EVENT_DSK,
  /**
   * @brief FtbS
   */
  ATEM_EVENT_FADE_TO_BLACK,
  /**
   * @brief InPr
   */
  ATEM_EVENT_INPUT_PROPERTIES,
  /**
   * @brief KeBP / KeDV / KeFS / KeOn
   */
  ATEM_EVENT_USK,
  /**
   * @brief _mpl / MPCE
   */
  ATEM_EVENT_MEDIA_PLAYER,
  /**
   * @brief MPfe
   */
  ATEM_EVENT_MEDIA_POOL,
  /**
   * @brief _pin
   */
  ATEM_EVENT_PRODUCT_ID,
  /**
   * @brief _ver
   */
  ATEM_EVENT_PROTOCOL_VERSION,
  /**
   * @brief PrgI / PrvI
   */
  ATEM_EVENT_SOURCE,
  /**
   * @brief StRS
   */
  ATEM_EVENT_STREAM,
  /**
   * @brief _top
   */
  ATEM_EVENT_TOPOLOGY,
  /**
   * @brief TrPs
   */
  ATEM_EVENT_TRANSITION_POSITION,
  /**
   * @brief TrSS
   */
  ATEM_EVENT_TRANSITION_STATE,
};

class Atem {
 public:
  /**
   * @brief Create a new connection to the ATEM
   *
   * @param address The address of the ATEM to connect to
   */
  Atem(const char* address);
  ~Atem();

  /**
   * @brief Get the State Mutex
   *
   * @warning Make sure you give the mutex back within 20ms or 16ms (e.g. 1
   * frame)
   *
   * @return SemaphoreHandle_t
   */
  SemaphoreHandle_t GetStateMutex() const { return this->state_mutex_; }

  // MARK: Direct state

  /**
   * @brief Get the map of input properties
   *
   * @warning Make sure your task has ownership over the atem state
   *
   * @return const std::map<Source, InputProperty> &
   */
  const std::map<Source, AtemState<InputProperty>>& GetInputProperties() const {
    return this->input_properties_;
  }

  const std::vector<Dsk>& GetDsk() const { return this->dsk_; }
  const std::vector<MixEffect>& GetMixEffect() const {
    return this->mix_effect_;
  }
  const std::vector<AtemState<MediaPlayerSource>>& GetMediaPlayerSources()
      const {
    return this->media_player_source_;
  }

  /**
   * @brief Get all sources that are currently displayed on a aux channel
   *
   * @warning This can be null, length can be determented using GetTopology
   * @warning Make sure your task has ownership over the atem state
   *
   * @return Source*
   */
  const std::vector<AtemState<Source>>& GetAuxOutputs() const {
    return this->aux_out_;
  }

  /**
   * @brief Get the map of the Media Player File Names
   *
   * @warning Make sure your task has ownership over the atem state
   *
   * @return const std::map<uint16_t, char*> {index, file name}
   */
  const std::map<uint16_t, AtemState<char*>>& GetMediaPlayerFileName() const {
    return this->media_player_file_;
  }

  // MARK: Parced state

  /**
   * @brief Returns if the atem connection is active or not
   *
   * @return true The connection is active.
   * @return false The connection isn't active
   */
  bool Connected() const { return this->state_ == ConnectionState::ACTIVE; }
  /**
   * @brief Get the source that's currently displayed of the aux channel. It
   * will return false when it's invalid.
   *
   * @param source[out] A variable that the source will be stored in
   * @param channel[in] Which aux channel to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetAuxOutput(Source& source, uint8_t channel) const;
  /**
   * @brief Get the state of a DSK
   *
   * @param state[out] A variable that the state will be stored in
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetDskState(DskState& state, uint8_t keyer) const;
  /**
   * @brief Get the current fill and key source of a DSK
   *
   * @param source[out] A variable that the state will be stored in
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetDskSource(DskSource& source, uint8_t keyer) const;
  /**
   * @brief Get the properties of a DSK
   *
   * @param properties[out] A variable that the state will be stored in
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetDskProperties(DskProperties& properties, uint8_t keyer) const;
  /**
   * @brief Get the state of the Fade to black on a specific MixEffect.
   *
   * @param state[out] A variable that the state will be stored in
   * @param me[in] Which MixEffect to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetFtbState(FadeToBlack& state, uint8_t me) const;
  /**
   * @brief Get information about the current stream state.
   *
   * @param state[out] A variable that will store the result
   * @return Weather or not the variable is valid
   */
  bool GetStreamState(StreamState& state) const;
  /**
   * @brief Get information about how many stills and clip the media player can
   * hold
   *
   * @param player[out] A variable that will store the result
   *
   * @return Weather or not the variable is valid
   */
  bool GetMediaPlayer(MediaPlayer& state) const;
  /**
   * @brief Get the access to the active source on a specific mediaplayer
   *
   * @param state[out] A variable that will hold the current state
   * @param mediaplayer[in] The media player to get the state from
   *
   * @return Weather or not the variable is valid
   */
  bool GetMediaPlayerSource(MediaPlayerSource& state,
                            uint8_t mediaplayer) const;

  /**
   * @brief Get the current preview source active on ME
   *
   * @param source[out] A variable that the source will be stored in
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetPreviewInput(Source& source, uint8_t me) const;
  /**
   * @brief Get the Product Id (model) of the connected atem.
   *
   * @return const char*
   */
  const char* GetProductId() const { return this->product_id_; }
  /**
   * @brief Get the current program source active on ME
   *
   * @param source[out] A variable that the source will be stored in
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetProgramInput(Source& source, uint8_t me) const;
  /**
   * @brief Get the Protocol Version
   *
   * @param version[out] A variable that will store the protocol version
   *
   * @return Weather or not the variable is valid
   */
  bool GetProtocolVersion(ProtocolVersion& version) const;
  /**
   * @brief Get the topology of the connected ATEM
   *
   * @param topology[out] A variable that will store the topology
   *
   * @return Weather or not the variable is valid
   */
  bool GetTopology(Topology& topology) const;
  /**
   * @brief Get the information about the current transition state on a ME
   *
   * @param state[out] A variable that will store the current state
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetTransitionState(TransitionState& state, uint8_t me) const;
  /**
   * @brief Get the information about the current transition position on a ME
   *
   * @position state[out] A variable that will store the current state
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetTransitionPosition(TransitionPosition& position, uint8_t me) const;
  /**
   * @brief Get the Usk state
   *
   * @param state[out] A variable that will store the current state
   * @param me[in] Which MixEffect to use
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskState(UskState& state, uint8_t me, uint8_t keyer) const;
  /**
   * @brief Get the number of Usk on a given ME
   *
   * @param number[out] A variable that will store the result
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskNumber(uint8_t& number, uint8_t me) const;
  /**
   * @brief Get is a USK is on air
   *
   * @param state[out] A variable that will store the current state
   * @param me[in] Which MixEffect to use
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskOnAir(bool& state, uint8_t me, uint8_t keyer) const;
  /**
   * @brief Get the Usk Dve state
   *
   * @param state[out] A variable that will store the current state
   * @param me[in] Which MixEffect to use
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskDveState(DveState& state, uint8_t me, uint8_t keyer) const;

  /**
   * @brief Send a list of commands to the ATEM, memory is automaticaly
   * deallocated
   *
   * @code
   *  atem_connection->SendCommands({
   *    new atem::cmd::CUT(),
   *  });
   * @endcode
   *
   * @param commands[in]
   *
   * @return If the packet was send (added to queue) successfully
   */
  esp_err_t SendCommands(const std::vector<AtemCommand*>& commands);

 protected:
  int sockfd_;

  // Connection state
  enum class ConnectionState { NOT_CONNECTED, CONNECTED, INITIALIZING, ACTIVE };
  ConnectionState state_{ConnectionState::NOT_CONNECTED};
  uint16_t session_id_;
  uint16_t local_id_{0};
  uint16_t remote_id_{0};

  // Check missing packets
  SequenceCheck sqeuence_;

// Packets send
#if CONFIG_ATEM_STORE_SEND
  SemaphoreHandle_t send_mutex_{xSemaphoreCreateMutex()};
  std::vector<AtemPacket*> send_packets_;
#endif

  // ATEM state
  SemaphoreHandle_t state_mutex_{xSemaphoreCreateMutex()};
  std::map<Source, AtemState<InputProperty>> input_properties_;
  AtemState<Topology> topology_;
  AtemState<ProtocolVersion> version_;
  AtemState<MediaPlayer> media_player_;
  char product_id_[45] = {0};
  std::vector<MixEffect> mix_effect_;
  std::vector<Dsk> dsk_;
  std::vector<AtemState<Source>> aux_out_;
  std::vector<AtemState<MediaPlayerSource>> media_player_source_;
  std::map<uint16_t, AtemState<char*>> media_player_file_;
  AtemState<StreamState> stream_{StreamState::IDLE};

  TaskHandle_t task_handle_{nullptr};
  void task_();

  /**
   * @brief Send an AtemPacket to the atem
   * @warning The packet is not deallocated
   *
   * @param packet
   */
  esp_err_t SendPacket_(AtemPacket* packet);
  /**
   * @brief Close current connection, Reset variables, and send INIT request.
   */
  void Reconnect_();
};

}  // namespace atem

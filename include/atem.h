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

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "atem_command.h"
#include "atem_packet.h"
#include "atem_state.h"
#include "atem_types.h"
#include "sequence_check.h"

namespace atem {

/**
 * @brief Atem events
 *
 * Events are commands clusted by type, so it's easer to consume (that's my
 * opinion).
 */
enum class Event : int32_t {
  /**
   * @brief AuxS
   */
  kAux,
  /**
   * @brief DskB / DskP / DskS
   */
  kDsk,
  /**
   * @brief FtbS
   */
  kFtb,
  /**
   * @brief InPr
   */
  kInpr,
  /**
   * @brief KeBP / KeOn
   */
  kUsk,
  /**
   * @brief KeDV / KeFS
   */
  kUskDve,
  /**
   * @brief _mpl / MPCE
   */
  kMediaPlayer,
  /**
   * @brief MPfe
   */
  kMediaPool,
  /**
   * @brief _pin
   */
  kPid,
  /**
   * @brief _ver
   */
  kVersion,
  /**
   * @brief PrgI / PrvI
   */
  kSource,
  /**
   * @brief StRS
   */
  kStream,
  /**
   * @brief _top
   */
  kTopology,
  /**
   * @brief TrPs
   */
  kTrPos,
  /**
   * @brief TrSS
   */
  kTrState,
};

class Atem {
 public:
  using EventCb = std::function<void(Event, uint16_t)>;

  /**
   * @brief Create a new connection to the ATEM
   *
   * @param [in] address The address of the ATEM to connect to
   * @param [in] event_cb A callback function that will be executed when an
   * internal event is created.
   */
  Atem(const char* address, EventCb event_cb);
  ~Atem();

  const ip4_addr_t* GetAddress() const { return &address_; };

  /**
   * @brief Get the State Mutex
   *
   * @warning Make sure you give the mutex back within 20ms or 16ms (e.g. 1
   * frame)
   *
   * @return SemaphoreHandle_t
   */
  SemaphoreHandle_t GetStateMutex() const { return this->state_mutex_; }

  /**
   * @brief Returns approximately the amount of bytes used by all the internal
   * structures of this object.
   *
   * @return size_t
   */
  size_t GetSize() const;

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
  const std::vector<AtemState<MediaPlayerSource>>& GetMediaPlayerSources(
  ) const {
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
  const auto& GetMediaPlayerFileName() const {
    return this->media_player_file_;
  }

  // MARK: Parced state

  /**
   * @brief Returns if the atem connection is active or not
   *
   * @return true The connection is active.
   * @return false The connection isn't active
   */
  bool Connected() const { return this->state_ == ConnectionState::kActive; }
  /**
   * @brief Get the source that's currently displayed of the aux channel. It
   * will return false when it's invalid.
   *
   * @param [in] channel Which aux channel to use
   * @param [out] source A variable that the source will be stored in
   *
   * @return Weather or not the variable is valid
   */
  bool GetAuxOutput(uint8_t channel, Source& source) const;
  /**
   * @brief Get the state of a DSK
   *
   * @param [in] keyer Which keyer to use
   * @param [out] state A variable that the state will be stored in
   *
   * @return Weather or not the variable is valid
   */
  bool GetDskState(uint8_t keyer, DskState& state) const;
  /**
   * @brief Get the current fill and key source of a DSK
   *
   * @param [in] keyer Which keyer to use
   * @param [out] source A variable that the state will be stored in
   *
   * @return Weather or not the variable is valid
   */
  bool GetDskSource(uint8_t keyer, DskSource& source) const;
  /**
   * @brief Get the properties of a DSK
   *
   * @param [in] keyer Which keyer to use
   * @param [out] properties A variable that the state will be stored in
   *
   * @return Weather or not the variable is valid
   */
  bool GetDskProperties(uint8_t keyer, DskProperties& properties) const;
  /**
   * @brief Get the state of the Fade to black on a specific MixEffect.
   *
   * @param [in] me Which MixEffect to use
   * @param [out] state A variable that the state will be stored in
   *
   * @return Weather or not the variable is valid
   */
  bool GetFtbState(uint8_t me, FadeToBlack& state) const;
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
   * @param [in] mediaplayer The media player to get the state from
   * @param [out] state A variable that will hold the current state
   *
   * @return Weather or not the variable is valid
   */
  bool GetMediaPlayerSource(
      uint8_t mediaplayer, MediaPlayerSource& state
  ) const;

  /**
   * @brief Get the current preview source active on ME
   *
   * @param [out] source A variable that the source will be stored in
   * @param [in] me Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetPreviewInput(uint8_t me, Source& source) const;
  /**
   * @brief Get the Product Id (model) of the connected atem.
   *
   * @return const char*
   */
  const char* GetProductId() const { return this->product_id_; }
  /**
   * @brief Get the current program source active on ME
   *
   *
   * @return Weather or not the variable is valid
   */
  bool GetProgramInput(uint8_t me, Source& source) const;
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
   * @param [out] state A variable that will store the current state
   * @param [in] me Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetTransitionState(uint8_t me, TransitionState& state) const;
  /**
   * @brief Get the information about the current transition position on a ME
   *
   * @param [in] me Which ME to use
   * @param [out] position A variable that will store the current state
   *
   * @return Weather or not the variable is valid
   */
  bool GetTransitionPosition(uint8_t me, TransitionPosition& position) const;
  /**
   * @brief Get the Usk state
   *
   * @param [in] me Which MixEffect to use
   * @param [in] keyer Which keyer to use
   * @param [out] state A variable that will store the current state
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskState(uint8_t me, uint8_t keyer, UskState& state) const;
  /**
   * @brief Get the number of Usk on a given ME
   *
   * @param [in] me Which ME to use
   * @param [out] count A variable that will store the result
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskNumber(uint8_t me, uint8_t& count) const;
  /**
   * @brief Get is a USK is on air
   *
   * @param [in] me Which MixEffect to use
   * @param [in] keyer Which keyer to use
   * @param [out] state A variable that will store the current state
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskOnAir(uint8_t me, uint8_t keyer, bool& state) const;
  /**
   * @brief Get the Usk Dve state
   *
   * @param [in] me Which MixEffect to use
   * @param [in] keyer Which keyer to use
   * @param [out] state A variable that will store the current state
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskDveState(uint8_t me, uint8_t keyer, DveState& state) const;

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
  ip4_addr_t address_;
  int sockfd_;

  // Connection state
  enum class ConnectionState {
    kNotConnected,
    kConnected,
    kInitializing,
    kActive
  };
  ConnectionState state_{ConnectionState::kNotConnected};
  uint16_t session_id_;
  uint16_t local_id_{0};
  uint16_t remote_id_{0};

  EventCb event_cb_;

  // Check missing packets
  SequenceCheck sqeuence_;

#if CONFIG_ATEM_STORE_SEND
  // Packets send
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
  std::vector<AtemState<std::string>> media_player_file_;
  AtemState<StreamState> stream_{StreamState::IDLE};

  TaskHandle_t task_handle_{nullptr};
  void task_();

  /**
   * @brief Send an AtemPacket to the atem
   * @warning The packet is not deallocated
   *
   * @param packet
   */
  esp_err_t SendPacket_(const AtemPacket* packet);
  /**
   * @brief Close current connection, Reset variables, and send INIT request.
   */
  void Reconnect_();
};

}  // namespace atem

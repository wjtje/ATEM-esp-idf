/**
 * @file atem.h
 * @author Wouter van der Wal (me@wjtje.dev)
 * @brief Provides the main class that used to communicate to an ATEM.
 *
 * @copyright Copyright (c) 2023 - Wouter van der Wal
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
#include "atem_types.h"

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
   * @brief Returns if the atem connection is active or not
   *
   * @return true The connection is active.
   * @return false The connection isn't active
   */
  bool Connected() { return this->state_ == ConnectionState::ACTIVE; }
  /**
   * @brief Get the source that's currently displayed of the aux channel. It
   * will return false when it's invalid.
   *
   * @param source[out] A variable that the source will be stored in
   * @param channel[in] Which aux channel to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetAuxOutput(types::Source* source, uint8_t channel);
  /**
   * @brief Get all sources that are currently displayed on a aux channel
   *
   * @warning This can be null, length can be determented using GetTopology
   * @warning Make sure your task has ownership over the atem state
   *
   * @return types::Source*
   */
  types::Source* GetAuxOutputs() { return this->aux_out_; }
  /**
   * @brief Get the state of a DSK
   *
   * @param state[out] A variable that the state will be stored in
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetDskState(types::DskState* state, uint8_t keyer);
  /**
   * @brief Get the state of the Fade to black on a specific MixEffect.
   *
   * @param state[out] A variable that the state will be stored in
   * @param me[in] Which MixEffect to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetFtbState(types::FadeToBlack* state, uint8_t me);
  /**
   * @brief Get the map of input properties
   *
   * @warning Make sure your task has ownership over the atem state
   *
   * @return std::map<types::Source, types::InputProperty>*
   */
  const std::map<types::Source, types::InputProperty> GetInputProperties() {
    return this->input_properties_;
  }
  /**
   * @brief Get the State Mutex
   *
   * @warning Make sure you give the mutex back within 20ms or 16ms (e.g. 1
   * frame)
   *
   * @return SemaphoreHandle_t
   */
  SemaphoreHandle_t GetStateMutex() { return this->state_mutex_; }
  /**
   * @brief Get information about how many stills and clip the media player can
   * hold
   *
   * @param player[out] A variable that will store the result
   *
   * @return Weather or not the variable is valid
   */
  bool GetMediaPlayer(types::MediaPlayer* state);
  /**
   * @brief Get the access to the active source on a specific mediaplayer
   *
   * @param state[out] A variable that will hold the current state
   * @param mediaplayer[in] The media player to get the state from
   *
   * @return Weather or not the variable is valid
   */
  bool GetMediaPlayerSource(types::MediaPlayerSource* state,
                            uint8_t mediaplayer);
  /**
   * @brief Get the map of the Media Player File Names
   *
   * @warning Make sure your task has ownership over the atem state
   *
   * @return std::map<uint16_t, char*> {index, file name}
   */
  std::map<uint16_t, char*> GetMediaPlayerFileName() { return this->mpf_; }
  /**
   * @brief Get the current preview source active on ME
   *
   * @param source[out] A variable that the source will be stored in
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetPreviewInput(types::Source* source, uint8_t me);
  /**
   * @brief Get the Product Id (model) of the connected atem.
   *
   * @return char*
   */
  char* GetProductId() { return this->pid_; }
  /**
   * @brief Get the current program source active on ME
   *
   * @param source[out] A variable that the source will be stored in
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetProgramInput(types::Source* source, uint8_t me);
  /**
   * @brief Get the Protocol Version
   *
   * @param version[out] A variable that will store the protocol version
   *
   * @return Weather or not the variable is valid
   */
  bool GetProtocolVersion(types::ProtocolVersion* version);
  /**
   * @brief Get the topology of the connected ATEM
   *
   * @param topology[out] A variable that will store the topology
   *
   * @return Weather or not the variable is valid
   */
  bool GetTopology(types::Topology* topology);
  /**
   * @brief Get the information about the current transition on a ME
   *
   * @param state[out] A variable that will store the current state
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetTransitionState(types::TransitionState* state, uint8_t me);
  /**
   * @brief Get the Usk Properties object
   *
   * @param state[out] A variable that will store the current state
   * @param me[in] Which MixEffect to use
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskState(types::UskState* state, uint8_t me, uint8_t keyer);
  /**
   * @brief Get the number of Usk on a given ME
   *
   * @param number[out] A variable that will store the result
   * @param me[in] Which ME to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskNumber(uint8_t* number, uint8_t me);
  /**
   * @brief Get the Usk Dve Properties object
   *
   * @param state[out] A variable that will store the current state
   * @param me[in] Which MixEffect to use
   * @param keyer[in] Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskOnAir(bool* state, uint8_t me, uint8_t keyer);

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
  int16_t offset_{1};
  uint32_t received_{0xFFFFFFFE};

// Packets send
#if CONFIG_ATEM_STORE_SEND
  SemaphoreHandle_t send_mutex_{xSemaphoreCreateMutex()};
  std::vector<AtemPacket*> send_packets_;
#endif

  // ATEM state
  SemaphoreHandle_t state_mutex_{xSemaphoreCreateMutex()};
  std::map<types::Source, types::InputProperty> input_properties_;
  types::Topology top_;                     // Topology
  types::ProtocolVersion ver_;              // Protocol version
  types::MediaPlayer mpl_;                  // Media player
  char pid_[45] = {0};                      // Product Id
  types::MixEffectState* me_{nullptr};      // [me]
  types::DskState* dsk_{nullptr};           // [keyer]
  types::Source* aux_out_{nullptr};         // Source in aux [aux]
  types::MediaPlayerSource* mps_{nullptr};  // Media player source [mpl]
  std::map<uint16_t, char*> mpf_;           // Media player file name

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
  /**
   * @brief Checks the order of the packet id's to make sure we are parcing them
   * in order.
   *
   * @param id The id of the received packet
   * @return >0 A packet with this id is missing
   * @return -1 This is fine
   * @return -2 Already parced
   */
  int16_t CheckOrder_(int16_t id);
};

}  // namespace atem

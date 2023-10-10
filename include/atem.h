/**
 * @file atem.h
 * @author Wouter van der Wal (me@wjtje.dev)
 * @brief Provides the main class that used to communicate to an ATEM.
 * @version 0.1
 *
 * @copyright Copyright (c) 2023 - Wouter van der Wal.
 *
 */
#pragma once
#include <arpa/inet.h>
#include <esp_console.h>
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
#include "config_manager.h"

namespace atem {

extern const char* TAG;
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
   * @brief InPr
   */
  ATEM_EVENT_INPUT_PROPERTIES,
  /**
   * @brief KeBP / KeDV / KeOn
   */
  ATEM_EVENT_USK,
  /**
   * @brief _mpl / MPCE
   */
  ATEM_EVENT_MEDIA_PLAYER,
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
   * @brief TrPs / TrSS
   */
  ATEM_EVENT_TRANSITION,
};

class Atem {
 public:
  static Atem* GetInstance();

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
   * @param source A variable that the source will be stored in
   * @param channel Which aux channel to use, default to 0
   *
   * @return Weather or not the variable is valid
   */
  bool GetAuxOutput(types::Source* source, uint8_t channel = 0);
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
   * @param state A variable that the state will be stored in
   * @param keyer Which keyer to use
   *
   * @return Weather or not the variable is valid
   */
  bool GetDskState(types::DskState* state, uint8_t keyer = 0);
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
   * @param player A variable that will store the result
   *
   * @return Weather or not the variable is valid
   */
  bool GetMediaPlayer(types::MediaPlayer* state);
  /**
   * @brief Get the access to the active source on a specific mediaplayer
   *
   * @param state A variable that will hold the current state
   * @param mediaplayer
   *
   * @return Weather or not the variable is valid
   */
  bool GetMediaPlayerSource(types::MediaPlayerSource* state,
                            uint8_t mediaplayer);
  /**
   * @brief Get the current preview source active on ME
   *
   * @param source A variable that the source will be stored in
   * @param me Which ME to use, defaults to 0
   *
   * @return Weather or not the variable is valid
   */
  bool GetPreviewInput(types::Source* source, uint8_t me = 0);
  /**
   * @brief Get the Product Id (model) of the connected atem.
   *
   * @return char*
   */
  char* GetProductId() { return this->pid_; }
  /**
   * @brief Get the current program source active on ME
   *
   * @param source A variable that the source will be stored in
   * @param me Which ME to use, defaults to 0
   *
   * @return Weather or not the variable is valid
   */
  bool GetProgramInput(types::Source* source, uint8_t me = 0);
  /**
   * @brief Get the Protocol Version
   *
   * @param version A variable that will store the protocol version
   *
   * @return Weather or not the variable is valid
   */
  bool GetProtocolVersion(types::ProtocolVersion* version);
  /**
   * @brief Get the topology of the connected ATEM
   *
   * @param topology A variable that will store the topology
   *
   * @return Weather or not the variable is valid
   */
  bool GetTopology(types::Topology* topology);
  /**
   * @brief Get the information about the current transition on a ME
   *
   * @param state A variable that will store the current state
   * @param me Which ME to use, defaults to 0
   *
   * @return Weather or not the variable is valid
   */
  bool GetTransitionState(types::TransitionState* state, uint8_t me = 0);
  /**
   * @brief Get the Usk Properties object
   *
   * @param state A variable that will store the current state
   * @param keyer Which keyer to use, default to 0
   * @param me Which ME to use, default to 0
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskState(types::UskState* state, uint8_t keyer = 0, uint8_t me = 0);
  /**
   * @brief Get the Usk Dve Properties object
   *
   * @param state A variable that will store the current state
   * @param keyer Which keyer to use, default to 0
   * @param me Which ME to use, default to 0
   *
   * @return Weather or not the variable is valid
   */
  bool GetUskOnAir(bool* state, uint8_t keyer = 0, uint8_t me = 0);

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
   * @param commands
   */
  void SendCommands(std::vector<AtemCommand*> commands);

 protected:
  Atem();
  ~Atem();

  // Singleton
  static Atem* instance_;
  static SemaphoreHandle_t mutex_;

  // Config
  class Config : public config_manager::Config {
   public:
    sockaddr* GetAddress() { return (struct sockaddr*)&this->addr_; }

   protected:
    esp_err_t Decode_(cJSON* json);

    sockaddr_in addr_;
  };
  Config config_;

  // Repl command
  static esp_err_t repl_();

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
  SemaphoreHandle_t send_mutex_{xSemaphoreCreateMutex()};
  std::vector<AtemPacket*> send_packets_;

  // ATEM state
  SemaphoreHandle_t state_mutex_{xSemaphoreCreateMutex()};
  std::map<types::Source, types::InputProperty> input_properties_;
  types::Topology top_;                     // Topology
  types::ProtocolVersion ver_;              // Protocol version
  types::MediaPlayer mpl_;                  // Media player
  char pid_[45];                            // Product Id
  types::MixEffectState* me_{nullptr};      // [me]
  types::UskState* usk_{nullptr};           // [me * top_.usk + keyer]
  types::DskState* dsk_{nullptr};           // [keyer]
  types::Source* aux_out_{nullptr};         // Source in aux [aux]
  types::MediaPlayerSource* mps_{nullptr};  // Media player source [mpl]

  TaskHandle_t task_handle_;
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

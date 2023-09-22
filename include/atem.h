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

ESP_EVENT_DECLARE_BASE(ATEM_EVENT);

/**
 * @brief Atem events
 *
 * Events are command clusted by type, so it's easer to consume (that's my
 * opinion)
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
   * @brief Get the source that's currently displayed of the aux channel
   *
   * @param channel Which aux channel to use, default to 0
   * @return Which source is displayed, returns 0xFFFF when it's invalid
   */
  types::Source GetAuxInput(uint8_t channel = 0) {
    if (this->aux_inp_ == nullptr || this->top_.aux - 1 < channel)
      return (types::Source)0xFFFF;
    return this->aux_inp_[channel];
  }
  /**
   * @brief Get all sources that are currently displayed on a aux channel
   *
   * @return types::Source*
   * @warning This can be null, length can be determented using GetTopology
   */
  types::Source* GetAuxInputs() { return this->aux_inp_; }
  /**
   * @brief Get the state of a DSK
   *
   * @warning This function can return nullptr when invalid
   *
   * @param keyer
   * @return types::DskState*
   */
  types::DskState* GetDskState(uint8_t keyer = 0) {
    if (this->dsk_ == nullptr || this->top_.dsk - 1 < keyer) return nullptr;
    return &this->dsk_[keyer];
  }
  /**
   * @brief Get the map of input properties
   *
   * @warning Make sure you got the mutex for this value
   *
   * @return std::map<types::Source, types::InputProperty*>*
   */
  const std::map<types::Source, types::InputProperty*> GetInputProperties() {
    return this->input_properties_;
  }
  SemaphoreHandle_t GetInputPropertiesMutex() {
    return this->input_properties_mutex_;
  }
  /**
   * @brief Get information about how many stills and clip the media player can
   * hold
   *
   * @return types::MediaPlayer
   */
  types::MediaPlayer GetMediaPlayer() { return this->mpl_; }
  /**
   * @brief Get the access to the active source on a specific mediaplayer
   *
   * @param mediaplayer
   * @return types::MediaPlayerSource
   */
  types::MediaPlayerSource GetMediaPlayerSource(uint8_t mediaplayer) {
    if (this->top_.mediaplayers - 1 < mediaplayer || this->mps_ == nullptr)
      return (types::MediaPlayerSource){
          .type = 0, .still_index = 0, .clip_index = 0};
    return this->mps_[mediaplayer];
  }
  /**
   * @brief Get the current preview source active on ME
   *
   * @param me Which ME to use, defaults to 0
   * @return Which source is displayed, returns 0xFFFF when it's invalid
   */
  types::Source GetPreviewInput(uint8_t me = 0) {
    if (this->me_ == nullptr || this->top_.me - 1 < me)
      return (types::Source)0xFFFF;
    return this->me_[me].preview;
  }
  /**
   * @brief Get the Product Id (model) of the connected atem.
   *
   * @warning This is {nullptr} when no atem is connected
   *
   * @return char*
   */
  char* GetProductId() { return this->pid_; }
  /**
   * @brief Get the current program source active on ME
   *
   * @param me Which ME to use, defaults to 0
   * @return Which source is displayed, returns 0xFFFF when it's invalid
   */
  types::Source GetProgramInput(uint8_t me = 0) {
    if (this->me_ == nullptr || this->top_.me - 1 < me)
      return (types::Source)0xFFFF;
    return this->me_[me].program;
  }
  types::ProtocolVersion GetProtocolVersion() { return this->ver_; }
  /**
   * @brief Get the topology of the connected ATEM
   *
   * @return types::Topology
   */
  types::Topology GetTopology() { return this->top_; }
  /**
   * @brief Get the information about the current transition on a ME
   *
   * @param me Which ME to use, defaults to 0
   * @return const types::TransitionState
   */
  const types::TransitionState GetTransitionState(uint8_t me = 0) {
    if (this->me_ == nullptr || this->top_.me - 1 < me)
      return (types::TransitionState){
          .in_transition = false, .position = 0, .style = 0, .next = 0};
    return this->me_[me].trst_;
  }
  /**
   * @brief Get the Usk Properties object
   *
   * @warning This can return nullptr when it's invalid
   *
   * @param keyer Which keyer to use, default to 0
   * @param me Which ME to use, default to 0
   * @return const types::UskProperties*
   */
  const types::UskState* GetUskState(uint8_t keyer = 0, uint8_t me = 0) {
    if (this->usk_ == nullptr || this->top_.me - 1 < me ||
        this->top_.usk - 1 < keyer)
      return nullptr;
    return &this->usk_[me * this->top_.usk + keyer];
  }
  /**
   * @brief Get the Usk Dve Properties object
   *
   * @param keyer Which keyer to use, default to 0
   * @param me Which ME to use, default to 0
   * @return Weather the USK is active or not
   */
  bool GetUskOnAir(uint8_t keyer = 0, uint8_t me = 0) {
    if (this->me_ == nullptr || this->top_.me - 1 < me ||
        this->top_.usk - 1 < keyer)
      return false;
    return this->me_[me].usk_on_air & (0x1 << keyer);
  }

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
  SemaphoreHandle_t input_properties_mutex_{xSemaphoreCreateMutex()};
  std::map<types::Source, types::InputProperty*> input_properties_;
  types::Topology top_;                     // Topology
  types::ProtocolVersion ver_;              // Protocol version
  types::MediaPlayer mpl_;                  // Media player
  char* pid_{nullptr};                      // Product Id
  types::MixEffectState* me_{nullptr};      // [me]
  types::UskState* usk_{nullptr};           // [me * top_.usk + keyer]
  types::DskState* dsk_{nullptr};           // [keyer]
  types::Source* aux_inp_{nullptr};         // Source in aux [aux]
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

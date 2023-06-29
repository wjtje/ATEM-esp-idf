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
// #include <esp_log.h>
// #include <freertos/task.h>
// #include <lwip/netdb.h>
#include <netdb.h>
// #include <lwip/sockets.h>
#include <stdio.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "atem_command.h"
#include "atem_packet.h"
#include "atem_types.h"

#define CONFIG_PACKET_BUFFER_SIZE 1600

// START: define ESP-IDF functions
#define ESP_LOGE(tag, format, ...) \
  printf("(E) %s: " format "\n", tag __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGW(tag, format, ...) \
  printf("(W) %s: " format "\n", tag __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGI(tag, format, ...) \
  printf("(I) %s: " format "\n", tag __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGD(tag, format, ...) \
  printf("(D) %s: " format "\n", tag __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGV(tag, format, ...) \
  printf("(V) %s: " format "\n", tag __VA_OPT__(, ) __VA_ARGS__)

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

// END

namespace atem {

class Atem {
 public:
  Atem(const char* ip);
  ~Atem();

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
   * @return std::map<types::Source, types::InputProperty*>*
   */
  const std::map<types::Source, types::InputProperty*>* GetInputProperties() {
    return &this->input_properties_;
  }
  /**
   * @brief Get the information about a specific source
   *
   * @warning This function can return nullptr when the source isn't found
   *
   * @param source
   * @return const types::InputProperty*
   */
  const types::InputProperty* GetInputProperty(types::Source source) {
    auto it = this->input_properties_.find(source);
    if (it == this->input_properties_.end())
      return nullptr;
    else
      return (*it).second;
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
  int sockfd_;

  // Connection state
  enum class ConnectionState { NOT_CONNECTED, CONNECTED, INITIALIZING, ACTIVE };
  ConnectionState state_{ConnectionState::NOT_CONNECTED};
  uint16_t session_id_;
  uint16_t local_id_{0};
  uint16_t remote_id_{0};

  // Check missing packets
  uint16_t offset_{0};
  uint32_t received_{0xFFFFFFFE};

  // Packets send
  // SemaphoreHandle_t send_mutex_{xSemaphoreCreateMutex()};
  std::map<uint16_t, AtemPacket*> send_packets_;

  // ATEM state
  std::map<types::Source, types::InputProperty*> input_properties_;
  types::Topology top_;                     // Topology
  types::ProtocolVersion ver_;              // Protocol version
  types::MediaPlayer mpl_;                  // Media player
  char* pid_{nullptr};                      // Product Id
  types::MixEffectState* me_{nullptr};      // [me]
  types::UskState* usk_{nullptr};           // [me * top_.usk + keyer]
  types::DskState* dsk_{nullptr};           // [keyer]
  types::Source* aux_inp_{nullptr};         // Source in aux [aux]
  types::MediaPlayerSource* mps_{nullptr};  // Media player source

  // TaskHandle_t task_handle_;
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
   * @param missing Where to store the id of the missing packet
   * @return true The order is correct
   * @return false The order is incorrect, if there is a pkt missing the id is
   * stored in missing
   */
  bool CheckOrder_(uint16_t id, uint16_t* missing);
};

}  // namespace atem

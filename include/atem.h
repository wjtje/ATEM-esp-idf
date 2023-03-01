#pragma once
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include "config_helpers.h"
#include "config_manager.h"
#include "helpers.h"
#include "sdkconfig.h"
#include "wcaf_log.h"

namespace atem {

ESP_EVENT_DECLARE_BASE(ATEM_EVENT);

enum CommandType : uint8_t {
  ACK_REQUEST = 0x01,
  HELLO = 0x02,
  RESEND = 0x04,
  REQUEST = 0x08,
  ACK = 0x10
};

class AtemCommunication {
  DEFINE_SINGLETON(AtemCommunication);

 public:
  // Check weather of not the ATEM is connected
  bool is_connected() { return this->connected_; }

 protected:
  class Config : public config_manager::Config {
   public:
    Config() { this->name_ = "atem"; }
    esp_err_t from_json(cJSON *json);

    CONFIG_DEFINE_NUMBER(port, uint16_t, 9910);
    CONFIG_DEFINE_NUMBER(timeout, uint8_t, 5);

   public:
    uint32_t get_ip() { return this->ip_; }

   protected:
    uint32_t ip_;
  };

  static const char *TAG;
  Config *config_;

  // Socket
  sockaddr_in atem_addr_;
  int sock_{0};

  // Buffers
  SemaphoreHandle_t send_mutex_;
  uint8_t send_buffer_[CONFIG_ATEM_PACKET_BUFFER_SIZE];

  // Command buffer
  uint8_t recv_buffer_[CONFIG_ATEM_RECEIVE_BUFFER_SIZE];
  ssize_t recv_len_;
  uint16_t cmd_index_;
  uint16_t cmd_length_;

  // State
  bool init_send_;
  bool connected_;
  int64_t last_packet_;      // Last time the ATEM has send a packet to us
  uint16_t last_local_id_;   // Last local packet id send to the ATEM
  uint16_t last_remote_id_;  // Last remote packet id received from the ATEM
  uint16_t session_id_;      // A uniqe session id received from the ATEM

  AtemCommunication();

  void thread_();

  /**
   * @brief Clears the buffer and creates the header
   *
   * @param cmd Type of command to send
   * @param length Length of the data to send
   * @param remote_packet_id Remote id
   * @return A uint8_t array of length
   */
  esp_err_t InitMessage_(const CommandType cmd, const uint16_t length,
                         const uint16_t remote_packet_id = 0,
                         bool increment_counter = true);
  /**
   * @brief Send a msg to the ATEM, this function will also delete the buffer,
   * and give back the mutex
   *
   * @param msg
   * @param length
   * @return esp_err_t
   */
  esp_err_t SendMessage_(uint16_t length);

  /**
   * @brief Parse a UDP packet received from the ATEM into individual commands
   *
   * @param packet_length The length of the received packet
   */
  void ParsePacket_(uint16_t packet_length);

  /**
   * @brief Parses an individual command
   *
   * @param cmd a four byte string indicating the command
   * @param index This index the data of the command starts at
   */
  void ParseCommand_(const char *cmd, ssize_t index);
};

}  // namespace atem

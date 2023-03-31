#pragma once
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <lwip/udp.h>

#include <list>

#include "atem_types.h"
#include "config_helpers.h"
#include "config_manager.h"
#include "helpers.h"

#define LOW_BYTE(w) ((uint8_t)((w)&0xff))
#define HIGH_BYTE(w) ((uint8_t)((w) >> 8))

namespace atem {

ESP_EVENT_DECLARE_BASE(ATEM_EVENT);

class ProtocolHeader {
 public:
  enum CommandFlags : uint8_t {
    ACK = 1,
    INIT = 2,
    RETRANSMISSION = 4,
    RESEND = 8,
    RESPONSE = 16
  };

  /**
   * @brief Construct a new Protocol Header object
   *
   * @param flags
   * @param length Length of the message
   * @param session The current sessions, default to a "random" session
   * @param id
   */
  ProtocolHeader(CommandFlags flags, uint16_t length, uint16_t session = 0x0B06,
                 uint16_t id = 0);
  /**
   * @brief Construct a new Protocol Header object from raw data
   *
   * @param data Raw data that's at least 12 bytes long
   */
  ProtocolHeader(const uint8_t* data);
  ~ProtocolHeader();

  /**
   * @brief Get the Command Flags
   *
   * @return CommandFlags
   */
  CommandFlags GetFlags() { return (CommandFlags)(this->data_[0] >> 3); }
  /**
   * @brief Get the length of the packet
   *
   * @return uint16_t
   */
  uint16_t GetLength() {
    return (uint16_t)(((this->data_[0] & 0x07) << 8) | this->data_[1]);
  }
  /**
   * @brief Get the session id of the packet
   *
   * @return uint16_t
   */
  uint16_t GetSession() {
    return (uint16_t)((this->data_[2] << 8) | this->data_[3]);
  }
  /**
   * @brief Get the pointer to a buffer containing the header
   *
   * @return uint8_t*
   */
  const uint8_t* GetData() { return this->data_; }
  /**
   * @brief Get the id of the package that was ACK
   *
   * @return uint16_t
   */
  uint16_t GetAckId() {
    return (uint16_t)((this->data_[4] << 8) | this->data_[5]);
  }
  /**
   * @brief Get the id of the package that was received
   *
   * @return uint16_t
   */
  uint16_t GetId() {
    return (uint16_t)((this->data_[10] << 8) | this->data_[11]);
  }
  /**
   * @brief Get the id of the package that needs to be resend
   *
   * @return uint16_t
   */
  uint16_t GetResendId() {
    return (uint16_t)((this->data_[6] << 8) | this->data_[7]);
  }

  /**
   * @brief Set the id of the packages that you ACK
   *
   * @param id
   */
  void SetAckId(uint16_t id) {
    this->data_[4] = HIGH_BYTE(id);
    this->data_[5] = LOW_BYTE(id);
  }
  /**
   * @brief Set the id of the package that will be send
   *
   * @param id
   */
  void SetId(uint16_t id) {
    this->data_[10] = HIGH_BYTE(id);
    this->data_[11] = LOW_BYTE(id);
  }
  /**
   * @brief Set the id of the package that needs to be resend
   *
   * @param id
   */
  void SetResendId(uint16_t id) {
    this->data_[6] = HIGH_BYTE(id);
    this->data_[7] = LOW_BYTE(id);
  }

 protected:
  uint8_t* data_;
};  // namespace atem

struct AtemPacket {
  uint16_t id;
  int64_t time;
  pbuf* p;
};

class AtemCommunication {
  DEFINE_SINGLETON(AtemCommunication);

 public:
  enum class ConnectionState { NOT_CONNECTED, CONNECTED, INIT, ACTIVE };
  /**
   * @brief Stops the atem protocol
   *
   */
  void Stop();

  InputProperty* GetInputProperty(Source source) {
    for (auto inpr : this->input_properties_)
      if (inpr->source == source) return inpr;
    return nullptr;
  }
  std::list<InputProperty*>* GetInputProperties() {
    return &this->input_properties_;
  }

  void SetPreviewInput(Source videoSource, uint8_t ME = 0);
  void SetAuxInput(Source videoSource, uint8_t channel = 0, bool active = true);

  void Cut(uint8_t ME = 0);
  void Auto(uint8_t ME = 0);

 protected:
  class Config : public config_manager::Config {
   public:
    Config() { this->name_ = "atem"; }
    esp_err_t from_json(cJSON* json);

    CONFIG_DEFINE_NUMBER(port, uint16_t, 9910);
    CONFIG_DEFINE_NUMBER(timeout, uint8_t, 5);

   public:
    ip_addr* get_ip() { return &this->ip_; }

   protected:
    ip_addr ip_;
  };

  Config* config_;

  // Connection information
  ConnectionState state_{ConnectionState::NOT_CONNECTED};
  int64_t last_communication_{0};
  udp_pcb* pcb_;

  uint16_t session_{0x0B06};
  uint16_t local_id_{0x0000};
  uint16_t remote_id_{0x0000};
  uint16_t init_id_{0};
  uint32_t init_pkt_{0};

  bool alloc_{false};

  // Atem state
  Topology top_;
  std::list<InputProperty*> input_properties_;
  Source* prg_inp_{nullptr};
  Source* prv_inp_{nullptr};
  Source* aux_chn_{nullptr};

  AtemCommunication();

  /**
   * @brief Create a packet buffer from a ProtocolHeader
   * @warning The header will be removed
   *
   * @param header
   * @return pbuf*
   */
  pbuf* CreatePacket_(ProtocolHeader* header, bool clean = true);
  /**
   * @brief Send a packet buffer to the ATEM
   * @warning the packet buffer will be removed
   *
   * @param p
   */
  void SendPacket_(pbuf* p);

  void SendCommand_(const char* command, const uint16_t length,
                    const uint8_t* data);
  /**
   * @brief Distroy the connection and reset all variables
   *
   */
  void ResetConnection_();

  /**
   * @brief Callback for receiving a packet from the ATEM
   *
   * @param p Packet buffer
   * @param addr
   * @param port
   */
  void ReceivePacket_(pbuf* p, const ip_addr_t* addr, uint16_t port);

  // Background thread
  TaskHandle_t thread_interval_;
  void ThreadInterval_();

  QueueHandle_t parser_queue_;
  TaskHandle_t thread_parser_;
  void ThreadParser_();
};

}  // namespace atem

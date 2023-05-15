#pragma once
#include <arpa/inet.h>
#include <esp_event.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <lwip/udp.h>

#include <map>
#include <utility>

#include "atem_command.h"
#include "atem_packet.h"
#include "atem_types.h"
#include "config_helpers.h"
#include "config_manager.h"

namespace atem {

ESP_EVENT_DECLARE_BASE(ATEM_EVENT);

class Atem {
 public:
  static Atem* GetInstance();

  Source GetAuxInput(uint8_t channel = 0) {
    if (this->aux_inp_ == nullptr) return (Source)0xFFFF;
    return this->aux_inp_[channel];
  }
  const std::map<atem::Source, InputProperty*>* GetInputProperties() {
    return &this->input_properties_;
  }
  const InputProperty* GetInputProperty(Source source) {
    auto it = this->input_properties_.find(source);
    if (it == this->input_properties_.end())
      return nullptr;
    else
      return (*it).second;
  }
  Source GetPreviewInput(uint8_t me = 0) {
    if (this->prv_inp_ == nullptr) return (Source)0xFFFF;
    return this->prv_inp_[me];
  }
  Source GetProgramInput(uint8_t me = 0) {
    if (this->prg_inp_ == nullptr) return (Source)0xFFFF;
    return this->prg_inp_[me];
  }
  const TransitionPosition GetTransitionPosition(uint8_t me = 0) {
    if (this->trps_ == nullptr)
      return (TransitionPosition){.in_transition = false, .position = 0};
    return this->trps_[me];
  }
  const DveProperties* GetUskDveProperties(uint8_t keyer = 0, uint8_t ME = 0) {
    if (this->dve_ == nullptr || this->top_.me - 1 < ME ||
        this->top_.dve - 1 < keyer)
      return nullptr;
    return &this->dve_[ME * this->top_.dve + keyer];
  }
  bool GetUskOnAir(uint8_t keyer = 0, uint8_t ME = 0) {
    if (this->usk_on_air_ == nullptr) return false;
    return this->usk_on_air_[ME] & (0x1 << keyer);
  }

  void SendCommands(std::initializer_list<AtemCommand*> commands);

 protected:
  Atem();
  ~Atem();

  // Singleton
  static Atem* instance_;
  static SemaphoreHandle_t mutex_;

  // Config

  class Config : public config_manager::Config {
   public:
    Config() { this->name_ = "atem"; }
    esp_err_t FromJson(cJSON* json);

    ip4_addr* GetIp() { return &this->ip_; }
    uint16_t GetPort() { return this->port_; }
    uint8_t GetTimeout() { return this->timeout_; }

   protected:
    ip4_addr ip_;
    uint16_t port_;
    uint8_t timeout_;
  };
  Config* config_;

  udp_pcb* udp_;

  // Connection state
  enum class ConnectionState { NOT_CONNECTED, CONNECTED, INITIALIZING, ACTIVE };
  ConnectionState state_{ConnectionState::NOT_CONNECTED};
  uint16_t session_id_;
  uint16_t local_id_{0};

  // ATEM state
  std::map<atem::Source, InputProperty*> input_properties_;
  Topology top_;
  ProtocolVerion ver_;
  Source* prg_inp_{nullptr};           // Source in program [me]
  Source* prv_inp_{nullptr};           // Source in preview [me]
  Source* aux_inp_{nullptr};           // Source in aux [aux]
  uint8_t* usk_on_air_{nullptr};       // USK on air [me] (bitmask)
  TransitionPosition* trps_{nullptr};  // Trans pos [me]
  DveProperties* dve_{nullptr};        // DVE properteis [me * top_.dve * dve]

  static void recv_(void* arg, udp_pcb* pcb, pbuf* p, const ip_addr_t* addr,
                    uint16_t port);

  TaskHandle_t task_handle_;
  QueueHandle_t task_queue_;
  void task_();

  /**
   * @brief Send an AtemPacket to the atem
   * @warning The packet is not deallocated
   *
   * @param packet
   */
  void SendPacket_(AtemPacket* packet);
  void SendInit_();
};

}  // namespace atem

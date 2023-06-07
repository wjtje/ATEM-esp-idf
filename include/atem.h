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
#include "config_manager.h"

namespace atem {

ESP_EVENT_DECLARE_BASE(ATEM_EVENT);

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
    if (this->aux_inp_ == nullptr) return (types::Source)0xFFFF;
    return this->aux_inp_[channel];
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
   * @brief Get the current preview source active on ME
   *
   * @param me Which ME to use, defaults to 0
   * @return Which source is displayed, returns 0xFFFF when it's invalid
   */
  types::Source GetPreviewInput(uint8_t me = 0) {
    if (this->prv_inp_ == nullptr || this->top_.me - 1 < me)
      return (types::Source)0xFFFF;
    return this->prv_inp_[me];
  }
  /**
   * @brief Get the current program source active on ME
   *
   * @param me Which ME to use, defaults to 0
   * @return Which source is displayed, returns 0xFFFF when it's invalid
   */
  types::Source GetProgramInput(uint8_t me = 0) {
    if (this->prg_inp_ == nullptr || this->top_.me - 1 < me)
      return (types::Source)0xFFFF;
    return this->prg_inp_[me];
  }
  /**
   * @brief Get the information about the current transition on a ME
   *
   * @param me Which ME to use, defaults to 0
   * @return const types::TransitionState
   */
  const types::TransitionState GetTransitionState(uint8_t me = 0) {
    if (this->trst_ == nullptr || this->top_.me - 1 < me)
      return (types::TransitionState){
          .in_transition = false, .position = 0, .style = 0, .next = 0};
    return this->trst_[me];
  }
  /**
   * @brief Get the Usk Dve Properties object
   *
   * @warning This can return nullptr when it's invalid
   *
   * @param keyer Which keyer to use, default to 0
   * @param me Which ME to use, default to 0
   * @return const UskDveProperties*
   */
  const types::UskDveProperties* GetUskDveProperties(uint8_t keyer = 0,
                                                     uint8_t me = 0) {
    if (this->dve_ == nullptr || this->top_.me - 1 < me ||
        this->top_.usk - 1 < keyer)
      return nullptr;
    return &this->dve_[me * this->top_.usk + keyer];
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
  const types::UskProperties* GetUskProperties(uint8_t keyer = 0,
                                               uint8_t me = 0) {
    if (this->dve_ == nullptr || this->top_.me - 1 < me ||
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
    if (this->usk_on_air_ == nullptr || this->top_.me - 1 < me ||
        this->top_.usk - 1 < keyer)
      return false;
    return this->usk_on_air_[me] & (0x1 << keyer);
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
    ip4_addr* GetIp() { return &this->ip_; }
    uint16_t GetPort() { return this->port_; }
    uint8_t GetTimeout() { return this->timeout_; }

   protected:
    esp_err_t Decode_(cJSON* json);

    ip4_addr ip_;
    uint16_t port_;
    uint8_t timeout_;
  };
  Config config_;

  udp_pcb* udp_;

  // Connection state
  enum class ConnectionState { NOT_CONNECTED, CONNECTED, INITIALIZING, ACTIVE };
  ConnectionState state_{ConnectionState::NOT_CONNECTED};
  uint16_t session_id_;
  uint16_t local_id_{0};

  // ATEM state
  std::map<types::Source, types::InputProperty*> input_properties_;
  types::Topology top_;
  types::ProtocolVerion ver_;
  types::Source* prg_inp_{nullptr};        // Source in program [me]
  types::Source* prv_inp_{nullptr};        // Source in preview [me]
  types::Source* aux_inp_{nullptr};        // Source in aux [aux]
  uint8_t* usk_on_air_{nullptr};           // USK on air [me] (bitmask)
  types::TransitionState* trst_{nullptr};  // Transition state [me]
  types::UskDveProperties* dve_{nullptr};  // DVE pr [me * top_.usk * dve]
  types::UskProperties* usk_{nullptr};     // USK pr [me * top_.usk * usk]

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

#include "atem.h"

namespace atem {

CREATE_SINGLETON(AtemCommunication);
ESP_EVENT_DEFINE_BASE(ATEM_EVENT);
const char *AtemCommunication::TAG{"AtemCommunication"};

AtemCommunication::AtemCommunication() {
  this->config_ = new Config();
  config_manager::ConfigManager::GetInstance()->restore(this->config_);

  this->send_mutex_ = xSemaphoreCreateMutex();

  // Define the atem adress and ip
  atem_addr_.sin_family = AF_INET;
  atem_addr_.sin_port = htons(this->config_->get_port());
  atem_addr_.sin_addr.s_addr = this->config_->get_ip();

  CREATE_TASK(AtemCommunication, thread_, "atem_comm", 4096, tskIDLE_PRIORITY,
              NULL);
}

void AtemCommunication::thread_() {
  while (1) {
    // Reset variables
    this->last_local_id_ = 0;
    this->init_send_ = false;
    this->connected_ = false;
    this->session_id_ = 0x5FFF;
    this->last_packet_ = esp_timer_get_time();

    // Create socket
    this->sock_ = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (this->sock_ < 0) {
      WCAF_LOG_ERROR("Failed to create socket (%d)", errno);
      break;
    }

    // Set the socket timeout
    int64_t timeout_micro =
        this->config_->get_timeout() *
        1000000;  // Convert seconds to micro seconds for the esp timer
    timeval timeout = {
        .tv_sec = this->config_->get_timeout(),
        .tv_usec = 0,
    };
    this->recv_len_ = lwip_setsockopt(this->sock_, SOL_SOCKET, SO_RCVTIMEO,
                                      &timeout, sizeof(timeout));

    // Send hello packet
    this->InitMessage_(CommandType::HELLO, 20);
    this->send_buffer_[9] = 0x3A;
    this->send_buffer_[12] = 0x01;
    this->SendMessage_(20);

    // Communication loop
    while (esp_timer_get_time() - this->last_packet_ < timeout_micro) {
      // Receive data
      sockaddr_storage source_addr;
      socklen_t socklen = sizeof(source_addr);
      lwip_recvfrom(this->sock_, this->recv_buffer_, sizeof(this->recv_buffer_),
                    0, (sockaddr *)&source_addr, &socklen);

      // Check ip
      if (((sockaddr_in *)&source_addr)->sin_addr.s_addr !=
          this->config_->get_ip()) {
        WCAF_LOG_WARNING("Received data on socket from other ip");
        continue;
      }

      // Parse data
      CommandType cmd =
          (CommandType)((this->recv_buffer_[0] << 8 | this->recv_buffer_[1]) >>
                        11);
      uint16_t length =
          (this->recv_buffer_[0] << 8 | this->recv_buffer_[1]) & 0x07FF;

      this->session_id_ = this->recv_buffer_[2] << 8 | this->recv_buffer_[3];
      this->last_remote_id_ =
          this->recv_buffer_[10] << 8 | this->recv_buffer_[11];
      this->last_packet_ = esp_timer_get_time();

      // Respond to HELLO
      if (cmd & CommandType::HELLO) {
        this->connected_ = true;

        this->InitMessage_(CommandType::ACK, 12);
        this->send_buffer_[9] = 0x03;
        this->SendMessage_(12);
      }

      // Check init payload
      if (!this->init_send_ && length == 12 && this->last_remote_id_ > 1) {
        this->init_send_ = true;
        WCAF_LOG_DEFAULT("Init payload at %d", this->last_remote_id_);
      }

      if (this->init_send_ && cmd & CommandType::ACK_REQUEST &&
          !(cmd & CommandType::RESEND)) {
        // Respond to ACK
        this->InitMessage_(CommandType::ACK, 12, this->last_remote_id_);
        this->SendMessage_(12);
      } else if (this->init_send_ && cmd & CommandType::REQUEST) {
        // Respond to REQUEST, we cann't actually respond because we don't have
        // a buffer of past messages
        WCAF_LOG_WARNING(
            "Atem requested a resend of %d",
            (uint16_t)((this->recv_buffer_[6] << 8) | this->recv_buffer_[7]));
        this->InitMessage_(CommandType::ACK_REQUEST, 12, 0, false);
        this->send_buffer_[10] = this->recv_buffer_[6];
        this->send_buffer_[11] = this->recv_buffer_[7];
        this->SendMessage_(12);
      }

      // Parse packet
      if (!(cmd & CommandType::HELLO) && length > 12) {
        WCAF_LOG_DEFAULT("Received a packet (%i) with %i bytes",
                         this->last_remote_id_, length);
        this->ParsePacket_(length);
      }
    }

    // Delete socket if communication is closed
    if (this->sock_ != -1) {
      WCAF_LOG_WARNING("Restarting socket");
      xSemaphoreGive(this->send_mutex_);
      lwip_shutdown(this->sock_, 0);
      close(this->sock_);
    }
  }

  // We should never reach this code, but just to be sure
  WCAF_LOG_ERROR("ATEM thead has quit!");
  vTaskDelete(NULL);
}

esp_err_t AtemCommunication::InitMessage_(const CommandType cmd,
                                          const uint16_t length,
                                          const uint16_t remote_packet_id,
                                          bool increment_counter) {
  // Take the mutex
  if (xSemaphoreTake(this->send_mutex_, 50 / portTICK_PERIOD_MS) == pdFALSE) {
    WCAF_LOG_ERROR("Failed to take send mutex");
    return ESP_FAIL;
  }

  // Clear buffer
  memset(this->send_buffer_, 0x0, length);

  // Create header
  this->send_buffer_[0] = (cmd << 3) | (HIGH_BYTE(length) & 0x07);
  this->send_buffer_[1] = LOW_BYTE(length);
  this->send_buffer_[2] = HIGH_BYTE(this->session_id_);
  this->send_buffer_[3] = LOW_BYTE(this->session_id_);
  this->send_buffer_[4] = HIGH_BYTE(remote_packet_id);
  this->send_buffer_[5] = LOW_BYTE(remote_packet_id);

  // Increase local packet id count
  if (!(cmd & (CommandType::HELLO | CommandType::ACK | CommandType::REQUEST)) &&
      increment_counter) {
    this->last_local_id_++;
    this->send_buffer_[10] = HIGH_BYTE(this->last_local_id_);
    this->send_buffer_[11] = LOW_BYTE(this->last_local_id_);
  }

  return ESP_OK;
}

esp_err_t AtemCommunication::SendMessage_(uint16_t length) {
  // Send to ATEM
  int err =
      lwip_sendto(this->sock_, this->send_buffer_, length, 0,
                  (sockaddr *)&this->atem_addr_, sizeof(this->atem_addr_));

  // Give mutex
  xSemaphoreGive(this->send_mutex_);

  // Check for reset
  if (err < 0) {
    WCAF_LOG_ERROR("Failed to send message");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t AtemCommunication::Config::from_json(cJSON *json) {
  this->ip_ = this->json_parse_ipstr_(json, "ip");

  CONFIG_PARSE_NUMBER(json, "port", uint16_t, port);
  CONFIG_PARSE_NUMBER(json, "timeout", uint8_t, timeout);
  return ESP_OK;
}

void AtemCommunication::ParsePacket_(uint16_t packet_length) {
  uint16_t i = 12;  // Packet index

  while (i < packet_length) {
    // Parse command header
    this->cmd_length_ = this->recv_buffer_[i] << 8 | this->recv_buffer_[i + 1];
    char cmd_str[] = {this->recv_buffer_[i + 4], this->recv_buffer_[i + 5],
                      this->recv_buffer_[i + 6], this->recv_buffer_[i + 7],
                      '\0'};

    // WCAF_LOG_DEFAULT("Got command '%s' of %d bytes", cmd_str,
    //                  this->cmd_length_);

    this->ParseCommand_(cmd_str, i + 8);

    i += this->cmd_length_;
  }
}

}  // namespace atem

#include "atem.h"

#include <freertos/task.h>

namespace atem {

CREATE_SINGLETON(AtemCommunication);
ESP_EVENT_DEFINE_BASE(ATEM_EVENT);
const char *TAG{"ATEM"};

ProtocolHeader::ProtocolHeader(CommandFlags flags, uint16_t length,
                               uint16_t session, uint16_t id) {
  if (length > 2047 || length < 12) ESP_LOGW(TAG, "Length to small or large");
  this->data_ = (uint8_t *)malloc(12);
  memset(this->data_ + 4, 0x0, 8);
  this->alloc_ = true;

  // Place the flags and length at the right place
  this->data_[0] = ((uint8_t)flags << 3) | HIGH_BYTE(length);
  this->data_[1] = LOW_BYTE(length);
  // Place the session at the right place
  this->data_[2] = HIGH_BYTE(session);
  this->data_[3] = LOW_BYTE(session);

  this->SetId(id);
}

ProtocolHeader::ProtocolHeader(uint8_t *data) { this->data_ = data; }

ProtocolHeader::~ProtocolHeader() {
  if (this->alloc_) free(this->data_);
}

AtemCommunication::AtemCommunication() {
  this->config_ = new Config();
  config_manager::ConfigManager::GetInstance()->Restore(this->config_);

  pbuf_init();

  // Allocate udp
  this->pcb_ = udp_new();
  if (this->pcb_ == nullptr) ESP_LOGE(TAG, "Failed to alloc new udp");

  // Setup callback
  udp_recv(
      this->pcb_,
      [](void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
         u16_t port) {
        ((AtemCommunication *)arg)->ReceivePacket_(p, addr, port);
      },
      this);

  // Start background thread
  xTaskCreate([](void *arg) { ((AtemCommunication *)arg)->ThreadInterval_(); },
              "atem_int", 4096, this, tskIDLE_PRIORITY + 5,
              &this->thread_interval_);

  this->parser_queue_ = xQueueCreate(20, sizeof(pbuf *));
  if (this->parser_queue_ == NULL) ESP_LOGE(TAG, "Failed to create queue");
  xTaskCreate([](void *arg) { ((AtemCommunication *)arg)->ThreadParser_(); },
              "atem_par", 4096, this, configMAX_PRIORITIES,
              &this->thread_parser_);
}

void AtemCommunication::Stop() {
  // Stop the threads
  vTaskDelete(this->thread_interval_);
  vTaskDelete(this->thread_parser_);
  vQueueDelete(this->parser_queue_);

  // Clear the connection
  this->ResetConnection_();
  // Remove the connection
  udp_remove(this->pcb_);
}

pbuf *AtemCommunication::CreatePacket_(ProtocolHeader *header, bool clean) {
  auto p = pbuf_alloc(PBUF_TRANSPORT, header->GetLength(), PBUF_RAM);
  if (p == nullptr) ESP_LOGE(TAG, "Failed to alloc packet buffer");

  // Initialze data
  ESP_ERROR_CHECK(pbuf_take_at(p, header->GetData(), 12, 0));
  if (clean)
    for (uint16_t i = 12; i < header->GetLength(); i++) pbuf_put_at(p, i, 0x00);

  delete header;
  return p;
}

void AtemCommunication::SendPacket_(pbuf *p) {
  auto err = udp_send(this->pcb_, p);
  if (err != ESP_OK) ESP_LOGE(TAG, "Failed to send packet (%u)", err);
  pbuf_free(p);
}

void AtemCommunication::SendCommand_(const char *command, const uint16_t length,
                                     const uint8_t *data) {
  auto p = this->CreatePacket_(new ProtocolHeader(
      ProtocolHeader::ACK, 20 + length, this->session_, ++this->local_id_));

  uint8_t cmd_header[] = {HIGH_BYTE(8 + length), LOW_BYTE(8 + length), 0, 0};
  pbuf_take_at(p, cmd_header, sizeof(cmd_header), 12);
  pbuf_take_at(p, command, 4, 16);
  pbuf_take_at(p, data, length, 20);

  this->SendPacket_(p);
}

void AtemCommunication::ReceivePacket_(pbuf *p, const ip_addr_t *addr,
                                       uint16_t port) {
  if (p->len < 12) {
    ESP_LOGW(TAG, "Received a package that's to small");
    pbuf_free(p);
  }

  this->last_communication_ = esp_timer_get_time();

  // Get header information
  auto header = new ProtocolHeader((uint8_t *)p->payload);
  this->session_ = header->GetSession();
  if (header->GetId() != 0) this->remote_id_ = header->GetId();

  // Take note of received init packages
  if (this->state_ == ConnectionState::INIT && this->remote_id_ < 32)
    this->init_pkt_ |= 0x01 << this->remote_id_;

  // Received INIT
  if (header->GetFlags() & ProtocolHeader::INIT) {
    auto status = pbuf_get_at(p, 12);

    if (status == 2) {
      ESP_LOGI(TAG, "Received INIT");
      this->state_ = ConnectionState::INIT;

      // Send back RESPONSE for INIT
      auto p = new ProtocolHeader(ProtocolHeader::RESPONSE, 12);
      this->SendPacket_(this->CreatePacket_(p));
    } else if (status == 3)
      ESP_LOGW(TAG, "ATEM is full");
    else if (status == 4)
      // TODO: Figure out what to do here
      ESP_LOGW(TAG, "Received reconnect attempt");
    else
      ESP_LOGE(TAG, "Received unknown INIT");

    goto cleanup;
  }

  // Detect when all INIT packages are send
  if (this->state_ == ConnectionState::INIT &&
      (header->GetFlags() & ProtocolHeader::ACK)) {
    ESP_LOGD(TAG, "INIT complete");
    this->state_ = ConnectionState::ACTIVE;
    this->init_id_ = this->remote_id_;
  }

  // Respond to ACK request
  if (header->GetFlags() & ProtocolHeader::ACK &&
      this->state_ == ConnectionState::ACTIVE) {
    ESP_LOGD(TAG, "-> ACK %u", this->remote_id_);
    auto p = new ProtocolHeader(ProtocolHeader::RESPONSE, 12, this->session_);
    p->SetAckId(this->remote_id_);
    this->SendPacket_(this->CreatePacket_(p));
  }

  // Respond to RESEND request
  if (header->GetFlags() & ProtocolHeader::RESEND &&
      this->state_ == ConnectionState::ACTIVE) {
    ESP_LOGW(TAG, "<- Resend request for %u (local_id: %u)",
             header->GetResendId(), this->local_id_);

    auto p = new ProtocolHeader(ProtocolHeader::ACK, 12, this->session_,
                                header->GetResendId());
    this->SendPacket_(this->CreatePacket_(p));
  }

  // Got ACK for packet
  if (header->GetAckId() != 0)
    ESP_LOGD(TAG, "<- ACK %u (local_id: %u)", header->GetAckId(),
             this->local_id_);

  // Send to parser thread using queue
  if (header->GetLength() > 12) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(this->parser_queue_, (void *)(&p),
                          &xHigherPriorityTaskWoken) != pdTRUE) {
      ESP_LOGE(TAG, "Failed to add package to queue");
      goto cleanup;
    }

    delete header;
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    return;
  }

cleanup:
  delete header;
  pbuf_free(p);
}

void AtemCommunication::ThreadInterval_() {
  for (;;) {
    auto now = esp_timer_get_time();

    // Check for connection
    if (this->state_ == ConnectionState::NOT_CONNECTED) {
      // Display messsage
      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(this->config_->get_ip()->u_addr.ip4.addr), ip_str,
                INET_ADDRSTRLEN);
      ESP_LOGI(TAG, "Connecting to %s", ip_str);

      // Connect
      ESP_ERROR_CHECK(udp_connect(this->pcb_, this->config_->get_ip(),
                                  this->config_->get_port()));

      // Send init
      auto p =
          this->CreatePacket_(new ProtocolHeader(ProtocolHeader::INIT, 20));
      pbuf_put_at(p, 12, 0x01);
      this->SendPacket_(p);

      this->state_ = ConnectionState::CONNECTED;
      this->last_communication_ = esp_timer_get_time();
    }

    // Check for active connection
    if (this->state_ != ConnectionState::NOT_CONNECTED &&
        now - this->last_communication_ > 2000000) {
      ESP_LOGE(TAG,
               "Didn't receive anything for the last 2 seconds, restarting");
      this->ResetConnection_();
    }

    // Ask for missing INIT pkg
    if (this->state_ == ConnectionState::ACTIVE &&
        this->init_pkt_ != 0xFFFFFFFF) {
      for (uint8_t i = 1; i < 32; i++) {
        if (this->init_id_ == i) {
          this->init_pkt_ = 0xFFFFFFFF;
          break;
        }
        if (!(this->init_pkt_ & (0x01 << i))) {
          this->init_pkt_ |= 0x01 << i;
          ESP_LOGW(TAG, "Missing %u", i);
          auto p = new ProtocolHeader(ProtocolHeader::ACK, 12, this->session_,
                                      ++this->local_id_);
          p->SetResendId(i);
          this->SendPacket_(this->CreatePacket_(p));
          break;
        }
      }
    }

    // Send keep alive if connection is active
    if (this->state_ == ConnectionState::ACTIVE &&
        now - this->last_communication_ > 500000) {
      auto p = new ProtocolHeader(
          (ProtocolHeader::CommandFlags)(ProtocolHeader::ACK |
                                         ProtocolHeader::RESPONSE),
          12, this->session_, ++this->local_id_);
      p->SetAckId(this->remote_id_);
      this->SendPacket_(this->CreatePacket_(p));
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void AtemCommunication::ResetConnection_() {
  this->state_ = ConnectionState::NOT_CONNECTED;
  udp_disconnect(this->pcb_);

  this->last_communication_ = 0;
  this->session_ = 0x0B06;
  this->local_id_ = 0;
  this->remote_id_ = 0;
  this->parced_id_ = 0;

  // Free allocated memory
  for (auto it = this->input_properties_.begin();
       it != this->input_properties_.end(); ++it) {
    delete (*it).second;
    this->input_properties_.erase(it);
  }
  if (this->prg_inp_ != nullptr) free(this->prg_inp_);
  if (this->prv_inp_ != nullptr) free(this->prv_inp_);
  if (this->trps_ != nullptr) free(this->trps_);
  if (this->aux_inp_ != nullptr) free(this->aux_inp_);

  // Free pbuf in parser queue
  pbuf *p;
  while (xQueueReceive(this->parser_queue_, &p, 1)) pbuf_free(p);
}

void AtemCommunication::ThreadParser_() {
  pbuf *p;
  for (;;) {
    if (!xQueueReceive(this->parser_queue_, &p, portMAX_DELAY)) continue;
    ESP_LOGI(TAG, "Parser: Received message of %u bytes", p->tot_len);

    // Define variables
    auto header = new ProtocolHeader((uint8_t *)p->payload);
    uint16_t i = 12;
    uint16_t cmd_len = 0;
    char cmd_str[] = {0, 0, 0, 0};

    if (header->GetId() <= this->parced_id_) goto cleanup;

    while (i < p->tot_len) {
      // Get information for command header
      cmd_len = pbuf_get_at(p, i) << 8 | pbuf_get_at(p, i + 1);
      pbuf_copy_partial(p, cmd_str, sizeof(cmd_str), i + 4);

      // Parse commands
      // Product Id
      if (!memcmp(cmd_str, "_pin", 4)) {
        char str[44];
        pbuf_copy_partial(p, str, sizeof(str), i + 8);
        ESP_LOGI(TAG, "Product id: %s", str);
        goto next;
      }

      // Topology
      if (!memcmp(cmd_str, "_top", 4)) {
        // Copy information
        this->top_.num_me = pbuf_get_at(p, i + 8);
        this->top_.num_aux = pbuf_get_at(p, i + 11);

        // Allocate buffers
        this->prg_inp_ = (Source *)calloc(this->top_.num_me, sizeof(Source));
        this->prv_inp_ = (Source *)calloc(this->top_.num_me, sizeof(Source));
        this->trps_ = (TransitionPosition *)calloc(this->top_.num_me,
                                                   sizeof(TransitionPosition));
        this->aux_inp_ = (Source *)calloc(this->top_.num_aux, sizeof(Source));

        ESP_LOGI(TAG, "Topology: (me: %u) (aux: %u)", this->top_.num_me,
                 this->top_.num_aux);
      }

      // Program Input
      if (!memcmp(cmd_str, "PrgI", 4)) {
        uint8_t me = pbuf_get_at(p, i + 8);
        Source source =
            (Source)(pbuf_get_at(p, i + 10) << 8 | pbuf_get_at(p, i + 11));

        ESP_LOGI(TAG, "New program: me: %u source: %u", me, source);

        if (this->prg_inp_ == nullptr || this->top_.num_me - 1 < me) goto next;
        this->prg_inp_[me] = source;

        goto next;
      }

      // Preview Input
      if (!memcmp(cmd_str, "PrvI", 4)) {
        uint8_t me = pbuf_get_at(p, i + 8);
        Source source =
            (Source)(pbuf_get_at(p, i + 10) << 8 | pbuf_get_at(p, i + 11));

        ESP_LOGI(TAG, "New preview: me: %u source: %u", me, source);

        if (this->prv_inp_ == nullptr || this->top_.num_me - 1 < me) goto next;
        this->prv_inp_[me] = source;

        goto next;
      }

      // AUX Channel
      if (!memcmp(cmd_str, "AuxS", 4)) {
        uint8_t channel = pbuf_get_at(p, i + 8);
        Source source =
            (Source)(pbuf_get_at(p, i + 10) << 8 | pbuf_get_at(p, i + 11));

        ESP_LOGI(TAG, "New aux: channel: %u source: %u", channel, source);

        if (this->aux_inp_ == nullptr || this->top_.num_aux - 1 < channel)
          goto next;
        this->aux_inp_[channel] = source;

        goto next;
      }

      // Transition Position
      if (!memcmp(cmd_str, "TrPs", 4)) {
        uint8_t me = pbuf_get_at(p, i + 8);

        if (this->trps_ == nullptr || this->top_.num_me - 1 < me) goto next;
        this->trps_[me].in_transition = (bool)(pbuf_get_at(p, i + 9) & 0x01);
        this->trps_[me].position =
            (uint16_t)(pbuf_get_at(p, i + 12) << 8 | pbuf_get_at(p, i + 13));

        goto next;
      }

      // Input Property
      if (!memcmp(cmd_str, "InPr", 4)) {
        auto source =
            (Source)(pbuf_get_at(p, i + 8) << 8 | pbuf_get_at(p, i + 9));
        auto inpr = (InputProperty *)malloc(sizeof(InputProperty));

        pbuf_copy_partial(p, inpr->name_long, sizeof(inpr->name_long) - 1,
                          i + 10);
        pbuf_copy_partial(p, inpr->name_short, sizeof(inpr->name_short) - 1,
                          i + 30);

        // TODO: Clean garbage at the end of the string

        if (!this->input_properties_
                 .insert(std::pair<Source, InputProperty *>(source, inpr))
                 .second) {
          // Item already exsists, cleaning old one
          auto current = this->input_properties_.at(source);
          free(current);
          current = inpr;
        }
      }

    next:
      i += cmd_len;
    }

    this->parced_id_ = header->GetId();

  cleanup:
    delete header;
    pbuf_free(p);
    esp_event_post(ATEM_EVENT, 0, nullptr, 0, TTW);
  }
}

esp_err_t AtemCommunication::Config::FromJson(cJSON *json) {
  this->ip_.type = IPADDR_TYPE_V4;
  this->ip_.u_addr.ip4.addr = this->JsonParseIpstr_(json, "ip");

  CONFIG_PARSE_NUMBER(json, "port", uint16_t, port);
  CONFIG_PARSE_NUMBER(json, "timeout", uint8_t, timeout);
  return ESP_OK;
}

void AtemCommunication::SetPreviewInput(Source videoSource, uint8_t ME) {
  uint8_t data[] = {ME, 0x00, HIGH_BYTE(videoSource), LOW_BYTE(videoSource)};
  return this->SendCommand_("CPvI", 4, data);
}

void AtemCommunication::SetAuxInput(Source videoSource, uint8_t channel,
                                    bool active) {
  uint8_t data[] = {active, channel, HIGH_BYTE((uint16_t)videoSource),
                    LOW_BYTE((uint16_t)videoSource)};
  return this->SendCommand_("CAuS", 4, data);
}

void AtemCommunication::Cut(uint8_t ME) {
  uint8_t data[] = {ME, 0x00, 0x00, 0x00};
  return this->SendCommand_("DCut", 4, data);
}

void AtemCommunication::Auto(uint8_t ME) {
  uint8_t data[] = {ME, 0x00, 0x00, 0x00};
  return this->SendCommand_("DAut", 4, data);
}

}  // namespace atem

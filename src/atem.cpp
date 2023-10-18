#include "atem.h"

namespace atem {

const char *TAG{"ATEM"};
ESP_EVENT_DEFINE_BASE(ATEM_EVENT);

Atem *Atem::instance_{nullptr};
SemaphoreHandle_t Atem::mutex_ = xSemaphoreCreateMutex();

Atem *Atem::GetInstance() {
  if (xSemaphoreTake(Atem::mutex_, 10 / portTICK_PERIOD_MS)) {
    if (Atem::instance_ == nullptr) Atem::instance_ = new Atem();
    xSemaphoreGive(Atem::mutex_);
    return Atem::instance_;
  }
  return nullptr;
}

esp_err_t Atem::Config::Decode_(cJSON *json) {
  cJSON_GetObjectCheck(json, atem, Object);

  cJSON_GetObjectCheck(atem_obj, ip, String);
  if (!ip4addr_aton(ip_obj->valuestring, (ip4_addr_t *)&this->addr_.sin_addr)) {
    ESP_LOGE(TAG, "Expected key 'ip' to be a valid ip4 adress");
    return ESP_FAIL;
  }

  this->addr_.sin_port = htons(9910);
  this->addr_.sin_family = AF_INET;

  return ESP_OK;
}

Atem::Atem() {
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      config_manager::ConfigManager::Restore(&this->config_));

  // Clear variables
  memset(&this->top_, 0, sizeof(this->top_));
  memset(&this->ver_, 0, sizeof(this->ver_));
  memset(&this->mpl_, 0, sizeof(this->mpl_));

  // Create socket
  int status;
  this->sockfd_ = socket(AF_UNSPEC, SOCK_DGRAM, AF_INET);
  if ((status = connect(this->sockfd_, this->config_.GetAddress(),
                        sizeof *this->config_.GetAddress())) != 0) {
    ESP_LOGE(TAG, "Failed to connect to adress (%s)", strerror(status));
  }

  // Set socket timeout
  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  if ((status = setsockopt(this->sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                           sizeof timeout)) != 0) {
    ESP_LOGE(TAG, "Failed to setsockopt (%s)", strerror(status));
  }

  // Pre allocate send vector
  xSemaphoreTake(this->send_mutex_, portMAX_DELAY);
  this->send_packets_.reserve(34);
  xSemaphoreGive(this->send_mutex_);

  // Create background task
  if (unlikely(!xTaskCreate([](void *a) { ((Atem *)a)->task_(); }, "atem", 5120,
                            this, configMAX_PRIORITIES, &this->task_handle_))) {
    ESP_LOGE(TAG, "Failed to create task");
    abort();
  }

  this->Reconnect_();

  // Register repl
  ESP_ERROR_CHECK_WITHOUT_ABORT(this->repl_());
}

Atem::~Atem() {
  vTaskDelete(this->task_handle_);

  // Clear cached packages
  xSemaphoreTake(this->send_mutex_, portMAX_DELAY);
  for (auto p : this->send_packets_) delete p;
  this->send_packets_.clear();
  xSemaphoreGive(this->send_mutex_);

  // Clear memory
  xSemaphoreTake(this->state_mutex_, portMAX_DELAY);
  this->input_properties_.clear();
  delete this->me_;
  delete this->usk_;
  delete this->dsk_;
  delete this->aux_out_;
  delete this->mps_;
  for (std::pair<uint16_t, char *> file : this->mpf_) free(file.second);
  this->mpf_.clear();
  xSemaphoreGive(this->state_mutex_);
}

void Atem::task_() {
  char buffer[CONFIG_PACKET_BUFFER_SIZE];
  AtemPacket packet(buffer);
  int ack_count = 0, len;
  uint32_t boot_events = 0;

  for (;;) {
    // Get length of next package
    len = recv(this->sockfd_, packet.GetData(), 2, MSG_PEEK);
    if (packet.GetLength() > sizeof buffer)
      ESP_LOGE(TAG, "Next package is buffer than buffer");

    // Something went wrong
    if (len < 0) {
      if (errno != EAGAIN) {
        ESP_LOGE(TAG, "recv error: %s (%i)", strerror(errno), errno);
        continue;
      }

      if (ack_count > 4) {  // Already send multiple ACK requests
        this->Reconnect_();
        continue;
      }

      // Send ACK-RESPONSE to test connection
      if (this->Connected()) {
        AtemPacket p = AtemPacket(0x11, this->session_id_, 12);
        p.SetId(++this->local_id_);
        p.SetAckId(this->remote_id_);
        this->SendPacket_(&p);
      }

      ack_count++;
      continue;
    }

    int recv_len = packet.GetLength();
    if (recv_len > sizeof buffer) {
      ESP_LOGE(TAG, "Next package is buffer than buffer");
      recv_len = sizeof buffer;
    }

    // Get next packet
    if ((len = recv(this->sockfd_, packet.GetData(), recv_len, 0)) < 0) {
      ESP_LOGE(TAG, "recv error: %s (%i)", strerror(errno), errno);
      continue;
    }

    ack_count = 0;

    // Check Length
    if (packet.GetLength() != len) {
      ESP_LOGW(TAG, "Received packet with invalid size (%u instead of %u)", len,
               packet.GetLength());
      continue;
    }

    ESP_LOGD(TAG, "<- Flags: %02X, ACK: %04X, Resend: %04X, Id: %04X, Len: %u",
             packet.GetFlags(), packet.GetAckId(), packet.GetResendId(),
             packet.GetId(), packet.GetLength());

    // Check session id
    if (this->state_ == ConnectionState::ACTIVE &&
        packet.GetSessionId() != this->session_id_) {
      ESP_LOGW(TAG,
               "Received packet with invalid session (%02x instead of %02x)",
               packet.GetSessionId(), this->session_id_);
      continue;
    }

    // INIT packet
    if (packet.GetFlags() & 0x2 && this->state_ != ConnectionState::ACTIVE) {
      ESP_LOGD(TAG, "Received INIT");
      uint8_t init_status = ((const uint8_t *)packet.GetData())[12];

      if (init_status == 0x2) {  // INIT accepted
        this->local_id_ = 0;
        this->remote_id_ = 0;
        this->state_ = ConnectionState::INITIALIZING;
        AtemPacket p = AtemPacket(0x10, packet.GetSessionId(), 12);
        this->SendPacket_(&p);
      } else if (init_status == 0x3) {  // No connection available
        ESP_LOGW(TAG,
                 "Couldn't connect to the atem because it has no connection "
                 "slot available");
      } else {
        ESP_LOGW(TAG, "Received an unknown INIT status (%02x)", init_status);
      }
    }

    // INIT packets complete
    if (this->state_ == ConnectionState::INITIALIZING &&
        packet.GetFlags() & 0x1 && packet.GetLength() == 12) {
      // Show some basic information about the ATEM
      if (xSemaphoreTake(this->state_mutex_, 100 / portTICK_PERIOD_MS)) {
        ESP_LOGI(TAG,
                 "Initialization done\n\t\tModel: %s\n\t\tVersion: %u.%u"
                 "\n\t\tTopology: ME(%u), sources(%u), dsk(%u), usk(%u), "
                 "aux(%u)",
                 this->pid_, this->ver_.major, this->ver_.minor, this->top_.me,
                 this->top_.sources, this->top_.dsk, this->top_.usk,
                 this->top_.aux);

        xSemaphoreGive(this->state_mutex_);
      }

      this->session_id_ = packet.GetSessionId();
      this->state_ = ConnectionState::ACTIVE;

      // Send event's
      for (int32_t i = 0; i < sizeof(boot_events) * 8; i++)
        if (boot_events & 1 << i) esp_event_post(ATEM_EVENT, i, nullptr, 0, 0);
    }

    // RESEND request
    if (packet.GetFlags() & 0x8 && this->state_ == ConnectionState::ACTIVE) {
      ESP_LOGW(TAG, "<- Resend request for %u", packet.GetResendId());
      bool send = false;

      // Try to find the packet
      if (xSemaphoreTake(this->send_mutex_, 50 / portTICK_PERIOD_MS)) {
        int16_t id = packet.GetId();
        for (int i = 0; AtemPacket * p : this->send_packets_) {
          if (i++ > 50) break;  // Limit to max 50 loops

          if (p->GetId() == id) {
            this->SendPacket_(p);
            send = true;
            break;
          }
        }

        xSemaphoreGive(this->send_mutex_);
      }

      // We don't have this packet, just pretend it was an ACK
      if (!send) {
        AtemPacket p = new AtemPacket(0x1, packet.GetSessionId(), 12);
        p.SetId(packet.GetResendId());
        this->SendPacket_(&p);
      }
    }

    // Send ACK
    if (packet.GetFlags() & 0x1) {
      this->remote_id_ = packet.GetId();

      // Respond to ACK
      if (this->state_ == ConnectionState::ACTIVE) {
        AtemPacket p = AtemPacket(0x10, packet.GetSessionId(), 12);
        p.SetAckId(this->remote_id_);
        this->SendPacket_(&p);
      }

      // Check if we are receiving it in order
      int16_t missing_id = this->CheckOrder_(packet.GetId());
      if (missing_id == -2) {
        ESP_LOGW(TAG, "Received packet %u, but is was already parced",
                 packet.GetId());
        continue;  // We can ignore this packet
      } else if (missing_id >= 0) {
        ESP_LOGE(TAG, "Missing packet %u", missing_id);

        // Request missing
        AtemPacket p = AtemPacket(0x18, this->session_id_, 12);
        p.SetResendId(missing_id);
        p.SetAckId(missing_id - 1);  // We received the previous packet
        this->SendPacket_(&p);
      }
    }

    // Receive ACK
    if (packet.GetFlags() & 0x10 && this->state_ == ConnectionState::ACTIVE) {
      if (xSemaphoreTake(this->send_mutex_, 50 / portTICK_PERIOD_MS)) {
        int16_t id = packet.GetAckId();
        int i = 0;

        for (std::vector<AtemPacket *>::iterator it =
                 this->send_packets_.begin();
             it != this->send_packets_.end();) {
          if (i++ > 50) break;  // Limit to max 50 loops

          // Remove all packets older than 32
          if ((((*it)->GetId() - id) & 0x7FFF) > 32 &&
              ((id - (*it)->GetId()) & 0x7FFF) > 32) {
            ESP_LOGD(TAG, "Removing packet with id %i because it's to old",
                     (*it)->GetId());
            delete (*it);
            it = this->send_packets_.erase(it);
          } else if ((*it)->GetId() == id) {
            delete (*it);
            it = this->send_packets_.erase(it);
            break;
          } else {
            ++it;
          }
        }

        xSemaphoreGive(this->send_mutex_);
      } else {
        ESP_LOGW(TAG, "Failed to note of ACK");
      }
    }

    // Check size of packet
    if (len <= 12 || packet.GetFlags() & 0x2) continue;

    // Parse packet
    uint32_t event = 0;

    // Initialize common variables
    uint8_t me, keyer, channel, state, mediaplayer;
    uint16_t pos;
    size_t len;
    types::Source source;
    types::UskState *usk_state;
    types::UskDveProperties *dve_properties;

    // Lock access to the state
    if (!xSemaphoreTake(this->state_mutex_, 150 / portTICK_PERIOD_MS)) {
      ESP_LOGW(TAG,
               "Failed to lock access to the state, make sure you only lock "
               "the state for max 100ms.");
      continue;
    }

    for (int i = 0; AtemCommand command : packet) {
      if (++i > 512) break;  // Limit 512 command in a single packet

      switch (ATEM_CMD(((char *)command.GetCmd()))) {
        case ATEM_CMD("_mpl"): {  // Media Player
          event |= 1 << ATEM_EVENT_MEDIA_PLAYER;
          this->mpl_.still = command.GetData<uint8_t *>()[0];
          this->mpl_.clip = command.GetData<uint8_t *>()[1];
          break;
        }
        case ATEM_CMD("_ver"): {  // Protocol version
          event |= 1 << ATEM_EVENT_PROTOCOL_VERSION;
          this->ver_ = {.major = ntohs(command.GetData<uint16_t *>()[0]),
                        .minor = ntohs(command.GetData<uint16_t *>()[1])};
          break;
        }
        case ATEM_CMD("_pin"): {  // Product Id
          event |= 1 << ATEM_EVENT_PRODUCT_ID;
          memcpy(this->pid_, command.GetData<char *>(), sizeof(this->pid_));

          len = strlen(command.GetData<char *>());
          if (len > 44) len = 44;
          memset(this->pid_ + len, 0, sizeof(this->pid_) - len);
          break;
        }
        case ATEM_CMD("_top"): {  // Topology
          event |= 1 << ATEM_EVENT_TOPOLOGY;
          memcpy(&this->top_, command.GetData<void *>(), sizeof(this->top_));

          // Clear memory
          delete this->me_;
          delete this->usk_;
          delete this->dsk_;
          delete this->aux_out_;
          delete this->mps_;

          // Allocate buffers
          this->me_ = new types::MixEffectState[this->top_.me];
          this->usk_ = new types::UskState[this->top_.me * this->top_.usk];
          this->dsk_ = new types::DskState[this->top_.dsk];
          this->aux_out_ = new types::Source[this->top_.aux];
          this->mps_ = new types::MediaPlayerSource[this->top_.mediaplayers];
          break;
        }
        case ATEM_CMD("AuxS"): {  // AUX Select
          event |= 1 << ATEM_EVENT_AUX;
          channel = command.GetData<uint8_t *>()[0];
          if (this->aux_out_ == nullptr || this->top_.aux - 1 < channel) break;

          this->aux_out_[channel] =
              (types::Source)ntohs(command.GetData<uint16_t *>()[1]);
          break;
        }
        case ATEM_CMD("DskB"): {  // DSK Source
          event |= 1 << ATEM_EVENT_DSK;
          keyer = command.GetData<uint8_t *>()[0];
          if (this->dsk_ == nullptr || this->top_.dsk - 1 < keyer) break;

          this->dsk_[keyer].fill =
              (types::Source)ntohs(command.GetData<uint16_t *>()[1]);
          this->dsk_[keyer].key =
              (types::Source)ntohs(command.GetData<uint16_t *>()[2]);
          break;
        }
        case ATEM_CMD("DskP"): {  // DSK Properties
          event |= 1 << ATEM_EVENT_DSK;
          keyer = command.GetData<uint8_t *>()[0];
          if (this->dsk_ == nullptr || this->top_.dsk - 1 < keyer) break;

          this->dsk_[keyer].tie = command.GetData<uint8_t *>()[1];
          break;
        }
        case ATEM_CMD("DskS"): {  // DSK State
          event |= 1 << ATEM_EVENT_DSK;
          keyer = command.GetData<uint8_t *>()[0];
          if (this->dsk_ == nullptr || this->top_.dsk - 1 < keyer) break;

          this->dsk_[keyer].on_air = command.GetData<uint8_t *>()[1];
          this->dsk_[keyer].in_transition = command.GetData<uint8_t *>()[2];
          this->dsk_[keyer].is_auto_transitioning =
              command.GetData<uint8_t *>()[3];
          break;
        }
        case ATEM_CMD("InPr"): {  // Input Property
          event |= 1 << ATEM_EVENT_INPUT_PROPERTIES;
          source = command.GetDataS<types::Source>(0);

          types::InputProperty inpr;
          memset(&inpr, 0, sizeof(inpr));

          // Copy name long
          len = strnlen(command.GetData<char *>() + 2, sizeof(inpr.name_long));
          memcpy(inpr.name_long, command.GetData<uint8_t *>() + 2, len);

          // Copy name short
          len =
              strnlen(command.GetData<char *>() + 22, sizeof(inpr.name_short));
          memcpy(inpr.name_short, command.GetData<uint8_t *>() + 22, len);

          // Store inpr
          this->input_properties_[source] = inpr;

          break;
        }
        case ATEM_CMD("KeBP"): {  // Key properties
          event |= 1 << ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->usk_ == nullptr || this->top_.me - 1 < me ||
              this->top_.usk - 1 < keyer)
            break;

          usk_state = &this->usk_[me * this->top_.usk + keyer];
          usk_state->type = command.GetData<uint8_t *>()[2];
          usk_state->fill =
              (types::Source)ntohs(command.GetData<uint16_t *>()[3]);
          usk_state->key =
              (types::Source)ntohs(command.GetData<uint16_t *>()[4]);
          usk_state->top = ntohs(command.GetData<uint16_t *>()[6]);
          usk_state->bottom = ntohs(command.GetData<uint16_t *>()[7]);
          usk_state->left = ntohs(command.GetData<uint16_t *>()[8]);
          usk_state->right = ntohs(command.GetData<uint16_t *>()[9]);
          break;
        }
        case ATEM_CMD("KeDV"): {  // Key properties DVE
          event |= 1 << ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->usk_ == nullptr || this->top_.me - 1 < me ||
              this->top_.usk - 1 < keyer)
            break;

          dve_properties = &this->usk_[me * this->top_.usk + keyer].dve_;
          dve_properties->size_x = ntohl(command.GetData<uint32_t *>()[1]);
          dve_properties->size_y = ntohl(command.GetData<uint32_t *>()[2]);
          dve_properties->pos_x = ntohl(command.GetData<uint32_t *>()[3]);
          dve_properties->pos_y = ntohl(command.GetData<uint32_t *>()[4]);
          dve_properties->rotation = ntohl(command.GetData<uint32_t *>()[5]);
          break;
        }
        case ATEM_CMD("KeFS"): {  // Key Fly State
          event |= 1 < ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->usk_ == nullptr || this->top_.me - 1 < me ||
              this->top_.usk - 1 < keyer)
            break;

          this->usk_[me * this->top_.usk + keyer].at_key_frame =
              command.GetData<uint8_t *>()[6];
          break;
        }
        case ATEM_CMD("KeOn"): {  // Key on Air
          event |= 1 << ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];
          state = command.GetData<uint8_t *>()[2];

          if (this->me_ == nullptr || this->top_.me - 1 < me || keyer > 15)
            break;

          this->me_[me].usk_on_air &= ~(0x1 << keyer);
          this->me_[me].usk_on_air |= (state << keyer);
          break;
        }
        case ATEM_CMD("MPCE"): {  // Media Player Source
          event |= 1 << ATEM_EVENT_MEDIA_PLAYER;
          mediaplayer = command.GetData<uint8_t *>()[0];
          if (this->top_.mediaplayers - 1 < mediaplayer) break;

          this->mps_[mediaplayer] = {
              .type = command.GetData<uint8_t *>()[1],
              .still_index = command.GetData<uint8_t *>()[2],
              .clip_index = command.GetData<uint8_t *>()[3],
          };
          break;
        }
        case ATEM_CMD("MPfe"): {  // Media Pool Frame Description
          uint8_t type = command.GetData<uint8_t *>()[0];
          uint16_t index = command.GetDataS<uint16_t>(1);
          bool is_used = command.GetData<uint8_t *>()[4];

          if (type != 0) break;  // Only work with stills
          event |= 1 << ATEM_EVENT_MEDIA_POOL;

          // Clear index
          std::map<uint16_t, char *>::iterator it = this->mpf_.find(index);
          if (it != this->mpf_.end()) {
            free((*it).second);
            this->mpf_.erase(it);
          }

          // Store file
          if (is_used) {
            // Create a copy of the filename
            uint8_t filename_len = command.GetData<uint8_t *>()[23];
            char *filename =
                strndup(command.GetData<char *>() + 24, filename_len);
            if (filename != nullptr) mpf_.insert({index, filename});
          }
          break;
        }
        case ATEM_CMD("PrgI"): {  // Program Input
          event |= 1 << ATEM_EVENT_SOURCE;
          me = command.GetData<uint8_t *>()[0];
          if (this->me_ == nullptr || this->top_.me - 1 < me) break;
          this->me_[me].program = command.GetDataS<types::Source>(1);
          break;
        }
        case ATEM_CMD("PrvI"): {  // Preview Input
          event |= 1 << ATEM_EVENT_SOURCE;
          me = command.GetData<uint8_t *>()[0];
          if (this->me_ == nullptr || this->top_.me - 1 < me) break;
          this->me_[me].preview = command.GetDataS<types::Source>(1);
          break;
        }
        case ATEM_CMD("TrPs"): {  // Transition Position
          event |= 1 << ATEM_EVENT_TRANSITION;
          me = command.GetData<uint8_t *>()[0];
          state = command.GetData<uint8_t *>()[1];
          pos = ntohs(command.GetData<uint16_t *>()[2]);

          if (this->me_ == nullptr || this->top_.me - 1 < me) break;
          this->me_[me].trst_.in_transition = (bool)(state & 0x01);
          this->me_[me].trst_.position = pos;
          break;
        }
        case ATEM_CMD("TrSS"): {  // Transition State
          event |= 1 << ATEM_EVENT_TRANSITION;
          me = command.GetData<uint8_t *>()[0];

          if (this->me_ == nullptr || this->top_.me - 1 < me) break;
          this->me_[me].trst_.style = command.GetData<uint8_t *>()[1];
          this->me_[me].trst_.next = command.GetData<uint8_t *>()[2];
          break;
        }
      }
    }

    xSemaphoreGive(this->state_mutex_);  // unlock the access

    // Send events
    if (event != 0 && this->state_ == ConnectionState::ACTIVE) {
      for (int32_t i = 0; i < sizeof(event) * 8; i++)
        if (event & 1 << i) esp_event_post(ATEM_EVENT, i, nullptr, 0, 0);
    } else {
      boot_events |= event;
    }
  }

  vTaskDelete(nullptr);
}

esp_err_t Atem::SendPacket_(AtemPacket *packet) {
  ESP_LOG_BUFFER_HEXDUMP(TAG, packet->GetData(), packet->GetLength(),
                         ESP_LOG_VERBOSE);

  ESP_LOGD(TAG, "-> Flags: %02X, ACK: %04X, Resend: %04X, Id: %04X, Len: %u",
           packet->GetFlags(), packet->GetAckId(), packet->GetResendId(),
           packet->GetId(), packet->GetLength());

  int len = send(this->sockfd_, packet->GetData(), packet->GetLength(), 0);
  if (len != packet->GetLength()) {
    ESP_LOGW(TAG, "Failed to send packet: %u", packet->GetId());
    return ESP_FAIL;
  }
  return ESP_OK;
}

void Atem::Reconnect_() {
  ESP_LOGI(TAG, "Reconnecting to ATEM");

  // Reset local variables
  this->state_ = ConnectionState::CONNECTED;
  this->local_id_ = 0;
  this->remote_id_ = 0;
  this->offset_ = 1;
  this->received_ = 0xFFFFFFFE;
  this->session_id_ = 0x0B06;

  // Remove all packets
  xSemaphoreTake(this->send_mutex_, portMAX_DELAY);
  for (auto p : this->send_packets_) delete p;
  this->send_packets_.clear();
  xSemaphoreGive(this->send_mutex_);

  // Send init request
  AtemPacket p = AtemPacket(0x2, this->session_id_, 20);
  ((uint8_t *)p.GetData())[12] = 0x01;
  this->SendPacket_(&p);
}

void Atem::SendCommands(std::vector<AtemCommand *> commands) {
  types::ProtocolVersion protocol_version;

  xSemaphoreTake(this->state_mutex_, portMAX_DELAY);
  protocol_version = this->ver_;
  xSemaphoreGive(this->state_mutex_);

  // Get the length of the commands
  uint16_t length = 12;  // Packet header
  uint16_t amount = 0;
  for (auto c : commands) {
    if (unlikely(c == nullptr)) continue;
    length += c->GetLength();
    amount++;
  }

  if (length == 12) return;  // Don't send empty commands
  ESP_LOGD(TAG, "Sending %u commands", amount);

  // Create the packet
  AtemPacket *packet = new AtemPacket(0x1, this->session_id_, length);
  packet->SetId(++this->local_id_);

  // Copy commands into packet
  uint16_t i = 12;
  for (auto c : commands) {
    if (unlikely(c == nullptr)) continue;
    c->PrepairCommand(protocol_version);
    memcpy((uint8_t *)packet->GetData() + i, c->GetRawData(), c->GetLength());
    i += c->GetLength();
    delete c;
  }

  // Send the packet
  this->SendPacket_(packet);

  // Store packet in send_packets_
  if (xSemaphoreTake(this->send_mutex_, 50 / portTICK_PERIOD_MS)) {
    if (this->send_packets_.size() < 33) {
      this->send_packets_.push_back(packet);
    } else {
      ESP_LOGW(TAG, "Failed to store packet");
      delete packet;
    }
    xSemaphoreGive(this->send_mutex_);
  } else {
    ESP_LOGW(TAG, "Failed to store packet");
    delete packet;
  }
}

int16_t Atem::CheckOrder_(int16_t id) {
  static uint16_t recv_len = sizeof(this->received_) * 8;
  uint16_t offset = (id - this->offset_) & 0x7FFF;
  // Max id that the ATEM can send is 0x7FFF

  // Make room
  if (offset < recv_len) {
    this->received_ <<= offset;
    this->offset_ = id;
  }

  // Offset maybe move, recalculate
  offset = abs(id - this->offset_);

  // Check if already parced
  if (this->received_ & 1 << offset) return -2;

  // Add the id to the received list
  this->received_ |= 1 << offset;

  // Check for missing id's
  if (this->received_ != 0xFFFFFFFF)
    for (int16_t i = recv_len - 1; i != 0; i--)
      if (!(this->received_ & 1 << i)) return ((this->offset_ - i) & 0x7FFF);

  return -1;
}

}  // namespace atem
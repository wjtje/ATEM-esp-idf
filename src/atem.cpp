#include "atem.h"

namespace atem {

static const char *TAG{"Atem"};
ESP_EVENT_DEFINE_BASE(ATEM_EVENT);

Atem::Atem(const char *address) {
  // Clear variables
  memset(&this->top_, 0, sizeof(this->top_));
  memset(&this->ver_, 0, sizeof(this->ver_));
  memset(&this->mpl_, 0, sizeof(this->mpl_));

  // Try to create a socket
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;  // IPv4
  hints.ai_socktype = SOCK_DGRAM;

  if ((rv = getaddrinfo(address, "9910", &hints, &servinfo)) != 0) {
    ESP_LOGE(TAG, "Failed to get address info");
    return;
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((this->sockfd_ =
             socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      continue;

    if (connect(this->sockfd_, p->ai_addr, p->ai_addrlen) == -1) {
      close(this->sockfd_);
      continue;
    }

    break;
  }

  if (p == NULL) {
    ESP_LOGE(TAG, "Failed to connect to ATEM");
    return;
  }

  freeaddrinfo(servinfo);

  // Set socket timeout
  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  if ((rv = setsockopt(this->sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                       sizeof timeout)) != 0) {
    ESP_LOGE(TAG, "Failed to setsockopt (%s)", strerror(rv));
  }

  // Pre allocate send vector
#if CONFIG_ATEM_STORE_SEND
  if (xSemaphoreTake(this->send_mutex_, pdMS_TO_TICKS(50))) {
    this->send_packets_.reserve(32);
    xSemaphoreGive(this->send_mutex_);
  } else {
    ESP_LOGE(TAG, "Failed to pre allocate send packet buffer");
  }
#endif

  // Create background task
  if (unlikely(!xTaskCreate([](void *a) { ((Atem *)a)->task_(); }, "atem",
                            5 * 1024, this, configMAX_PRIORITIES - 1,
                            &this->task_handle_))) {
    ESP_LOGE(TAG, "Failed to create task");
    return;
  }

  this->Reconnect_();
}

Atem::~Atem() {
  if (this->task_handle_ != nullptr) {
    vTaskDelete(this->task_handle_);
  }

  // Clear cached packages
#if CONFIG_ATEM_STORE_SEND
  xSemaphoreTake(this->send_mutex_, portMAX_DELAY);
  for (auto p : this->send_packets_) delete p;
  this->send_packets_.clear();
  xSemaphoreGive(this->send_mutex_);
#endif

  // Clear memory
  xSemaphoreTake(this->state_mutex_, portMAX_DELAY);
  this->input_properties_.clear();
  // Clear ME (and the keyer state)
  if (this->me_ != nullptr) {
    for (int i = 0; i < this->top_.me; i++) {
      if (this->me_[i].keyer != nullptr) free(this->me_[i].keyer);
    }
    free(this->me_);
    this->me_ = nullptr;
  }
  if (this->dsk_ != nullptr) {
    free(this->dsk_);
    this->dsk_ = nullptr;
  }
  delete this->aux_out_;
  delete this->mps_;
  if (this->mps_ != nullptr) {
    free(this->mps_);
    this->mps_ = nullptr;
  }
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

    // Something went wrong
    if (len < 0) {
      if (errno != EAGAIN) {
        ESP_LOGE(TAG, "recv error: %s (%i)", strerror(errno), errno);
        continue;
      }

      if (ack_count > 4) {  // Already send multiple ACK requests
        if (ack_count != INT_MAX) {
          ESP_LOGW(TAG, "The connection seems dead, reconnecting");
          ack_count = INT_MAX;
        }

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
      ESP_LOGE(TAG, "Next package is larger than the buffer");
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
      ESP_LOGI(TAG, "Initialization done");
      this->session_id_ = packet.GetSessionId();
      this->state_ = ConnectionState::ACTIVE;

      // Send event's
      for (int32_t i = 0; i < sizeof(boot_events) * 8; i++)
        if (boot_events & 1 << i) {
          esp_event_post(ATEM_EVENT, i, nullptr, 0, 0);
        }
    }

    // RESEND request
    if (packet.GetFlags() & 0x8 && this->state_ == ConnectionState::ACTIVE) {
      ESP_LOGW(TAG, "<- Resend request for %u", packet.GetResendId());
      bool send = false;

      // Try to find the packet
#if CONFIG_ATEM_STORE_SEND
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
#endif

      // We don't have this packet, just pretend it was an ACK
      if (!send) {
        AtemPacket p = AtemPacket(0x1, packet.GetSessionId(), 12);
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
        ESP_LOGD(TAG, "Received duplicate packet with id %u", packet.GetId());
        continue;  // We can ignore this packet
      } else if (missing_id >= 0) {
        ESP_LOGW(TAG, "Missing packet %u, trying to request it", missing_id);

        // Request missing
        AtemPacket p = AtemPacket(0x8, this->session_id_, 12);
        p.SetResendId(missing_id);
        this->SendPacket_(&p);
      }
    }

#if CONFIG_ATEM_STORE_SEND
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
#endif

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
        case ATEM_CMD("_MeC"): {  // Mix Effect Config
          event |= 1 << ATEM_EVENT_TOPOLOGY;
          uint8_t me = command.GetData(0);
          uint8_t num_keyer = command.GetData(1);

          if (this->me_ == nullptr || this->top_.me - 1 < me) break;

          this->me_[me].num_keyers = num_keyer;
          this->me_[me].keyer =
              (types::UskState *)calloc(num_keyer, sizeof(types::UskState));
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

          // Clear allocated memeory
          if (this->me_ != nullptr) {
            for (int i = 0; i < this->top_.me; i++) {
              if (this->me_[i].keyer != nullptr) free(this->me_[i].keyer);
            }
            free(this->me_);
          }

          this->top_.me = command.GetData(0);
          this->top_.sources = command.GetData(1);
          this->top_.dsk = command.GetData(2);
          this->top_.aux = command.GetData(3);
          this->top_.mixminus_outputs = command.GetData(4);
          this->top_.mediaplayers = command.GetData(5);
          this->top_.multiviewers = command.GetData(6);
          this->top_.rs485 = command.GetData(7);
          this->top_.hyperdecks = command.GetData(8);
          this->top_.dve = command.GetData(9);
          this->top_.stingers = command.GetData(10);
          this->top_.supersources = command.GetData(11);
          this->top_.talkback_channels = command.GetData(13);
          this->top_.camera_control = command.GetData(18);

          // Clear memory
          if (this->dsk_ != nullptr) free(this->dsk_);
          delete this->aux_out_;
          if (this->mps_ != nullptr) free(this->mps_);

          // Allocate buffers
          this->me_ = (atem::types::MixEffectState *)calloc(
              this->top_.me, sizeof(types::MixEffectState));
          this->dsk_ = (types::DskState *)calloc(this->top_.dsk,
                                                 sizeof(types::DskState));
          this->aux_out_ = new types::Source[this->top_.aux];
          this->mps_ = (types::MediaPlayerSource *)calloc(
              this->top_.mediaplayers, sizeof(types::MediaPlayerSource));
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
        case ATEM_CMD("FtbS"): {  // Fade to black State
          event |= 1 << ATEM_EVENT_FADE_TO_BLACK;
          me = command.GetData<uint8_t *>()[0];
          if (this->me_ == nullptr || this->top_.me - 1 < me) break;
          this->me_[me].ftb = {
              .fully_black = bool(command.GetData<uint8_t *>()[1]),
              .in_transition = bool(command.GetData<uint8_t *>()[2]),
          };
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
          memcpy(inpr.name_short, command.GetData<uint8_t *>() + 22,
                 sizeof(inpr.name_short));

          // Store inpr
          this->input_properties_[source] = inpr;

          break;
        }
        case ATEM_CMD("KeBP"): {  // Key properties
          event |= 1 << ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->me_ == nullptr || this->top_.me - 1 < me ||
              this->me_[me].num_keyers - 1 < keyer)
            break;

          usk_state = this->me_[me].keyer + keyer;
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
          if (this->me_ == nullptr || this->top_.me - 1 < me ||
              this->me_[me].num_keyers - 1 < keyer)
            break;

          (this->me_[me].keyer + keyer)->dve_ = {
              .size_x = (int)ntohl(command.GetData<uint32_t *>()[1]),
              .size_y = (int)ntohl(command.GetData<uint32_t *>()[2]),
              .pos_x = (int)ntohl(command.GetData<uint32_t *>()[3]),
              .pos_y = (int)ntohl(command.GetData<uint32_t *>()[4]),
              .rotation = (int)ntohl(command.GetData<uint32_t *>()[5]),
          };
          break;
        }
        case ATEM_CMD("KeFS"): {  // Key Fly State
          event |= 1 < ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->me_ == nullptr || this->top_.me - 1 < me ||
              this->me_[me].num_keyers - 1 < keyer)
            break;

          (this->me_[me].keyer + keyer)->at_key_frame =
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
        case ATEM_CMD("StRS"): {  // Stream Status
          if (command.GetLength() != 12) continue;
          event |= 1 << ATEM_EVENT_STREAM;
          this->stream_ = (types::StreamState)(command.GetData<uint8_t *>()[1]);
          break;
        }
        case ATEM_CMD("TrPs"): {  // Transition Position
          event |= 1 << ATEM_EVENT_TRANSITION_POSITION;
          me = command.GetData<uint8_t *>()[0];
          state = command.GetData<uint8_t *>()[1];
          pos = ntohs(command.GetData<uint16_t *>()[2]);

          if (this->me_ == nullptr || this->top_.me - 1 < me) break;
          this->me_[me].transition.in_transition = (bool)(state & 0x01);
          this->me_[me].transition.position = pos;
          break;
        }
        case ATEM_CMD("TrSS"): {  // Transition State
          event |= 1 << ATEM_EVENT_TRANSITION_STATE;
          me = command.GetData<uint8_t *>()[0];

          if (this->me_ == nullptr || this->top_.me - 1 < me) break;
          this->me_[me].transition.style = command.GetData<uint8_t *>()[1];
          this->me_[me].transition.next = command.GetData<uint8_t *>()[2];
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
}  // namespace atem

esp_err_t Atem::SendPacket_(AtemPacket *packet) {
  ESP_LOG_BUFFER_HEXDUMP(TAG, packet->GetData(), packet->GetLength(),
                         ESP_LOG_VERBOSE);

  ESP_LOGD(TAG, "-> Flags: %02X, ACK: %04X, Resend: %04X, Id: %04X, Len: %u",
           packet->GetFlags(), packet->GetAckId(), packet->GetResendId(),
           packet->GetId(), packet->GetLength());

  int len = send(this->sockfd_, packet->GetData(), packet->GetLength(), 0);
  if (len != packet->GetLength()) {
    if (this->state_ >= ConnectionState::INITIALIZING)
      ESP_LOGW(TAG, "Failed to send packet: %u", packet->GetId());
    return ESP_FAIL;
  }
  return ESP_OK;
}

void Atem::Reconnect_() {
  if (this->state_ != ConnectionState::CONNECTED)
    ESP_LOGI(TAG, "Reconnecting to ATEM");

  // Reset local variables
  this->state_ = ConnectionState::CONNECTED;
  this->local_id_ = 0;
  this->remote_id_ = 0;
  this->offset_ = 1;
  this->received_ = 0xFFFFFFFE;
  this->session_id_ = 0x0B06;

  // Remove all packets
#if CONFIG_ATEM_STORE_SEND
  xSemaphoreTake(this->send_mutex_, portMAX_DELAY);
  for (auto p : this->send_packets_) delete p;
  this->send_packets_.clear();
  xSemaphoreGive(this->send_mutex_);
#endif

  // Send init request
  AtemPacket p = AtemPacket(0x2, this->session_id_, 20);
  ((uint8_t *)p.GetData())[12] = 0x01;
  this->SendPacket_(&p);
}

esp_err_t Atem::SendCommands(const std::vector<AtemCommand *> &commands) {
  // Get the length of the commands
  uint16_t length = 12;  // Packet header
  uint16_t amount = 0;
  for (auto c : commands) {
    if (unlikely(c == nullptr)) continue;
    length += c->GetLength();
    amount++;
  }

  if (length == 12) return ESP_ERR_INVALID_ARG;  // Don't send empty commands
  ESP_LOGD(TAG, "Sending %u commands (%u bytes)", amount, length);

  // Create the packet
  AtemPacket *packet = new AtemPacket(0x1, this->session_id_, length);
  packet->SetId(++this->local_id_);

  // Copy commands into packet
  uint16_t i = 12;
  for (auto c : commands) {
    if (unlikely(c == nullptr)) continue;
    c->PrepairCommand(this->ver_);
    memcpy((uint8_t *)packet->GetData() + i, c->GetRawData(), c->GetLength());
    i += c->GetLength();
    delete c;
  }

  if (unlikely(i != length)) {
    delete packet;
    return ESP_FAIL;
  }

  // Send the packet
  if (this->SendPacket_(packet) != ESP_OK) {
    delete packet;
    return ESP_FAIL;
  }

#if CONFIG_ATEM_STORE_SEND
  // Store packet in send_packets_
  if (xSemaphoreTake(this->send_mutex_, pdMS_TO_TICKS(10))) {
    if (this->send_packets_.size() >= 32) {
      AtemPacket *p = this->send_packets_.back();
      this->send_packets_.pop_back();
      delete p;
    }

    this->send_packets_.push_back(packet);
    xSemaphoreGive(this->send_mutex_);
    return ESP_OK;
  }

  ESP_LOGW(TAG, "Failed to store packet (MUTEX FAIL)");
  delete packet;
  return ESP_ERR_TIMEOUT;
#else
  delete packet;
  return ESP_OK;
#endif
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
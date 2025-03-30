#include "atem.h"

namespace atem {

static const char *TAG{"Atem"};
ESP_EVENT_DEFINE_BASE(ATEM_EVENT);

// MARK: Constructor and deconstructor

Atem::Atem(const char *address) {
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
}

// MARK: Background task

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
    if (recv_len > sizeof(buffer)) {
      ESP_LOGE(TAG,
               "Next package (len: %i) is larger than the buffer (len: %u)",
               recv_len, sizeof(buffer));
      recv_len = sizeof(buffer);
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
    if (this->state_ == ConnectionState::kActive &&
        packet.GetSessionId() != this->session_id_) {
      ESP_LOGW(TAG,
               "Received packet with invalid session (%02x instead of %02x)",
               packet.GetSessionId(), this->session_id_);
      continue;
    }

    // INIT packet
    if (packet.GetFlags() & 0x2 && this->state_ != ConnectionState::kActive) {
      ESP_LOGD(TAG, "Received INIT");
      uint8_t init_status = ((const uint8_t *)packet.GetData())[12];

      if (init_status == 0x2) {  // INIT accepted
        this->local_id_ = 0;
        this->remote_id_ = 0;
        this->state_ = ConnectionState::kInitializing;
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
    if (this->state_ == ConnectionState::kInitializing &&
        packet.GetFlags() & 0x1 && packet.GetLength() == 12) {
      ESP_LOGI(TAG, "Initialization done");
      this->session_id_ = packet.GetSessionId();
      this->state_ = ConnectionState::kActive;

      // Send event's
      uint16_t packet_id = 1;  // Init packet ID
      for (int32_t i = 0; i < sizeof(boot_events) * 8; i++) {
        if (boot_events & (1 << i)) {
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              esp_event_post(ATEM_EVENT, i, &packet_id, sizeof(packet_id), 0));
        }
      }
    }

    // RESEND request
    if (packet.GetFlags() & 0x8 && this->state_ == ConnectionState::kActive) {
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
      bool should_parse_packet = true;

      AtemPacket p = AtemPacket(0x10, packet.GetSessionId(), 12);
      p.SetAckId(this->remote_id_);

      if (!this->sqeuence_.Add(packet.GetId())) {
        ESP_LOGD(TAG, "Received duplicate packet with id %u", packet.GetId());
        should_parse_packet = false;  // We can ignore this packet
      }

      // Check if we are receiving it in order
      const int16_t missing_id = this->sqeuence_.GetMissing();
      if (missing_id >= 0) {
        ESP_LOGW(TAG, "Missing packet %u, trying to request it", missing_id);

        // Request missing
        p.SetFlags(p.GetFlags() | 0x8);
        p.SetResendId(missing_id);
        p.SetId(0);
        p.SetUnknown(0x100);
      }

      if (state_ == ConnectionState::kActive || missing_id >= 0) {
        this->SendPacket_(&p);
      }

      if (!should_parse_packet) continue;
    }

#if CONFIG_ATEM_STORE_SEND
    // Receive ACK
    if (packet.GetFlags() & 0x10 && this->state_ == ConnectionState::kActive) {
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
    uint8_t me, keyer, channel, mediaplayer;
    size_t len;
    Source source;

    // Lock access to the state
    if (!xSemaphoreTake(this->state_mutex_, 150 / portTICK_PERIOD_MS)) {
      ESP_LOGW(TAG,
               "Failed to lock access to the state, make sure you only lock "
               "the state for max 100ms.");
      continue;
    }

    for (int i = 0; AtemCommand command : packet) {
      if (++i > 512) {  // Limit 512 command in a single packet
        ESP_LOGE(TAG, "To many commands in one package");
        break;
      }

      switch (ATEM_CMD(((char *)command.GetCmd()))) {
        case ATEM_CMD("_mpl"): {  // Media Player
          event |= 1 << ATEM_EVENT_MEDIA_PLAYER;

          const MediaPlayer media_player = {
              .still = command.GetData<uint8_t *>()[0],
              .clip = command.GetData<uint8_t *>()[1],
          };
          this->media_player_.Set(this->sqeuence_, media_player);
          break;
        }
        case ATEM_CMD("_MeC"): {  // Mix Effect Config
          event |= 1 << ATEM_EVENT_TOPOLOGY;
          uint8_t me = command.GetData(0);
          uint8_t num_keyer = command.GetData(1);

          if (this->mix_effect_.size() <= me) break;
          this->mix_effect_[me].keyer.resize(num_keyer);
          break;
        }
        case ATEM_CMD("_ver"): {  // Protocol version
          event |= 1 << ATEM_EVENT_PROTOCOL_VERSION;

          const ProtocolVersion version = {
              .major = command.GetDataS<uint16_t>(0),
              .minor = command.GetDataS<uint16_t>(1),
          };
          this->version_.Set(this->sqeuence_, version);
          break;
        }
        case ATEM_CMD("_pin"): {  // Product Id
          event |= 1 << ATEM_EVENT_PRODUCT_ID;
          memcpy(this->product_id_, command.GetData<char *>(),
                 sizeof(this->product_id_));

          len = strlen(command.GetData<char *>());
          if (len > 44) len = 44;
          memset(this->product_id_ + len, 0, sizeof(this->product_id_) - len);
          break;
        }
        case ATEM_CMD("_top"): {  // Topology
          event |= 1 << ATEM_EVENT_TOPOLOGY;

          const Topology top = {
              .me = command.GetData(0),
              .sources = command.GetData(1),
              .dsk = command.GetData(2),
              .aux = command.GetData(3),
              .mixminus_outputs = command.GetData(4),
              .mediaplayers = command.GetData(5),
              .multiviewers = command.GetData(6),
              .rs485 = command.GetData(7),
              .hyperdecks = command.GetData(8),
              .dve = command.GetData(9),
              .stingers = command.GetData(10),
              .supersources = command.GetData(11),
              .talkback_channels = command.GetData(13),
              .camera_control = command.GetData(18),
          };
          topology_.Set(this->sqeuence_, top);

          // Resize buffers
          this->mix_effect_.resize(top.me);
          this->dsk_.resize(top.dsk);
          this->aux_out_.resize(top.aux);
          this->media_player_source_.resize(top.mediaplayers);
          break;
        }
        case ATEM_CMD("AuxS"): {  // AUX Select
          event |= 1 << ATEM_EVENT_AUX;
          channel = command.GetData<uint8_t *>()[0];
          if (this->aux_out_.size() <= channel) break;

          this->aux_out_[channel].Set(this->sqeuence_,
                                      command.GetDataS<Source>(1));
          break;
        }
        case ATEM_CMD("DskB"): {  // DSK Source
          event |= 1 << ATEM_EVENT_DSK;
          keyer = command.GetData(0);
          if (this->dsk_.size() <= keyer) break;

          const DskSource source = {
              .fill = command.GetDataS<Source>(1),
              .key = command.GetDataS<Source>(2),
          };
          this->dsk_[keyer].source.Set(this->sqeuence_, source);
          break;
        }
        case ATEM_CMD("DskP"): {  // DSK Properties
          event |= 1 << ATEM_EVENT_DSK;
          keyer = command.GetData<uint8_t *>()[0];
          if (this->dsk_.size() <= keyer) break;

          const DskProperties properties{
              .tie = bool(command.GetData(1)),
          };
          this->dsk_[keyer].properties.Set(this->sqeuence_, properties);
          break;
        }
        case ATEM_CMD("DskS"): {  // DSK State
          event |= 1 << ATEM_EVENT_DSK;
          keyer = command.GetData<uint8_t *>()[0];
          if (this->dsk_.size() <= keyer) break;

          const DskState state = {
              .on_air = bool(command.GetData(1)),
              .in_transition = bool(command.GetData(2)),
              .is_auto_transitioning = bool(command.GetData(3)),
          };
          this->dsk_[keyer].state.Set(this->sqeuence_, state);
          break;
        }
        case ATEM_CMD("FtbS"): {  // Fade to black State
          event |= 1 << ATEM_EVENT_FADE_TO_BLACK;
          me = command.GetData<uint8_t *>()[0];

          const FadeToBlack ftb = {
              .fully_black = bool(command.GetData<uint8_t *>()[1]),
              .in_transition = bool(command.GetData<uint8_t *>()[2]),
          };

          if (this->mix_effect_.size() <= me) break;
          this->mix_effect_[me].ftb.Set(this->sqeuence_, ftb);
          break;
        }
        case ATEM_CMD("InPr"): {  // Input Property
          event |= 1 << ATEM_EVENT_INPUT_PROPERTIES;
          source = command.GetDataS<Source>(0);

          InputProperty inpr;
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
          auto it = input_properties_.find(source);
          if (it != this->input_properties_.end()) {
            (*it).second.Set(this->sqeuence_, inpr);
          } else {
            input_properties_.insert(
                {source, AtemState(this->sqeuence_, inpr)});
          }

          break;
        }
        case ATEM_CMD("KeBP"): {  // Usk properties
          event |= 1 << ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->mix_effect_.size() <= me) break;
          if (this->mix_effect_[me].keyer.size() <= keyer) break;

          const UskState state = {
              .type = command.GetData<uint8_t *>()[2],
              .fill = (Source)ntohs(command.GetData<uint16_t *>()[3]),
              .key = (Source)ntohs(command.GetData<uint16_t *>()[4]),
              .top = int16_t(ntohs(command.GetData<uint16_t *>()[6])),
              .bottom = int16_t(ntohs(command.GetData<uint16_t *>()[7])),
              .left = int16_t(ntohs(command.GetData<uint16_t *>()[8])),
              .right = int16_t(ntohs(command.GetData<uint16_t *>()[9])),
          };

          this->mix_effect_[me].keyer[keyer].state.Set(this->sqeuence_, state);
          break;
        }
        case ATEM_CMD("KeDV"): {  // Usk properties DVE
          event |= 1 << ATEM_EVENT_USK_DVE;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->mix_effect_.size() <= me) break;
          if (this->mix_effect_[me].keyer.size() <= keyer) break;

          const DveState properties = {
              .size_x = (int)ntohl(command.GetData<uint32_t *>()[1]),
              .size_y = (int)ntohl(command.GetData<uint32_t *>()[2]),
              .pos_x = (int)ntohl(command.GetData<uint32_t *>()[3]),
              .pos_y = (int)ntohl(command.GetData<uint32_t *>()[4]),
              .rotation = (int)ntohl(command.GetData<uint32_t *>()[5]),
          };
          this->mix_effect_[me].keyer[keyer].dve.Set(this->sqeuence_,
                                                     properties);
          break;
        }
        case ATEM_CMD("KeFS"): {  // Usk Fly State
          event |= 1 < ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->mix_effect_.size() <= me) break;
          if (this->mix_effect_[me].keyer.size() <= keyer) break;

          this->mix_effect_[me].keyer[keyer].at_key_frame.Set(
              this->sqeuence_, command.GetData<uint8_t *>()[6]);
          break;
        }
        case ATEM_CMD("KeOn"): {  // Key on Air
          event |= 1 << ATEM_EVENT_USK;
          me = command.GetData<uint8_t *>()[0];
          keyer = command.GetData<uint8_t *>()[1];

          // Check if we have allocated memory for this
          if (this->mix_effect_.size() <= me) break;
          if (keyer > 15) break;

          auto &usk_on_air = this->mix_effect_[me].usk_on_air;
          uint16_t state = usk_on_air.IsValid() ? usk_on_air.Get() : 0;

          state &= ~(0x1 << keyer);
          state |= (command.GetData<uint8_t *>()[2] << keyer);

          usk_on_air.Set(this->sqeuence_, state);
          break;
        }
        case ATEM_CMD("MPCE"): {  // Media Player Source
          event |= 1 << ATEM_EVENT_MEDIA_PLAYER;
          mediaplayer = command.GetData<uint8_t *>()[0];
          if (this->media_player_source_.size() <= mediaplayer) break;

          const MediaPlayerSource source = {
              .type = command.GetData(1),
              .still_index = command.GetData(2),
              .clip_index = command.GetData(3),
          };
          this->media_player_source_[mediaplayer].Set(this->sqeuence_, source);
          break;
        }
        case ATEM_CMD("MPfe"): {  // Media Pool Frame Description
          uint8_t type = command.GetData<uint8_t *>()[0];
          uint16_t index = command.GetDataS<uint16_t>(1);
          bool is_used = command.GetData<uint8_t *>()[4];

          if (type != 0) break;  // Only work with stills
          event |= 1 << ATEM_EVENT_MEDIA_POOL;

          // Clear index
          auto it = this->media_player_file_.find(index);
          if (it != this->media_player_file_.end())
            this->media_player_file_.erase(it);

          // Store file
          if (is_used) {
            const std::string file_name =
                std::string(command.GetData<char *>() + 24,
                            command.GetData<uint8_t *>()[23]);

            this->media_player_file_.insert(
                {index, AtemState(this->sqeuence_, std::move(file_name))});
          }
          break;
        }
        case ATEM_CMD("PrgI"): {  // Program Input
          event |= 1 << ATEM_EVENT_SOURCE;
          me = command.GetData<uint8_t *>()[0];

          if (this->mix_effect_.size() <= me) break;
          this->mix_effect_[me].program.Set(this->sqeuence_,
                                            command.GetDataS<Source>(1));
          break;
        }
        case ATEM_CMD("PrvI"): {  // Preview Input
          event |= 1 << ATEM_EVENT_SOURCE;
          me = command.GetData<uint8_t *>()[0];

          if (this->mix_effect_.size() <= me) break;
          this->mix_effect_[me].preview.Set(this->sqeuence_,
                                            command.GetDataS<Source>(1));
          break;
        }
        case ATEM_CMD("StRS"): {  // Stream Status
          if (command.GetLength() != 12) continue;
          event |= 1 << ATEM_EVENT_STREAM;
          this->stream_.Set(this->sqeuence_,
                            (StreamState)(command.GetData<uint8_t *>()[1]));
          break;
        }
        case ATEM_CMD("TrPs"): {  // Transition Position
          event |= 1 << ATEM_EVENT_TRANSITION_POSITION;

          me = command.GetData(0);
          if (this->mix_effect_.size() <= me) break;

          const TransitionPosition position = {
              .in_transition = (bool)(command.GetData(1) & 0x01),
              .position = command.GetDataS<uint16_t>(2),
          };
          this->mix_effect_[me].transition.position.Set(this->sqeuence_,
                                                        position);
          break;
        }
        case ATEM_CMD("TrSS"): {  // Transition State
          event |= 1 << ATEM_EVENT_TRANSITION_STATE;

          me = command.GetData(0);
          if (this->mix_effect_.size() <= me) break;

          const TransitionState state = {
              .style = static_cast<TransitionStyle>(command.GetData(1)),
              .next = command.GetData(2),
          };
          this->mix_effect_[me].transition.state.Set(this->sqeuence_, state);
          break;
        }
      }
    }

    xSemaphoreGive(this->state_mutex_);  // unlock the access

    // Send events
    if (event != 0 && this->state_ == ConnectionState::kActive) {
      uint16_t packet_id = packet.GetId();

      for (int32_t i = 0; i < sizeof(event) * 8; i++)
        if (event & (1 << i))
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              esp_event_post(ATEM_EVENT, i, &packet_id, sizeof(packet_id), 0));
    } else {
      boot_events |= event;
    }
  }

  vTaskDelete(nullptr);
}  // namespace atem

// MARK Public functions

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
    c->PrepairCommand(this->version_.Get());
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

// MARK: Private functions

esp_err_t Atem::SendPacket_(AtemPacket *packet) {
  ESP_LOG_BUFFER_HEXDUMP(TAG, packet->GetData(), packet->GetLength(),
                         ESP_LOG_VERBOSE);

  ESP_LOGD(TAG, "-> Flags: %02X, ACK: %04X, Resend: %04X, Id: %04X, Len: %u",
           packet->GetFlags(), packet->GetAckId(), packet->GetResendId(),
           packet->GetId(), packet->GetLength());

  int len = send(this->sockfd_, packet->GetData(), packet->GetLength(), 0);
  if (len != packet->GetLength()) {
    if (this->state_ >= ConnectionState::kInitializing)
      ESP_LOGW(TAG, "Failed to send packet: %u", packet->GetId());
    return ESP_FAIL;
  }
  return ESP_OK;
}

void Atem::Reconnect_() {
  const bool was_connected = this->product_id_[0] != '\0';
  if (was_connected) ESP_LOGI(TAG, "Reconnecting to ATEM");

  // Reset local variables
  this->state_ = ConnectionState::kConnected;
  this->local_id_ = 0;
  this->remote_id_ = 0;
  this->session_id_ = 0x0B06;
  this->sqeuence_ = SequenceCheck();

  // Clear state
  xSemaphoreTake(this->state_mutex_, portMAX_DELAY);
  this->input_properties_.clear();
  this->topology_ = AtemState<Topology>();
  this->version_ = AtemState<ProtocolVersion>();
  this->media_player_ = AtemState<MediaPlayer>();
  memset(this->product_id_, 0, sizeof(this->product_id_));
  this->mix_effect_.clear();
  this->dsk_.clear();
  this->aux_out_.clear();
  this->media_player_source_.clear();
  this->media_player_file_.clear();
  this->stream_ = AtemState<StreamState>();
  xSemaphoreGive(this->state_mutex_);

  // Remove all packets
#if CONFIG_ATEM_STORE_SEND
  xSemaphoreTake(this->send_mutex_, portMAX_DELAY);
  for (auto p : this->send_packets_) delete p;
  this->send_packets_.clear();
  xSemaphoreGive(this->send_mutex_);
#endif

  // Send event that Product ID has changed
  if (was_connected) {
    uint16_t packet_id = 0;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(
        ATEM_EVENT, ATEM_EVENT_PRODUCT_ID, &packet_id, sizeof(packet_id), 0));
  }

  // Send init request
  AtemPacket p = AtemPacket(0x2, this->session_id_, 20);
  ((uint8_t *)p.GetData())[12] = 0x01;
  this->SendPacket_(&p);
}

}  // namespace atem
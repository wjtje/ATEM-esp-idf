#include "atem.h"

namespace atem {

ESP_EVENT_DEFINE_BASE(ATEM_EVENT);

const char *TAG{"ATEM"};
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

  // Create background task
  if (unlikely(!xTaskCreate([](void *a) { ((Atem *)a)->task_(); }, "atem", 4096,
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

  // Clear memory
  delete this->pid_;
  delete this->me_;
  delete this->usk_;
  delete this->aux_inp_;
  delete this->mps_;

  xSemaphoreTake(this->send_mutex_, portMAX_DELAY);
  for (auto p : this->send_packets_) delete p.second;
  this->send_packets_.clear();
  xSemaphoreGive(this->send_mutex_);

  auto itr = this->input_properties_.begin();
  while (itr != this->input_properties_.end()) {
    types::InputProperty *i = itr->second;
    itr = this->input_properties_.erase(itr);
    delete i;
  }
}

void Atem::task_() {
  char buffer[CONFIG_PACKET_BUFFER_SIZE];
  AtemPacket packet(buffer);
  int ack_count = 0, len;

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
        auto p = new AtemPacket(0x11, this->session_id_, 12);
        p->SetId(++this->local_id_);
        p->SetAckId(this->remote_id_);
        this->SendPacket_(p);
        delete p;
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
        AtemPacket *p = new AtemPacket(0x10, packet.GetSessionId(), 12);
        this->SendPacket_(p);
        delete p;
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
      if (this->pid_ != nullptr) {
        // Show some basic information about the ATEM
        ESP_LOGI(TAG, "Protocol Version: %u.%u", this->ver_.major,
                 this->ver_.minor);
        ESP_LOGI(TAG, "Model: %s", this->pid_);
        ESP_LOGI(TAG, "Topology: ME(%u), sources(%u)", this->top_.me,
                 this->top_.sources);
      }

      ESP_LOGI(TAG, "Initialization done");
      this->session_id_ = packet.GetSessionId();
      this->state_ = ConnectionState::ACTIVE;
    }

    // RESEND request
    if (packet.GetFlags() & 0x8 && this->state_ == ConnectionState::ACTIVE) {
      ESP_LOGW(TAG, "<- Resend request for %u", packet.GetResendId());
      bool send = false;

      // Try to find the packet
      if (xSemaphoreTake(this->send_mutex_, 50 / portTICK_PERIOD_MS)) {
        if (this->send_packets_.contains(packet.GetAckId())) {
          this->SendPacket_(this->send_packets_[packet.GetAckId()]);
          send = true;
        }

        xSemaphoreGive(this->send_mutex_);
      }

      // We don't have this packet, just pretend it was an ACK
      if (!send) {
        AtemPacket *p = new AtemPacket(0x1, packet.GetSessionId(), 12);
        p->SetId(packet.GetResendId());
        this->SendPacket_(p);
        delete p;
      }
    }

    // Send ACK
    if (packet.GetFlags() & 0x1) {
      this->remote_id_ = packet.GetId();
      ESP_LOGD(TAG, "-> ACK %u", this->remote_id_);

      // Respond to ACK
      if (this->state_ == ConnectionState::ACTIVE) {
        AtemPacket *p = new AtemPacket(0x10, packet.GetSessionId(), 12);
        p->SetAckId(this->remote_id_);
        this->SendPacket_(p);
        delete p;
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
        auto p = new AtemPacket(0x8, this->session_id_, 12);
        p->SetResendId(missing_id);
        // ((uint8_t *)p->GetData())[8] = 0x01;
        this->SendPacket_(p);
        delete p;
      }
    }

    // Receive ACK
    if (packet.GetFlags() & 0x10 && this->state_ == ConnectionState::ACTIVE) {
      ESP_LOGD(TAG, "<- ACK %u", packet.GetAckId());

      if (xSemaphoreTake(this->send_mutex_, 50 / portTICK_PERIOD_MS)) {
        if (this->send_packets_.contains(packet.GetAckId())) {
          delete this->send_packets_[packet.GetAckId()];
          this->send_packets_.erase(packet.GetAckId());
        }
        xSemaphoreGive(this->send_mutex_);
      } else {
        ESP_LOGW(TAG, "Failed to note of ACK");
      }
    }

    // Check size of packet
    if (len <= 12 || packet.GetFlags() & 0x2) continue;

    // Parse packet
    ESP_LOGD(TAG, "Got packet with %u bytes", len);
    uint32_t event = 0;

    for (int i = 0; AtemCommand command : packet) {
      if (++i > 512) break;  // Limit 512 command in a single packet

      if (command == "_mpl") {  // Media Player
        event |= 1 << ATEM_EVENT_MEDIA_PLAYER;
        this->mpl_.still = command.GetData<uint8_t *>()[0];
        this->mpl_.clip = command.GetData<uint8_t *>()[1];
      } else if (command == "_ver") {  // Protocol version
        event |= 1 << ATEM_EVENT_PROTOCOL_VERSION;
        this->ver_ = {.major = ntohs(command.GetData<uint16_t *>()[0]),
                      .minor = ntohs(command.GetData<uint16_t *>()[1])};
      } else if (command == "_pin") {  // Product Id
        event |= 1 << ATEM_EVENT_PRODUCT_ID;
        delete this->pid_;
        size_t len = strlen(command.GetData<char *>()) + 1;
        this->pid_ = new char[len];
        memcpy(this->pid_, command.GetData<char *>(), len);
      } else if (command == "_top") {  // Topology
        event |= 1 << ATEM_EVENT_TOPOLOGY;
        memcpy(&this->top_, command.GetData<void *>(), sizeof(this->top_));

        // Clear memory
        delete this->me_;
        delete this->usk_;
        delete this->dsk_;
        delete this->aux_inp_;
        delete this->mps_;

        // Allocate buffers
        this->me_ = new types::MixEffectState[this->top_.me];
        this->usk_ = new types::UskState[this->top_.me * this->top_.usk];
        this->dsk_ = new types::DskState[this->top_.dsk];
        this->aux_inp_ = new types::Source[this->top_.aux];
        this->mps_ = new types::MediaPlayerSource[this->top_.mediaplayers];
      } else if (command == "AuxS") {  // AUX Select
        event |= 1 << ATEM_EVENT_AUX;
        uint8_t channel = command.GetData<uint8_t *>()[0];
        if (this->aux_inp_ == nullptr || this->top_.aux - 1 < channel) continue;

        this->aux_inp_[channel] =
            (types::Source)ntohs(command.GetData<uint16_t *>()[1]);
      } else if (command == "DskB") {  // DSK Source
        event |= 1 << ATEM_EVENT_DSK;
        uint8_t keyer = command.GetData<uint8_t *>()[0];
        if (this->dsk_ == nullptr || this->top_.dsk - 1 < keyer) continue;

        this->dsk_[keyer].fill =
            (types::Source)ntohs(command.GetData<uint16_t *>()[1]);
        this->dsk_[keyer].key =
            (types::Source)ntohs(command.GetData<uint16_t *>()[2]);
      } else if (command == "DskP") {  // DSK Properties
        event |= 1 << ATEM_EVENT_DSK;
        uint8_t keyer = command.GetData<uint8_t *>()[0];
        if (this->dsk_ == nullptr || this->top_.dsk - 1 < keyer) continue;

        this->dsk_[keyer].tie = command.GetData<uint8_t *>()[1];
      } else if (command == "DskS") {  // DSK State
        event |= 1 << ATEM_EVENT_DSK;
        uint8_t keyer = command.GetData<uint8_t *>()[0];
        if (this->dsk_ == nullptr || this->top_.dsk - 1 < keyer) continue;

        this->dsk_[keyer].on_air = command.GetData<uint8_t *>()[1];
        this->dsk_[keyer].in_transition = command.GetData<uint8_t *>()[2];
        this->dsk_[keyer].is_auto_transitioning =
            command.GetData<uint8_t *>()[3];
      } else if (command == "InPr") {  // Input Property
        event |= 1 << ATEM_EVENT_INPUT_PROPERTIES;
        auto source = command.GetDataS<types::Source>(0);
        types::InputProperty *inpr = new types::InputProperty;

        memcpy(inpr->name_long, command.GetData<uint8_t *>() + 2,
               sizeof(inpr->name_long));
        memcpy(inpr->name_short, command.GetData<uint8_t *>() + 22,
               sizeof(inpr->name_short));

        // Clean garbage at the end of the string
        size_t len;
        len = strlen(inpr->name_long);
        if (len > 20) len = 20;
        memset(inpr->name_long + len, 0, sizeof(inpr->name_long) - len);
        len = strlen(inpr->name_short);
        if (len > 4) len = 4;
        memset(inpr->name_short + len, 0, sizeof(inpr->name_short) - len);

        // Remove if already exists
        if (this->input_properties_.contains(source)) {
          delete this->input_properties_[source];
          this->input_properties_.erase(source);
        }

        this->input_properties_[source] = inpr;
      } else if (command == "KeBP") {  // Key properties
        event |= 1 << ATEM_EVENT_USK;
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t keyer = command.GetData<uint8_t *>()[1];

        // Check if we have allocated memory for this
        if (this->usk_ == nullptr || this->top_.me - 1 < me ||
            this->top_.usk - 1 < keyer)
          continue;

        auto prop = &this->usk_[me * this->top_.usk + keyer];
        prop->type = command.GetData<uint8_t *>()[2];
        prop->fill = (types::Source)ntohs(command.GetData<uint16_t *>()[3]);
        prop->key = (types::Source)ntohs(command.GetData<uint16_t *>()[4]);
        prop->top = ntohs(command.GetData<uint16_t *>()[6]);
        prop->bottom = ntohs(command.GetData<uint16_t *>()[7]);
        prop->left = ntohs(command.GetData<uint16_t *>()[8]);
        prop->right = ntohs(command.GetData<uint16_t *>()[9]);
      } else if (command == "KeDV") {  // Key properties DVE
        event |= 1 << ATEM_EVENT_USK;
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t keyer = command.GetData<uint8_t *>()[1];

        // Check if we have allocated memory for this
        if (this->usk_ == nullptr || this->top_.me - 1 < me ||
            this->top_.usk - 1 < keyer)
          continue;

        auto prop = &this->usk_[me * this->top_.usk + keyer].dve_;
        prop->size_x = ntohl(command.GetData<uint32_t *>()[1]);
        prop->size_y = ntohl(command.GetData<uint32_t *>()[2]);
        prop->pos_x = ntohl(command.GetData<uint32_t *>()[3]);
        prop->pos_y = ntohl(command.GetData<uint32_t *>()[4]);
        prop->rotation = ntohl(command.GetData<uint32_t *>()[5]);
      } else if (command == "KeFS") {  // Key Fly State
        event |= 1 < ATEM_EVENT_USK;
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t keyer = command.GetData<uint8_t *>()[1];

        // Check if we have allocated memory for this
        if (this->usk_ == nullptr || this->top_.me - 1 < me ||
            this->top_.usk - 1 < keyer)
          continue;

        this->usk_[me * this->top_.usk + keyer].at_key_frame =
            command.GetData<uint8_t *>()[6];
      } else if (command == "KeOn") {  // Key on Air
        event |= 1 << ATEM_EVENT_USK;
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t keyer = command.GetData<uint8_t *>()[1];
        uint8_t state = command.GetData<uint8_t *>()[2];

        if (this->me_ == nullptr || this->top_.me - 1 < me || keyer > 15)
          continue;

        this->me_[me].usk_on_air &= ~(0x1 << keyer);
        this->me_[me].usk_on_air |= (state << keyer);
      } else if (command == "MPCE") {  // Media Player Source
        event |= 1 << ATEM_EVENT_MEDIA_PLAYER;
        uint8_t mediaplayer = command.GetData<uint8_t *>()[0];
        if (this->top_.mediaplayers - 1 < mediaplayer) continue;

        this->mps_[mediaplayer] = {
            .type = command.GetData<uint8_t *>()[1],
            .still_index = command.GetData<uint8_t *>()[2],
            .clip_index = command.GetData<uint8_t *>()[3],
        };
      } else if (command == "PrgI") {  // Program Input
        event |= 1 << ATEM_EVENT_SOURCE;
        uint8_t me = command.GetData<uint8_t *>()[0];
        if (this->me_ == nullptr || this->top_.me - 1 < me) continue;
        this->me_[me].program = command.GetDataS<types::Source>(1);
      } else if (command == "PrvI") {  // Preview Input
        event |= 1 << ATEM_EVENT_SOURCE;
        uint8_t me = command.GetData<uint8_t *>()[0];
        if (this->me_ == nullptr || this->top_.me - 1 < me) continue;
        this->me_[me].preview = command.GetDataS<types::Source>(1);
      } else if (command == "TrPs") {  // Transition Position
        event |= 1 << ATEM_EVENT_TRANSITION;
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t state = command.GetData<uint8_t *>()[1];
        uint16_t pos = ntohs(command.GetData<uint16_t *>()[2]);

        if (this->me_ == nullptr || this->top_.me - 1 < me) continue;
        this->me_[me].trst_.in_transition = (bool)(state & 0x01);
        this->me_[me].trst_.position = pos;
      } else if (command == "TrSS") {  // Transition State
        event |= 1 << ATEM_EVENT_TRANSITION;
        uint8_t me = command.GetData<uint8_t *>()[0];

        if (this->me_ == nullptr || this->top_.me - 1 < me) continue;
        this->me_[me].trst_.style = command.GetData<uint8_t *>()[1];
        this->me_[me].trst_.next = command.GetData<uint8_t *>()[2];
      }
    }

    // Send events
    if (event != 0)
      for (int32_t i = 0; i < sizeof(event) * 8; i++)
        if (event & 1 << i)
          esp_event_post(ATEM_EVENT, i, this, sizeof(this), 0);
  }

  vTaskDelete(nullptr);
}

esp_err_t Atem::SendPacket_(AtemPacket *packet) {
  ESP_LOG_BUFFER_HEXDUMP(TAG, packet->GetData(), packet->GetLength(),
                         ESP_LOG_VERBOSE);

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
  for (auto p : this->send_packets_) delete p.second;
  this->send_packets_.clear();
  xSemaphoreGive(this->send_mutex_);

  // Send init request
  AtemPacket *p = new AtemPacket(0x2, this->session_id_, 20);
  ((uint8_t *)p->GetData())[12] = 0x01;
  this->SendPacket_(p);
  delete p;
}

void Atem::SendCommands(std::vector<AtemCommand *> commands) {
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
    c->PrepairCommand(this->GetProtocolVersion());
    memcpy((uint8_t *)packet->GetData() + i, c->GetRawData(), c->GetLength());
    i += c->GetLength();
    delete c;
  }

  // Send the packet
  this->SendPacket_(packet);

  // Store packet in send_packets_
  if (xSemaphoreTake(this->send_mutex_, 50 / portTICK_PERIOD_MS)) {
    this->send_packets_[this->local_id_] = packet;
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
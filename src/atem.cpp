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
  if (!ip4addr_aton(ip_obj->valuestring, &this->ip_)) {
    ESP_LOGE(TAG, "Expected key 'ip' to be a valid ip4 adress");
    return ESP_FAIL;
  }

  cJSON_GetObjectCheck(atem_obj, port, Number);
  this->port_ = (uint16_t)(port_obj->valueint);

  cJSON_GetObjectCheck(atem_obj, timeout, Number);
  this->timeout_ = (uint8_t)(timeout_obj->valueint);

  return ESP_OK;
}

Atem::Atem() {
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      config_manager::ConfigManager::Restore(&this->config_));

  // Allocate udp
  this->udp_ = udp_new();
  if (unlikely(this->udp_ == nullptr)) {
    ESP_LOGE(TAG, "Failed to alloc memory for udp connection");
    abort();
  }

  // Register udp callback
  udp_recv(this->udp_, this->recv_, this);

  // Connect to ATEM
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      udp_connect(this->udp_, this->config_.GetIp(), this->config_.GetPort()));

  // Allocate queue
  this->task_queue_ = xQueueCreate(10, sizeof(pbuf *));
  if (unlikely(this->task_queue_ == nullptr)) {
    ESP_LOGE(TAG, "Failed to allocate queue");
    abort();
  }

  // Create background task
  if (unlikely(!xTaskCreate([](void *a) { ((Atem *)a)->task_(); }, "atem", 4096,
                            this, 8, &this->task_handle_))) {
    ESP_LOGE(TAG, "Failed to create task");
    abort();
  }

  this->SendInit_();
}

Atem::~Atem() {
  vTaskDelete(this->task_handle_);

  AtemPacket *p;
  while (xQueueReceive(this->task_queue_, &p, 1)) delete p;

  udp_remove(this->udp_);

  // Clear memory
  delete this->prg_inp_;
  delete this->prv_inp_;
  delete this->trst_;
  delete this->aux_inp_;
  delete this->usk_on_air_;
  delete this->dve_;
  delete this->usk_;

  auto itr = this->input_properties_.begin();
  while (itr != this->input_properties_.end()) {
    types::InputProperty *i = itr->second;
    itr = this->input_properties_.erase(itr);
    delete i;
  }
}

void Atem::recv_(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr,
                 uint16_t port) {
  auto packet = new AtemPacket(p);

  if (packet->GetLength() != p->len) {  // Check Length
    ESP_LOGW(TAG, "Received package with invalid size (%u instead of %u)",
             p->len, packet->GetLength());
    delete packet;
    return;
  }

  if (((Atem *)arg)->state_ == ConnectionState::ACTIVE &&
      packet->GetSessionId() != ((Atem *)arg)->session_id_) {
    ESP_LOGW(TAG,
             "Received package with invalid session (%02x instead of %02x)",
             packet->GetSessionId(), ((Atem *)arg)->session_id_);
    delete packet;
    return;
  }

  if (packet->GetFlags() & 0x2 &&
      ((Atem *)arg)->state_ != ConnectionState::ACTIVE) {  // INIT
    ESP_LOGD(TAG, "Received INIT");
    uint8_t init_status = ((const uint8_t *)packet->GetData())[12];

    if (init_status == 0x2) {  // INIT accepted
      ((Atem *)arg)->state_ = ConnectionState::INITIALIZING;
      AtemPacket *p = new AtemPacket(0x10, packet->GetSessionId(), 12);
      ((Atem *)arg)->SendPacket_(p);
      delete p;
    } else if (init_status == 0x3) {  // No connection available
      ESP_LOGW(TAG,
               "Couldn't connect to the atem because it has no connection "
               "slot available");
    } else {
      ESP_LOGW(TAG, "Received an unknown INIT status (%02x)", init_status);
    }
  }

  if (((Atem *)arg)->state_ == ConnectionState::INITIALIZING &&
      packet->GetFlags() & 0x1 &&
      packet->GetLength() == 12) {  // INIT packages done
    // TODO: Check for missing INIT packages
    ESP_LOGI(TAG, "Initialization done");
    ((Atem *)arg)->session_id_ = packet->GetSessionId();
    ((Atem *)arg)->state_ = ConnectionState::ACTIVE;
    // Send event (because we didn't do that while initializing)
    esp_event_post(ATEM_EVENT, 0, nullptr, 0, 10 / portTICK_PERIOD_MS);
  }

  if (packet->GetFlags() & 0x1 &&
      ((Atem *)arg)->state_ == ConnectionState::ACTIVE) {  // ACK
    ESP_LOGD(TAG, "-> ACK %u", packet->GetRemoteId());
    // Respond to ACK
    AtemPacket *p = new AtemPacket(0x10, packet->GetSessionId(), 12);
    p->SetAckId(packet->GetRemoteId());
    ((Atem *)arg)->SendPacket_(p);
    delete p;
  }

  if (packet->GetFlags() & 0x8 &&
      ((Atem *)arg)->state_ == ConnectionState::ACTIVE) {  // RESEND
    ESP_LOGW(TAG, "<- Resend request for %u", packet->GetResendId());
    // We don't actually save the messages, just pretend that it was an ACK
    AtemPacket *p = new AtemPacket(0x1, packet->GetSessionId(), 12);
    p->SetLocalId(packet->GetResendId());
    ((Atem *)arg)->SendPacket_(p);
    delete p;
  }

  // Add to task queue
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (!xQueueSendFromISR(((Atem *)arg)->task_queue_, (void *)(&packet),
                         &xHigherPriorityTaskWoken)) {
    ESP_LOGE(TAG, "Failed to add package to queue");
    delete packet;
    return;
  }

  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
  return;
}

void Atem::task_() {
  AtemPacket *p;

  for (;;) {
    // Wait for package or timeout
    if (!xQueueReceive(
            this->task_queue_, &p,
            (this->config_.GetTimeout() * 1000) / portTICK_PERIOD_MS)) {
      ESP_LOGW(TAG, "Timeout, didn't receive anything for the last %ums",
               (this->config_.GetTimeout() * 1000));
      // Reset local variables
      this->local_id_ = 0;
      this->SendInit_();
      continue;
    }

    uint16_t len = p->GetLength();

    // Check size of packet
    if (len <= 12 || p->GetFlags() & 0x2) {
      delete p;
      continue;
    }

    // Parse packet
    ESP_LOGD(TAG, "Got packet with %u bytes", len);

    for (auto command : *p) {
      if (command == "_ver") {  // Protocol version
        this->ver_ = {.major = ntohs(command.GetData<uint16_t *>()[0]),
                      .minor = ntohs(command.GetData<uint16_t *>()[1])};
        ESP_LOGI(TAG, "Protocol Version: %u.%u", this->ver_.major,
                 this->ver_.minor);
      } else if (command == "_pin") {  // Product Id
        ESP_LOGI(TAG, "Model: %s", command.GetData<char *>());
      } else if (command == "_top") {  // Topology
        memcpy(&this->top_, command.GetData<void *>(), sizeof(this->top_));
        ESP_LOGI(TAG, "Topology: ME(%u), sources(%u)", this->top_.me,
                 this->top_.sources);

        // Clear memory
        delete this->prg_inp_;
        delete this->prv_inp_;
        delete this->trst_;
        delete this->aux_inp_;
        delete this->usk_on_air_;
        delete this->dve_;
        delete this->usk_;

        // Allocate buffers
        this->prg_inp_ = new types::Source[this->top_.me];
        this->prv_inp_ = new types::Source[this->top_.me];
        this->trst_ = new types::TransitionState[this->top_.me];
        this->aux_inp_ = new types::Source[this->top_.aux];
        this->usk_on_air_ = new uint8_t[this->top_.me];
        this->dve_ =
            new types::UskDveProperties[this->top_.me * this->top_.usk];
        this->usk_ = new types::UskProperties[this->top_.me * this->top_.usk];
      } else if (command == "AuxS") {  // AUX Select
        uint8_t channel = command.GetData<uint8_t *>()[0];
        types::Source source =
            (types::Source)ntohs(command.GetData<uint16_t *>()[1]);

        ESP_LOGI(TAG, "New aux: channel: %u source: %u", channel, source);

        if (this->aux_inp_ == nullptr || this->top_.aux - 1 < channel) continue;
        this->aux_inp_[channel] = source;
      } else if (command == "InPr") {  // Input Property
        types::Source source =
            (types::Source)ntohs(command.GetData<uint16_t *>()[0]);
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
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t keyer = command.GetData<uint8_t *>()[1];

        // Check if we have allocated memory for this
        if (this->dve_ == nullptr || this->top_.me - 1 < me ||
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
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t keyer = command.GetData<uint8_t *>()[1];

        // Check if we have allocated memory for this
        if (this->dve_ == nullptr || this->top_.me - 1 < me ||
            this->top_.usk - 1 < keyer)
          continue;

        auto prop = &this->dve_[me * this->top_.usk + keyer];
        prop->size_x = ntohl(command.GetData<uint32_t *>()[1]);
        prop->size_y = ntohl(command.GetData<uint32_t *>()[2]);
        prop->pos_x = ntohl(command.GetData<uint32_t *>()[3]);
        prop->pos_y = ntohl(command.GetData<uint32_t *>()[4]);
        prop->rotation = ntohl(command.GetData<uint32_t *>()[5]);
      } else if (command == "KeOn") {  // Key on Air
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t keyer = command.GetData<uint8_t *>()[1];
        uint8_t state = command.GetData<uint8_t *>()[2];

        ESP_LOGI(TAG, "ME: %u, keyer: %u, air: %u", me, keyer, state);

        if (this->usk_on_air_ == nullptr || this->top_.me - 1 < me || keyer > 8)
          continue;

        this->usk_on_air_[me] &= ~(0x1 << keyer);
        this->usk_on_air_[me] |= (state << keyer);
      } else if (command == "PrgI") {  // Program Input
        uint8_t me = command.GetData<uint8_t *>()[0];
        types::Source source =
            (types::Source)ntohs(command.GetData<uint16_t *>()[1]);

        ESP_LOGI(TAG, "New program: me: %u source: %u", me, source);

        if (this->prg_inp_ == nullptr || this->top_.me - 1 < me) continue;
        this->prg_inp_[me] = source;
      } else if (command == "PrvI") {  // Preview Input
        uint8_t me = command.GetData<uint8_t *>()[0];
        types::Source source =
            (types::Source)ntohs(command.GetData<uint16_t *>()[1]);

        ESP_LOGI(TAG, "New preview: me: %u source: %u", me, source);

        if (this->prv_inp_ == nullptr || this->top_.me - 1 < me) continue;
        this->prv_inp_[me] = source;
      } else if (command == "TrPs") {  // Transition Position
        uint8_t me = command.GetData<uint8_t *>()[0];
        uint8_t state = command.GetData<uint8_t *>()[1];
        uint16_t pos = ntohs(command.GetData<uint16_t *>()[2]);

        if (this->trst_ == nullptr || this->top_.me - 1 < me) continue;
        this->trst_[me].in_transition = (bool)(state & 0x01);
        this->trst_[me].position = pos;
      } else if (command == "TrSS") {  // Transition State
        uint8_t me = command.GetData<uint8_t *>()[0];

        if (this->trst_ == nullptr || this->top_.me - 1 < me) continue;
        this->trst_[me].style = command.GetData<uint8_t *>()[1];
        this->trst_[me].next = command.GetData<uint8_t *>()[2];

      } else if (command == "VidM") {  // Video Mode
        uint8_t mode = command.GetData<uint8_t *>()[0];

        ESP_LOGI(TAG, "New video mode: %u", mode);
      }

      // Send event
      if (this->state_ == ConnectionState::ACTIVE) {
        auto cmd = (uint8_t *)command.GetCmd();
        esp_event_post(ATEM_EVENT,
                       (cmd[0] << 24) | (cmd[1] << 16) | (cmd[2] << 8) | cmd[3],
                       command.GetData<void *>(), command.GetLength() - 8,
                       10 / portTICK_PERIOD_MS);
      }
    }

    delete p;
  }

  vTaskDelete(nullptr);
}

void Atem::SendPacket_(AtemPacket *packet) {
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      udp_send(this->udp_, packet->GetPacketBuffer()));
}

void Atem::SendInit_() {
  this->state_ = ConnectionState::CONNECTED;
  AtemPacket *p = new AtemPacket(0x2, 0x0B06, 20);
  ((uint8_t *)p->GetData())[12] = 0x01;
  this->SendPacket_(p);
  delete p;
}

void Atem::SendCommands(std::initializer_list<AtemCommand *> commands) {
  // Get the length of the commands
  uint16_t length = 12;  // Packet header
  for (auto c : commands) {
    if (unlikely(c == nullptr)) continue;
    length += c->GetLength();
  }

  if (length == 12) return;  // Don't send empty commands

  // Create the packet
  AtemPacket *packet = new AtemPacket(0x1, this->session_id_, length);
  packet->SetRemoteId(++this->local_id_);

  // Copy commands into packet
  uint16_t i = 12;
  for (auto c : commands) {
    if (unlikely(c == nullptr)) continue;
    memcpy((uint8_t *)packet->GetData() + i, c->GetRawData(), c->GetLength());
    i += c->GetLength();
    delete c;
  }

  // Send the packet
  this->SendPacket_(packet);
  delete packet;
}

}  // namespace atem
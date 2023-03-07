#include "atem_cmd.h"

#include "atem.h"

namespace atem {

void AtemCommunication::ParseCommand_(const char *cmd, ssize_t i) {
  // Protocol version
  if (!strcmp(cmd, "_ver")) {
    uint16_t major =
        (uint16_t)(this->recv_buffer_[i] << 8 | this->recv_buffer_[i + 1]);
    uint16_t minor =
        (uint16_t)(this->recv_buffer_[i + 2] << 8 | this->recv_buffer_[i + 3]);

    WCAF_LOG_INFO("ATEM Protocol version: %i.%i", major, minor);
  }

  // Product Id
  if (!strcmp(cmd, "_pin")) {
    WCAF_LOG_INFO("ATEM Product Id: %s", this->recv_buffer_ + i);
  }

  // Warning
  if (!strcmp(cmd, "Warn")) {
    WCAF_LOG_WARNING("ATEM: %s", this->recv_buffer_ + i);
  }

  // Initialization Completed
  if (!strcmp(cmd, "InCm")) {
    WCAF_LOG_INFO("Initialization Completed! at %u", this->switcher_pkt_id_);
    this->init_ = true;
  }

  // Program Input
  if (!strcmp(cmd, "PrgI")) {
    ProgramInput msg_ = {
        .ME = this->recv_buffer_[i],
        .source = (uint16_t)(this->recv_buffer_[i + 2] << 8 |
                             this->recv_buffer_[i + 3]),
    };
    esp_event_post(ATEM_EVENT, ATEM_CMD("PrgI"), &msg_, sizeof(ProgramInput),
                   TTW);
  }

  // Preview Input
  if (!strcmp(cmd, "PrvI")) {
    PreviewInput msg_ = {
        .ME = this->recv_buffer_[i],
        .source = (uint16_t)(this->recv_buffer_[i + 2] << 8 |
                             this->recv_buffer_[i + 3]),
        .visable = (bool)(this->recv_buffer_[i + 4] & 0x01),
    };
    esp_event_post(ATEM_EVENT, ATEM_CMD("PrvI"), &msg_, sizeof(PreviewInput),
                   TTW);
  }
}

esp_err_t AtemCommunication::SetPreviewInput(uint16_t videoSource, uint8_t ME) {
  uint8_t data[] = {ME, 0x00, HIGH_BYTE(videoSource), LOW_BYTE(videoSource)};
  return this->SendCommand_("CPvI", 4, data);
}

esp_err_t AtemCommunication::Cut(uint8_t ME) {
  uint8_t data[] = {ME, 0x00, 0x00, 0x00};
  return this->SendCommand_("DCut", 4, data);
}

}  // namespace atem

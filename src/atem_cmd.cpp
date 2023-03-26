#include "atem_cmd.h"

#include <sys/param.h>

#include "atem.h"
namespace atem {

void AtemCommunication::ParseCommand_(const char *cmd, ssize_t i) {
  // Protocol version
  if (!strcmp(cmd, "_ver")) {
    this->protocol_version_.major =
        (uint16_t)(this->recv_buffer_[i] << 8 | this->recv_buffer_[i + 1]);
    this->protocol_version_.minor =
        (uint16_t)(this->recv_buffer_[i + 2] << 8 | this->recv_buffer_[i + 3]);

    WCAF_LOG_INFO("ATEM Protocol version: %i.%i", this->protocol_version_.major,
                  this->protocol_version_.minor);
  }

  // Product Id
  if (!strcmp(cmd, "_pin")) {
    WCAF_LOG_INFO("ATEM Product Id: %s", this->recv_buffer_ + i);
    auto len = strlen((char *)this->recv_buffer_ + i) + 1;
    this->product_id_ = (char *)malloc(len);
    memcpy(this->product_id_, this->recv_buffer_ + i, len);
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

  // Input Propertie
  if (!strcmp(cmd, "InPr")) {
    auto inpr = (InputProperty *)malloc(sizeof(InputProperty));
    memset(inpr, NULL, sizeof(InputProperty));

    auto name_long = (char *)this->recv_buffer_ + i + 2;
    auto name_short = (char *)this->recv_buffer_ + i + 22;

    inpr->source =
        (Source)(this->recv_buffer_[i] << 8 | this->recv_buffer_[i + 1]);
    memcpy(inpr->name_long, name_long,
           MIN(sizeof(inpr->name_long) - 1, strlen(name_long)));
    memcpy(inpr->name_short, name_short,
           MIN(sizeof(inpr->name_short) - 1, strlen(name_short)));

    WCAF_LOG_DEFAULT("InPr: %u (%s) %s", inpr->source, inpr->name_short,
                     inpr->name_long);
    this->input_properties_.push_back(inpr);
  }

  // Program Input
  if (!strcmp(cmd, "PrgI")) {
    ProgramInput msg_ = {
        .ME = this->recv_buffer_[i],
        .source = (Source)(this->recv_buffer_[i + 2] << 8 |
                           this->recv_buffer_[i + 3]),
    };
    esp_event_post(ATEM_EVENT, ATEM_CMD("PrgI"), &msg_, sizeof(ProgramInput),
                   TTW);
  }

  // Preview Input
  if (!strcmp(cmd, "PrvI")) {
    PreviewInput msg_ = {
        .ME = this->recv_buffer_[i],
        .source = (Source)(this->recv_buffer_[i + 2] << 8 |
                           this->recv_buffer_[i + 3]),
        .visable = (bool)(this->recv_buffer_[i + 4] & 0x01),
    };
    esp_event_post(ATEM_EVENT, ATEM_CMD("PrvI"), &msg_, sizeof(PreviewInput),
                   TTW);
  }

  // AUX Channel
  if (!strcmp(cmd, "AuxS")) {
    AuxInput msg_ = {
        .channel = this->recv_buffer_[i],
        .source = (Source)(this->recv_buffer_[i + 2] << 8 |
                           this->recv_buffer_[i + 3]),
    };
    esp_event_post(ATEM_EVENT, ATEM_CMD("AuxS"), &msg_, sizeof(AuxInput), TTW);
  }

  // Transition Position
  if (!strcmp(cmd, "TrPs")) {
    TransitionPosition msg_ = {
        .ME = this->recv_buffer_[i],
        .in_transition = (bool)(this->recv_buffer_[i + 1] & 0x01),
        .position = (uint16_t)(this->recv_buffer_[i + 4] << 8 |
                               this->recv_buffer_[i + 5]),
    };
    esp_event_post(ATEM_EVENT, ATEM_CMD("TrPs"), &msg_,
                   sizeof(TransitionPosition), TTW);
  }
}

esp_err_t AtemCommunication::SetPreviewInput(uint16_t videoSource, uint8_t ME) {
  uint8_t data[] = {ME, 0x00, HIGH_BYTE(videoSource), LOW_BYTE(videoSource)};
  return this->SendCommand_("CPvI", 4, data);
}

esp_err_t AtemCommunication::SetAuxInput(uint16_t videoSource, uint8_t channel,
                                         bool active) {
  uint8_t data[] = {active, channel, HIGH_BYTE(videoSource),
                    LOW_BYTE(videoSource)};
  return this->SendCommand_("CAuS", 4, data);
}

esp_err_t AtemCommunication::Cut(uint8_t ME) {
  uint8_t data[] = {ME, 0x00, 0x00, 0x00};
  return this->SendCommand_("DCut", 4, data);
}

esp_err_t AtemCommunication::Auto(uint8_t ME) {
  uint8_t data[] = {ME, 0x00, 0x00, 0x00};
  return this->SendCommand_("DAut", 4, data);
}

}  // namespace atem

#include "atem_cmd.h"

#include "atem.h"

namespace atem {

void AtemCommunication::ParseCommand_(const char *cmd, ssize_t i) {
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
    };
    esp_event_post(ATEM_EVENT, ATEM_CMD("PrvI"), &msg_, sizeof(PreviewInput),
                   TTW);
  }
}

}  // namespace atem

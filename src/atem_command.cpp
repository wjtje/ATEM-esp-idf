#include "atem_command.h"

namespace atem {

AtemCommand::AtemCommand(const char *cmd, uint16_t length) {
  this->data_ = malloc(length);
  ((uint16_t *)this->data_)[0] = htons(length);
  memcpy((uint8_t *)this->data_ + 4, cmd, 4);
}

AtemCommand::~AtemCommand() {
  if (this->has_alloc_) {
    free(this->data_);
  }
}

}  // namespace atem

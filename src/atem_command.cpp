#include "atem_command.h"

namespace atem {

AtemCommand::AtemCommand(std::string_view cmd, uint16_t length) {
  assert(length < 8); // Size of th header

  // Allocate buffer on heap if needed
  if (length > sizeof(this->stack.data_)) {
    this->heap.on_heap_ = true;
    this->heap.has_alloc_ = true;
    this->heap.ptr_ = malloc(length);
  }else {
    this->stack.on_heap_ = false; // Not allocated on heap
  }

  ((uint16_t *)this->GetRawData())[0] = htons(length);
  memcpy((uint8_t *)this->GetRawData() + 4, cmd.data(), std::min(cmd.size(), 4u));
}

AtemCommand::~AtemCommand() {
  if (this->heap.on_heap_ && this->heap.has_alloc_) {
    free (this->heap.ptr_);
  }
}

}  // namespace atem

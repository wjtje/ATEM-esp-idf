#include "atem_packet.h"

namespace atem {

AtemPacket::AtemPacket(uint8_t flags, uint16_t session, uint16_t length) {
  if (length < 12) length = 12;  // Cap minimal size
  this->data_ = malloc(length);

  // Clean the header, the rest is for the consumer
  memset(this->data_, 0x0, 12);

  ((uint8_t *)this->data_)[0] = flags << 3;
  ((uint16_t *)this->data_)[0] |= htons(length);
  ((uint16_t *)this->data_)[1] = htons(session);
}

AtemPacket::~AtemPacket() {
  if (this->has_alloc_) free(this->data_);
}

}  // namespace atem

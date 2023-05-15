#include "atem_packet.h"

namespace atem {

AtemPacket::AtemPacket(pbuf *p) {
  this->p_ = p;
  this->data_ = p->payload;
}

AtemPacket::AtemPacket(uint8_t flags, uint16_t session, uint16_t length) {
  if (length < 12) length = 12;  // Cap minimal size

  this->p_ = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
  this->data_ = this->p_->payload;

  // Clean the header, the rest is for the consumer
  memset(this->data_, 0x0, 12);

  ((uint8_t *)this->data_)[0] = flags << 3;
  ((uint16_t *)this->data_)[0] |= htons(length);
  ((uint16_t *)this->data_)[1] = htons(session);
}

}  // namespace atem

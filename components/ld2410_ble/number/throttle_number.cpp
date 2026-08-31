#include "throttle_number.h"

namespace esphome {
namespace ld2410_ble{

void ThrottleNumber::control(float value) {
  this->publish_state(value);
  this->parent_->set_throttle(static_cast<uint16_t>(value * 1000.0f));
}

}  // namespace ld2410_ble
}  // namespace esphome

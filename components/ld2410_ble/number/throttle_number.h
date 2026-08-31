#pragma once

#include "esphome/components/number/number.h"
#include "../ld2410_ble.h"

namespace esphome {
namespace ld2410_ble{

// Purely local: unlike every other number in this component (timeout, gate thresholds,
// light_threshold), throttle never talks to the sensor at all -- it's an ESP32-side rate
// limit on how often an already-received periodic data frame actually gets processed. So
// control() just writes straight to the parent, no send_command_()/ACK round-trip needed.
// number::Number carries no setup()/Component of its own (that's why platforms like the
// built-in template number additionally inherit PollingComponent when they need one), so
// the initial state -- whatever `throttle:` in YAML set on the parent, or its default -- is
// published from publish_initial_state(), called once by the parent right after it takes
// ownership of this entity, instead of a separate initial_value/restore_value story that
// could drift out of sync with it.
class ThrottleNumber : public number::Number, public Parented<LD2410BLEComponent> {
 public:
  ThrottleNumber() = default;

  void publish_initial_state() { this->publish_state(this->parent_->get_throttle() / 1000.0f); }

 protected:
  void control(float value) override;
};

}  // namespace ld2410_ble
}  // namespace esphome

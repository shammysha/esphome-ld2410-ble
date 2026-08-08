#pragma once

#include "esphome/components/switch/switch.h"
#include "../ld2410.h"

namespace esphome {
namespace ld2410_ble{

// Pure software gate: no hardware command, just controls whether handle_periodic_data_()
// publishes the g0..g8 gate sensors to HA. See LD2410BLEComponent::handle_periodic_data_().
class CalibrateSwitch : public switch_::Switch, public Parented<LD2410BLEComponent> {
 public:
  CalibrateSwitch() = default;

 protected:
  void write_state(bool state) override;
};

}  // namespace ld2410
}  // namespace esphome

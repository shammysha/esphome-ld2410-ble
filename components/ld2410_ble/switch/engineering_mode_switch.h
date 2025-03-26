#pragma once

#include "esphome/components/switch/switch.h"
#include "../ld2410.h"

namespace esphome {
namespace ld2410_ble{

class EngineeringModeSwitch : public switch_::Switch, public Parented<LD2410BLEComponent> {
 public:
  EngineeringModeSwitch() = default;

 protected:
  void write_state(bool state) override;
};

}  // namespace ld2410
}  // namespace esphome

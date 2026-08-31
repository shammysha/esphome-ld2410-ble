#pragma once

#include "esphome/components/number/number.h"
#include "../ld2410_ble.h"

namespace esphome {
namespace ld2410_ble{

class GateThresholdNumber : public number::Number, public Parented<LD2410BLEComponent> {
 public:
  GateThresholdNumber(uint8_t gate);

 protected:
  uint8_t gate_;
  void control(float value) override;
};

}  // namespace ld2410_ble
}  // namespace esphome

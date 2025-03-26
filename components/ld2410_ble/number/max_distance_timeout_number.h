#pragma once

#include "esphome/components/number/number.h"
#include "../ld2410.h"

namespace esphome {
namespace ld2410_ble{

class MaxDistanceTimeoutNumber : public number::Number, public Parented<LD2410BLEComponent> {
 public:
  MaxDistanceTimeoutNumber() = default;

 protected:
  void control(float value) override;
};

}  // namespace ld2410
}  // namespace esphome

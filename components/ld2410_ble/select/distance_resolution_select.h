#pragma once

#include "esphome/components/select/select.h"
#include "../ld2410_ble.h"

namespace esphome {
namespace ld2410_ble{

class DistanceResolutionSelect : public select::Select, public Parented<LD2410BLEComponent> {
 public:
  DistanceResolutionSelect() = default;

 protected:
  void control(const std::string &value) override;
};

}  // namespace ld2410_ble
}  // namespace esphome

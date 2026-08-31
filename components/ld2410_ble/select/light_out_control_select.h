#pragma once

#include "esphome/components/select/select.h"
#include "../ld2410_ble.h"

namespace esphome {
namespace ld2410_ble{

class LightOutControlSelect : public select::Select, public Parented<LD2410BLEComponent> {
 public:
  LightOutControlSelect() = default;

 protected:
  void control(const std::string &value) override;
};

}  // namespace ld2410_ble
}  // namespace esphome

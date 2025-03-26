#pragma once

#include "esphome/components/button/button.h"
#include "../ld2410.h"

namespace esphome {
namespace ld2410_ble{

class QueryButton : public button::Button, public Parented<LD2410BLEComponent> {
 public:
  QueryButton() = default;

 protected:
  void press_action() override;
};

}  // namespace ld2410
}  // namespace esphome

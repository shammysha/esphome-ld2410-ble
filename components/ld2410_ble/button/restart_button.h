#pragma once

#include "esphome/components/button/button.h"
#include "../ld2410.h"

namespace esphome {
namespace ld2410_ble{

class RestartButton : public button::Button, public Parented<LD2410BLEComponent> {
 public:
  RestartButton() = default;

 protected:
  void press_action() override;
};

}  // namespace ld2410
}  // namespace esphome

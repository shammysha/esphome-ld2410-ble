#include "calibrate_switch.h"

namespace esphome {
namespace ld2410_ble{

void CalibrateSwitch::write_state(bool state) { this->publish_state(state); }

}  // namespace ld2410
}  // namespace esphome

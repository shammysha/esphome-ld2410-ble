#include "query_button.h"

namespace esphome {
namespace ld2410_ble{

void QueryButton::press_action() { this->parent_->read_all_info(); }

}  // namespace ld2410_ble
}  // namespace esphome

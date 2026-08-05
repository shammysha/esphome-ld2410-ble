#include "ld2410.h"
#include "esphome/core/log.h"
#include <utility>

#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#define highbyte(val) (uint8_t)((val) >> 8)
#define lowbyte(val) (uint8_t)((val) &0xff)

namespace esphome {
namespace ld2410_ble{

static const char *const TAG = "ld2410";

void LD2410BLEComponent::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
  // This fires on every GATT event, including ESP_GATTC_NOTIFY_EVT -- which is every single
  // sensor reading (~10/s while connected). ESP_LOGV keeps it out of INFO/DEBUG logs, where
  // it drowned out everything else; VERBOSE is still there for wire-level debugging.
  ESP_LOGV(TAG, "GATTS Event received: %d", event);

  switch (event) {

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *chr = this->parent()->get_characteristic(this->service_uuid_, this->char_notify_uuid_);

      if (chr == nullptr) {
        ESP_LOGE(TAG, "[%s] No notify service found at device. Does it really LD2410?", this->parent()->address_str());
        break;
      }

      this->handle = chr->handle;

      auto status = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), this->handle);
      if (status) {
        ESP_LOGE(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", status);
        break;
      }

      {
        char uuid_buf[esp32_ble::UUID_STR_LEN];
        ESP_LOGI(TAG, "Found notify characteristic %s on device %s", this->char_notify_uuid_.to_str(uuid_buf),
                this->parent()->address_str());
      }

      auto *cmd_chr = this->parent()->get_characteristic(this->service_uuid_, this->char_command_uuid_);
      if (cmd_chr == nullptr) {
        char cmd_uuid_buf[esp32_ble::UUID_STR_LEN];
        char svc_uuid_buf[esp32_ble::UUID_STR_LEN];
        ESP_LOGI("TAG", "Characteristic %s was not found in service %s",
                this->char_command_uuid_.to_str(cmd_uuid_buf), this->service_uuid_.to_str(svc_uuid_buf));
        break;
      }
      this->char_handle = cmd_chr->handle;
      this->char_props_ = cmd_chr->properties;

      if (this->char_props_ & ESP_GATT_CHAR_PROP_BIT_WRITE) {
        this->write_type_ = ESP_GATT_WRITE_TYPE_RSP;
        ESP_LOGI(TAG, "Write type: ESP_GATT_WRITE_TYPE_RSP");

      } else if (this->char_props_ & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) {
        this->write_type_ = ESP_GATT_WRITE_TYPE_NO_RSP;
        ESP_LOGI(TAG, "Write type: ESP_GATT_WRITE_TYPE_NO_RSP");

      } else {
        char uuid_buf[esp32_ble::UUID_STR_LEN];
        ESP_LOGE(TAG, "Characteristic %s does not allow writing", this->char_command_uuid_.to_str(uuid_buf));
        break;
      }
      this->node_state = espbt::ClientState::ESTABLISHED;
      {
        char uuid_buf[esp32_ble::UUID_STR_LEN];
        ESP_LOGD(TAG, "Found command characteristic %s on device %s", this->char_command_uuid_.to_str(uuid_buf),
                this->parent()->address_str());
      }

      this->node_state = espbt::ClientState::ESTABLISHED;
      if (this->ble_status_binary_sensor_ != nullptr) {
        this->ble_status_binary_sensor_->publish_state(true);
      }

      this->set_permissions();
      this->set_engineering_mode(true, /*force_ble=*/true);

      break;
    }

/*
    case ESP_GATTC_OPEN_EVT: {
      ESP_LOGW(TAG, "Connected!");
      if (param->open.status == ESP_GATT_OK) {
        ESP_LOGI(TAG, "Connected successfully!");
        break;
      }
      break;
    }
*/
    case ESP_GATTC_CLOSE_EVT: {
      ESP_LOGW(TAG, "Disconnected!");

      if (this->ble_status_binary_sensor_ != nullptr) {
        this->ble_status_binary_sensor_->publish_state(false);
      }
      break;
    }

    case ESP_GATTC_READ_CHAR_EVT: {
      if (param->read.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, "Error reading char at handle %d, status=%d", param->read.handle, param->read.status);
        break;
      }

      this->current_frame_source_ = FrameSource::BLE;
      this->handle_ack_data_(param->read.value, param->read.value_len);

      /*
      if (param->read.handle == this->char_handle) {
        this->handle_ack_data_(param->read.value, param->read.value_len);
      }
       */
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.handle == this->handle) {
        if (param->reg_for_notify.status != ESP_GATT_OK) {
          ESP_LOGW(TAG, "Error registering for notifications at handle %d, status=%d", param->reg_for_notify.handle, param->reg_for_notify.status);
          break;
        }
        this->node_state = espbt::ClientState::ESTABLISHED;
        char uuid_buf[esp32_ble::UUID_STR_LEN];
        ESP_LOGD(TAG, "Register for notify on %s complete", this->char_notify_uuid_.to_str(uuid_buf));
      }
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle == this->handle) {
        this->current_frame_source_ = FrameSource::BLE;
        this->handle_periodic_data_(param->notify.value, param->notify.value_len);
      }
      break;
    }

    default:
      return ;
  }
}

bool LD2410BLEComponent::parse_device(const espbt::ESPBTDevice &device) {
  if (!this->has_mac_suffix_ || this->ble_address_resolved_) {
    return false;
  }

  const uint8_t *address = device.address();
  if (address[4] != this->mac_suffix_[0] || address[5] != this->mac_suffix_[1]) {
    return false;
  }

  ESP_LOGI(TAG, "Found LD2410 by MAC suffix %02X:%02X -> %s", this->mac_suffix_[0], this->mac_suffix_[1],
          device.address_str().c_str());
  this->ble_address_resolved_ = true;

  if (this->parent() == nullptr) {
    ESP_LOGE(TAG, "mac_suffix matched but no ble_client is attached to redirect");
    return true;
  }

  this->parent()->set_address(device.address_uint64());
  this->parent()->set_enabled(true);
  this->parent()->connect();
  return true;
}

void LD2410BLEComponent::update() {
  if (this->parent() == nullptr) {
    return;  // BLE not configured for this instance (UART-only)
  }

  if (this->node_state != espbt::ClientState::ESTABLISHED) {
      ESP_LOGW(TAG, "Reconnecting to device");
      this->parent()->set_enabled(true);
      this->parent()->connect();
  } else {
    auto status = esp_ble_gattc_read_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->handle,
                                          ESP_GATT_AUTH_REQ_NONE);

    if (status) {
      this->status_set_warning();
      ESP_LOGW(TAG, "Error sending read request for sensor, status=%d", status);
    }
/*
    ESP_LOGCONFIG(TAG, "Setting up LD2410...");
    this->read_all_info();
    ESP_LOGCONFIG(TAG, "Mac Address : %s", const_cast<char *>(this->mac_.c_str()));
    ESP_LOGCONFIG(TAG, "Firmware Version : %s", const_cast<char *>(this->version_.c_str()));
    ESP_LOGCONFIG(TAG, "LD2410 setup complete.");
*/
  }
}

void LD2410BLEComponent::loop() {
  if (this->uart_enabled_) {
    while (this->available()) {
      this->readline_(this->read(), this->uart_buffer_, sizeof(this->uart_buffer_));
    }
  }
  this->update_transport_diagnostics_();
}

bool LD2410BLEComponent::should_use_uart_() {
  if (!this->uart_enabled_) {
    return false;  // UART not configured at all
  }
  if (this->uart_recently_healthy_()) {
    return true;  // UART is alive -> prefer it
  }
  if (!this->ble_recently_healthy_()) {
    return true;  // Neither transport looks alive -> still try the wired one
  }
  return false;  // UART is quiet but BLE is alive -> fail over
}

void LD2410BLEComponent::update_transport_diagnostics_() {
#ifdef USE_TEXT_SENSOR
  if (this->active_transport_text_sensor_ == nullptr) {
    return;
  }
  bool uart_active = this->should_use_uart_();
  if (this->transport_diag_published_ && uart_active == this->last_reported_uart_active_) {
    return;
  }
  this->transport_diag_published_ = true;
  this->last_reported_uart_active_ = uart_active;
  this->active_transport_text_sensor_->publish_state(uart_active ? "uart" : "ble");
#endif
}

void LD2410BLEComponent::readline_(int readch, uint8_t *buffer, int len) {
  if (readch < 0) {
    return;  // No data available
  }

  if (this->uart_buffer_pos_ < len - 1) {
    buffer[this->uart_buffer_pos_++] = readch;
    buffer[this->uart_buffer_pos_] = 0;
  } else {
    ESP_LOGW(TAG, "Max command length exceeded; ignoring");
    this->uart_buffer_pos_ = 0;
    return;
  }
  if (this->uart_buffer_pos_ < 4) {
    return;  // Not enough data to process yet
  }

  uint8_t *tail = &buffer[this->uart_buffer_pos_ - 4];
  if (tail[0] == 0xF8 && tail[1] == 0xF7 && tail[2] == 0xF6 && tail[3] == 0xF5) {
    ESP_LOGV(TAG, "Will handle Periodic Data (UART)");
    this->current_frame_source_ = FrameSource::UART;
    this->handle_periodic_data_(buffer, this->uart_buffer_pos_);
    this->uart_buffer_pos_ = 0;
  } else if (tail[0] == 0x04 && tail[1] == 0x03 && tail[2] == 0x02 && tail[3] == 0x01) {
    ESP_LOGV(TAG, "Will handle ACK Data (UART)");
    this->current_frame_source_ = FrameSource::UART;
    if (this->handle_ack_data_(buffer, this->uart_buffer_pos_)) {
      this->uart_buffer_pos_ = 0;
    } else {
      ESP_LOGV(TAG, "ACK Data incomplete");
    }
  }
}

void LD2410BLEComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LD2410:");
  ESP_LOGCONFIG(TAG, "  UART transport: %s", this->uart_enabled_ ? "enabled" : "not configured");
  ESP_LOGCONFIG(TAG, "  BLE transport: %s", this->parent() != nullptr ? "enabled" : "not configured");
  if (this->has_mac_suffix_) {
    ESP_LOGCONFIG(TAG, "  BLE MAC suffix: %02X:%02X (%s)", this->mac_suffix_[0], this->mac_suffix_[1],
                  this->ble_address_resolved_ ? "resolved" : "scanning");
  }
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "TargetBinarySensor", this->target_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "MovingTargetBinarySensor", this->moving_target_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "StillTargetBinarySensor", this->still_target_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OutPinPresenceStatusBinarySensor", this->out_pin_presence_status_binary_sensor_);
#endif
#ifdef USE_SWITCH
  LOG_SWITCH("  ", "EngineeringModeSwitch", this->engineering_mode_switch_);
  LOG_SWITCH("  ", "BluetoothSwitch", this->bluetooth_switch_);
#endif
#ifdef USE_BUTTON
  LOG_BUTTON("  ", "ResetButton", this->reset_button_);
  LOG_BUTTON("  ", "RestartButton", this->restart_button_);
  LOG_BUTTON("  ", "QueryButton", this->query_button_);
#endif
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "LightSensor", this->light_sensor_);
  LOG_SENSOR("  ", "MovingTargetDistanceSensor", this->moving_target_distance_sensor_);
  LOG_SENSOR("  ", "StillTargetDistanceSensor", this->still_target_distance_sensor_);
  LOG_SENSOR("  ", "MovingTargetEnergySensor", this->moving_target_energy_sensor_);
  LOG_SENSOR("  ", "StillTargetEnergySensor", this->still_target_energy_sensor_);
  LOG_SENSOR("  ", "DetectionDistanceSensor", this->detection_distance_sensor_);
  for (sensor::Sensor *s : this->gate_still_sensors_) {
    LOG_SENSOR("  ", "NthGateStillSesnsor", s);
  }
  for (sensor::Sensor *s : this->gate_move_sensors_) {
    LOG_SENSOR("  ", "NthGateMoveSesnsor", s);
  }
#endif
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "VersionTextSensor", this->version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "MacTextSensor", this->mac_text_sensor_);
  LOG_TEXT_SENSOR("  ", "ActiveTransportTextSensor", this->active_transport_text_sensor_);
#endif
#ifdef USE_SELECT
  LOG_SELECT("  ", "LightFunctionSelect", this->light_function_select_);
  LOG_SELECT("  ", "OutPinLevelSelect", this->out_pin_level_select_);
  LOG_SELECT("  ", "DistanceResolutionSelect", this->distance_resolution_select_);
#endif
#ifdef USE_NUMBER
  LOG_NUMBER("  ", "LightThresholdNumber", this->light_threshold_number_);
  LOG_NUMBER("  ", "MaxStillDistanceGateNumber", this->max_still_distance_gate_number_);
  LOG_NUMBER("  ", "MaxMoveDistanceGateNumber", this->max_move_distance_gate_number_);
  LOG_NUMBER("  ", "TimeoutNumber", this->timeout_number_);
  for (number::Number *n : this->gate_still_threshold_numbers_) {
    LOG_NUMBER("  ", "Still Thresholds Number", n);
  }
  for (number::Number *n : this->gate_move_threshold_numbers_) {
    LOG_NUMBER("  ", "Move Thresholds Number", n);
  }
#endif
}

void LD2410BLEComponent::read_all_info() {
  this->set_config_mode_(true);
  this->get_version_();
  this->get_mac_();
  this->get_distance_resolution_();
  this->get_light_control_();
  this->query_parameters_();
  this->set_config_mode_(false);
}

void LD2410BLEComponent::restart_and_read_all_info() {
  this->set_config_mode_(true);
  this->restart_();
  this->set_timeout(1000, [this]() { this->read_all_info(); });
}

bool LD2410BLEComponent::write_ble_(const std::vector<uint8_t> &data) {
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    ESP_LOGE(TAG, "Cannot write to BLE characteristic - not connected");
    return false;
  }

  ESP_LOGV(TAG, "Will write %d bytes: %s", data.size(), format_hex_pretty(data).c_str());

  esp_err_t err = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(),
      this->parent()->get_conn_id(),
      this->char_handle,
      data.size(),
      const_cast<uint8_t *>(data.data()),
      this->write_type_,
      ESP_GATT_AUTH_REQ_NONE
  );

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error writing to characteristic: %s!", esp_err_to_name(err));
    return false;
  }

  return true;
}

bool LD2410BLEComponent::send_command_(uint8_t command, const uint8_t *command_value, int command_value_len,
                                       bool force_ble) {
  ESP_LOGV(TAG, "Sending COMMAND %02X", command);

  int len = 2;
  if (command_value != nullptr)
    len += command_value_len;

  std::vector<uint8_t> data(&CMD_FRAME_HEADER[0], &CMD_FRAME_HEADER[sizeof(CMD_FRAME_HEADER)]);
  std::vector<uint8_t> postamble(&CMD_FRAME_END[0], &CMD_FRAME_END[sizeof(CMD_FRAME_END)]);

  data.push_back(lowbyte(len));
  data.push_back(highbyte(len));
  data.push_back(lowbyte(command));
  data.push_back(highbyte(command));

  if (command_value != nullptr) {
    for (int i = 0; i < command_value_len; i++) {
      data.push_back(command_value[i]);
    }
  }
  data.insert(data.end(), postamble.begin(), postamble.end());

  if (!force_ble && this->should_use_uart_()) {
    ESP_LOGV(TAG, "Will write %d bytes over UART: %s", data.size(), format_hex_pretty(data).c_str());
    this->write_array(data.data(), data.size());
    if (command != CMD_ENABLE_CONF && command != CMD_DISABLE_CONF) {
      delay(50);  // NOLINT
    }
    return true;
  }

  return this->write_ble_(data);
}

void LD2410BLEComponent::handle_periodic_data_(uint8_t *buffer, int len) {
  if (len < 12)
    return;  // 4 frame start bytes + 2 length bytes + 1 data end byte + 1 crc byte + 4 frame end bytes
  if (buffer[0] != 0xF4 || buffer[1] != 0xF3 || buffer[2] != 0xF2 || buffer[3] != 0xF1)  // check 4 frame start bytes
    return;
  if (buffer[7] != HEAD || buffer[len - 6] != END || buffer[len - 5] != CHECK)  // Check constant values
    return;  // data head=0xAA, data end=0x55, crc=0x00

  if (this->current_frame_source_ == FrameSource::UART) {
    this->last_uart_frame_millis_ = millis();
  } else {
    this->last_ble_frame_millis_ = millis();
  }

  /*
    Reduce data update rate to prevent home assistant database size grow fast
  */
  int32_t current_millis = millis();
  if (current_millis - last_periodic_millis_ < this->throttle_)
    return;
  last_periodic_millis_ = current_millis;

  /*
    Data Type: 7th
    0x01: Engineering mode
    0x02: Normal mode
  */
  bool engineering_mode = buffer[DATA_TYPES] == 0x01;
#ifdef USE_SWITCH
  if (this->engineering_mode_switch_ != nullptr &&
      current_millis - last_engineering_mode_change_millis_ > this->throttle_) {
    this->engineering_mode_switch_->publish_state(engineering_mode);
  }
#endif
#ifdef USE_BINARY_SENSOR
  /*
    Target states: 9th
    0x00 = No target
    0x01 = Moving targets
    0x02 = Still targets
    0x03 = Moving+Still targets
  */
  char target_state = buffer[TARGET_STATES];
  if (this->target_binary_sensor_ != nullptr) {
    this->target_binary_sensor_->publish_state(target_state != 0x00);
  }
  if (this->moving_target_binary_sensor_ != nullptr) {
    this->moving_target_binary_sensor_->publish_state(CHECK_BIT(target_state, 0));
  }
  if (this->still_target_binary_sensor_ != nullptr) {
    this->still_target_binary_sensor_->publish_state(CHECK_BIT(target_state, 1));
  }
#endif
  /*
    Moving target distance: 10~11th bytes
    Moving target energy: 12th byte
    Still target distance: 13~14th bytes
    Still target energy: 15th byte
    Detect distance: 16~17th bytes
  */
#ifdef USE_SENSOR
  if (this->moving_target_distance_sensor_ != nullptr) {
    int new_moving_target_distance = this->two_byte_to_int_(buffer[MOVING_TARGET_LOW], buffer[MOVING_TARGET_HIGH]);
    if (this->moving_target_distance_sensor_->get_state() != new_moving_target_distance)
      this->moving_target_distance_sensor_->publish_state(new_moving_target_distance);
  }
  if (this->moving_target_energy_sensor_ != nullptr) {
    int new_moving_target_energy = buffer[MOVING_ENERGY];
    if (this->moving_target_energy_sensor_->get_state() != new_moving_target_energy)
      this->moving_target_energy_sensor_->publish_state(new_moving_target_energy);
  }
  if (this->still_target_distance_sensor_ != nullptr) {
    int new_still_target_distance = this->two_byte_to_int_(buffer[STILL_TARGET_LOW], buffer[STILL_TARGET_HIGH]);
    if (this->still_target_distance_sensor_->get_state() != new_still_target_distance)
      this->still_target_distance_sensor_->publish_state(new_still_target_distance);
  }
  if (this->still_target_energy_sensor_ != nullptr) {
    int new_still_target_energy = buffer[STILL_ENERGY];
    if (this->still_target_energy_sensor_->get_state() != new_still_target_energy)
      this->still_target_energy_sensor_->publish_state(new_still_target_energy);
  }
  if (this->detection_distance_sensor_ != nullptr) {
    int new_detect_distance = this->two_byte_to_int_(buffer[DETECT_DISTANCE_LOW], buffer[DETECT_DISTANCE_HIGH]);
    if (this->detection_distance_sensor_->get_state() != new_detect_distance)
      this->detection_distance_sensor_->publish_state(new_detect_distance);
  }
  if (engineering_mode) {
    /*
      Moving distance range: 18th byte
      Still distance range: 19th byte
      Moving enery: 20~28th bytes
    */
    for (std::vector<sensor::Sensor *>::size_type i = 0; i != this->gate_move_sensors_.size(); i++) {
      sensor::Sensor *s = this->gate_move_sensors_[i];
      if (s != nullptr) {
        s->publish_state(buffer[MOVING_SENSOR_START + i]);
      }
    }
    /*
      Still energy: 29~37th bytes
    */
    for (std::vector<sensor::Sensor *>::size_type i = 0; i != this->gate_still_sensors_.size(); i++) {
      sensor::Sensor *s = this->gate_still_sensors_[i];
      if (s != nullptr) {
        s->publish_state(buffer[STILL_SENSOR_START + i]);
      }
    }
    /*
      Light sensor: 38th bytes
    */
    if (this->light_sensor_ != nullptr) {
      int new_light_sensor = buffer[LIGHT_SENSOR];
      if (this->light_sensor_->get_state() != new_light_sensor)
        this->light_sensor_->publish_state(new_light_sensor);
    }
  } else {
    for (auto *s : this->gate_move_sensors_) {
      if (s != nullptr && !std::isnan(s->get_state())) {
        s->publish_state(NAN);
      }
    }
    for (auto *s : this->gate_still_sensors_) {
      if (s != nullptr && !std::isnan(s->get_state())) {
        s->publish_state(NAN);
      }
    }
    if (this->light_sensor_ != nullptr && !std::isnan(this->light_sensor_->get_state())) {
      this->light_sensor_->publish_state(NAN);
    }
  }
#endif
#ifdef USE_BINARY_SENSOR
  if (engineering_mode) {
    if (this->out_pin_presence_status_binary_sensor_ != nullptr) {
      this->out_pin_presence_status_binary_sensor_->publish_state(buffer[OUT_PIN_SENSOR] == 0x01);
    }
  } else {
    if (this->out_pin_presence_status_binary_sensor_ != nullptr) {
      this->out_pin_presence_status_binary_sensor_->publish_state(false);
    }
  }
#endif
}

const char VERSION_FMT[] = "%u.%02X.%02X%02X%02X%02X";

std::string format_version(uint8_t *buffer) {
  std::string::size_type version_size = 256;
  std::string version;
  do {
    version.resize(version_size + 1);
    version_size = std::snprintf(&version[0], version.size(), VERSION_FMT, buffer[13], buffer[12], buffer[17],
                                 buffer[16], buffer[15], buffer[14]);
  } while (version_size + 1 > version.size());
  version.resize(version_size);
  return version;
}

const char MAC_FMT[] = "%02X:%02X:%02X:%02X:%02X:%02X";

const std::string UNKNOWN_MAC("unknown");
const std::string NO_MAC("08:05:04:03:02:01");

std::string format_mac(uint8_t *buffer) {
  std::string::size_type mac_size = 256;
  std::string mac;
  do {
    mac.resize(mac_size + 1);
    mac_size = std::snprintf(&mac[0], mac.size(), MAC_FMT, buffer[10], buffer[11], buffer[12], buffer[13], buffer[14],
                             buffer[15]);
  } while (mac_size + 1 > mac.size());
  mac.resize(mac_size);
  if (mac == NO_MAC) {
    return UNKNOWN_MAC;
  }
  return mac;
}

#ifdef USE_NUMBER
std::function<void(void)> set_number_value(number::Number *n, float value) {
  float normalized_value = value * 1.0;
  if (n != nullptr && (!n->has_state() || n->state != normalized_value)) {
    n->state = normalized_value;
    return [n, normalized_value]() { n->publish_state(normalized_value); };
  }
  return []() {};
}
#endif

bool LD2410BLEComponent::handle_ack_data_(uint8_t *buffer, int len) {
  ESP_LOGV(TAG, "Handling ACK DATA for COMMAND %02X", buffer[COMMAND]);
  if (len < 10) {
    ESP_LOGE(TAG, "Error with last command : incorrect length");
    return true;
  }
  if (buffer[0] != 0xFD || buffer[1] != 0xFC || buffer[2] != 0xFB || buffer[3] != 0xFA) {  // check 4 frame start bytes
    ESP_LOGE(TAG, "Error with last command : incorrect Header");
    return true;
  }
  if (buffer[COMMAND_STATUS] != 0x01) {
    ESP_LOGE(TAG, "Error with last command : status != 0x01");
    return true;
  }
  if (this->two_byte_to_int_(buffer[8], buffer[9]) != 0x00) {
    ESP_LOGE(TAG, "Error with last command , last buffer was: %u , %u", buffer[8], buffer[9]);
    return true;
  }

  if (this->current_frame_source_ == FrameSource::UART) {
    this->last_uart_frame_millis_ = millis();
  } else {
    this->last_ble_frame_millis_ = millis();
  }

  switch (buffer[COMMAND]) {
    case lowbyte(CMD_ENABLE_CONF):
      ESP_LOGV(TAG, "Handled Enable conf command");
      break;
    case lowbyte(CMD_DISABLE_CONF):
      ESP_LOGV(TAG, "Handled Disabled conf command");
      break;
    case lowbyte(CMD_VERSION):
      this->version_ = format_version(buffer);
      ESP_LOGV(TAG, "FW Version is: %s", const_cast<char *>(this->version_.c_str()));
#ifdef USE_TEXT_SENSOR
      if (this->version_text_sensor_ != nullptr) {
        this->version_text_sensor_->publish_state(this->version_);
      }
#endif
      break;
    case lowbyte(CMD_QUERY_DISTANCE_RESOLUTION): {
      std::string distance_resolution =
          DISTANCE_RESOLUTION_INT_TO_ENUM.at(this->two_byte_to_int_(buffer[10], buffer[11]));
      ESP_LOGV(TAG, "Distance resolution is: %s", const_cast<char *>(distance_resolution.c_str()));
#ifdef USE_SELECT
      if (this->distance_resolution_select_ != nullptr &&
          this->distance_resolution_select_->current_option() != distance_resolution) {
        this->distance_resolution_select_->publish_state(distance_resolution);
      }
#endif
    } break;
    case lowbyte(CMD_QUERY_LIGHT_CONTROL): {
      this->light_function_ = LIGHT_FUNCTION_INT_TO_ENUM.at(buffer[10]);
      this->light_threshold_ = buffer[11] * 1.0;
      this->out_pin_level_ = OUT_PIN_LEVEL_INT_TO_ENUM.at(buffer[12]);
      ESP_LOGV(TAG, "Light function is: %s", const_cast<char *>(this->light_function_.c_str()));
      ESP_LOGV(TAG, "Light threshold is: %f", this->light_threshold_);
      ESP_LOGV(TAG, "Out pin level is: %s", const_cast<char *>(this->out_pin_level_.c_str()));
#ifdef USE_SELECT
      if (this->light_function_select_ != nullptr &&
          this->light_function_select_->current_option() != this->light_function_) {
        this->light_function_select_->publish_state(this->light_function_);
      }
      if (this->out_pin_level_select_ != nullptr &&
          this->out_pin_level_select_->current_option() != this->out_pin_level_) {
        this->out_pin_level_select_->publish_state(this->out_pin_level_);
      }
#endif
#ifdef USE_NUMBER
      if (this->light_threshold_number_ != nullptr &&
          (!this->light_threshold_number_->has_state() ||
           this->light_threshold_number_->state != this->light_threshold_)) {
        this->light_threshold_number_->publish_state(this->light_threshold_);
      }
#endif
    } break;
    case lowbyte(CMD_MAC):
      if (len < 20) {
        return false;
      }
      this->mac_ = format_mac(buffer);
      ESP_LOGV(TAG, "MAC Address is: %s", const_cast<char *>(this->mac_.c_str()));
#ifdef USE_TEXT_SENSOR
      if (this->mac_text_sensor_ != nullptr) {
        this->mac_text_sensor_->publish_state(this->mac_);
      }
#endif
#ifdef USE_SWITCH
      if (this->bluetooth_switch_ != nullptr) {
        this->bluetooth_switch_->publish_state(this->mac_ != UNKNOWN_MAC);
      }
#endif
      break;
    case lowbyte(CMD_GATE_SENS):
      ESP_LOGV(TAG, "Handled sensitivity command");
      break;
    case lowbyte(CMD_BLUETOOTH):
      ESP_LOGV(TAG, "Handled bluetooth command");
      break;
    case lowbyte(CMD_SET_DISTANCE_RESOLUTION):
      ESP_LOGV(TAG, "Handled set distance resolution command");
      break;
    case lowbyte(CMD_SET_LIGHT_CONTROL):
      ESP_LOGV(TAG, "Handled set light control command");
      break;
    case lowbyte(CMD_BT_PASSWORD):
      ESP_LOGV(TAG, "Handled set bluetooth password command");
      break;
    case lowbyte(CMD_QUERY):  // Query parameters response
    {
      if (buffer[10] != 0xAA)
        return true;  // value head=0xAA
#ifdef USE_NUMBER
      /*
        Moving distance range: 13th byte
        Still distance range: 14th byte
      */
      std::vector<std::function<void(void)>> updates;
      updates.push_back(set_number_value(this->max_move_distance_gate_number_, buffer[12]));
      updates.push_back(set_number_value(this->max_still_distance_gate_number_, buffer[13]));
      /*
        Moving Sensitivities: 15~23th bytes
      */
      for (std::vector<number::Number *>::size_type i = 0; i != this->gate_move_threshold_numbers_.size(); i++) {
        updates.push_back(set_number_value(this->gate_move_threshold_numbers_[i], buffer[14 + i]));
      }
      /*
        Still Sensitivities: 24~32th bytes
      */
      for (std::vector<number::Number *>::size_type i = 0; i != this->gate_still_threshold_numbers_.size(); i++) {
        updates.push_back(set_number_value(this->gate_still_threshold_numbers_[i], buffer[23 + i]));
      }
      /*
        None Duration: 33~34th bytes
      */
      updates.push_back(set_number_value(this->timeout_number_, this->two_byte_to_int_(buffer[32], buffer[33])));
      for (auto &update : updates) {
        update();
      }
#endif
    } break;
    default:
      break;
  }

  return true;
}

void LD2410BLEComponent::set_config_mode_(bool enable, bool force_ble) {
  uint8_t cmd = enable ? CMD_ENABLE_CONF : CMD_DISABLE_CONF;
  uint8_t cmd_value[2] = {0x01, 0x00};
  this->send_command_(cmd, enable ? cmd_value : nullptr, 2, force_ble);
}

void LD2410BLEComponent::set_bluetooth(bool enable) {
  this->set_config_mode_(true);
  uint8_t enable_cmd_value[2] = {0x01, 0x00};
  uint8_t disable_cmd_value[2] = {0x00, 0x00};
  this->send_command_(CMD_BLUETOOTH, enable ? enable_cmd_value : disable_cmd_value, 2);
  this->set_timeout(200, [this]() { this->restart_and_read_all_info(); });
}

void LD2410BLEComponent::set_distance_resolution(const std::string &state) {
  this->set_config_mode_(true);
  uint8_t cmd_value[2] = {DISTANCE_RESOLUTION_ENUM_TO_INT.at(state), 0x00};
  this->send_command_(CMD_SET_DISTANCE_RESOLUTION, cmd_value, 2);
  this->set_timeout(200, [this]() { this->restart_and_read_all_info(); });
}

void LD2410BLEComponent::set_permissions() {
  if (this->password_.length() != 6) {
    ESP_LOGE(TAG, "set_bluetooth_password(): invalid password length, must be exactly 6 chars '%s'", this->password_.c_str());
    return;
  }
  uint8_t cmd_value[6];
  std::copy(this->password_.begin(), this->password_.end(), std::begin(cmd_value));
  // BLE session bootstrap: this is a BLE-only password gate, must go over the link being
  // established regardless of which transport is currently preferred.
  this->send_command_(CMD_PERMISSIONS, cmd_value, 6, /*force_ble=*/true);
}

void LD2410BLEComponent::set_bluetooth_password(const std::string &password) {
  if (password.length() != 6) {
    ESP_LOGE(TAG, "set_bluetooth_password(): invalid password length, must be exactly 6 chars '%s'", password.c_str());
    return;
  }
  this->set_config_mode_(true);
  uint8_t cmd_value[6];
  std::copy(password.begin(), password.end(), std::begin(cmd_value));
  this->send_command_(CMD_BT_PASSWORD, cmd_value, 6);
  this->set_config_mode_(false);
  this->password_ = password;
}

void LD2410BLEComponent::set_engineering_mode(bool enable, bool force_ble) {
  this->set_config_mode_(true, force_ble);
  last_engineering_mode_change_millis_ = millis();
  uint8_t cmd = enable ? CMD_ENABLE_ENG : CMD_DISABLE_ENG;
  this->send_command_(cmd, nullptr, 0, force_ble);
  this->set_config_mode_(false, force_ble);
}

void LD2410BLEComponent::factory_reset() {
  this->set_config_mode_(true);
  this->send_command_(CMD_RESET, nullptr, 0);
  this->set_timeout(200, [this]() { this->restart_and_read_all_info(); });
}

void LD2410BLEComponent::restart_() { this->send_command_(CMD_RESTART, nullptr, 0); }

void LD2410BLEComponent::query_parameters_() { this->send_command_(CMD_QUERY, nullptr, 0); }
void LD2410BLEComponent::get_version_() { this->send_command_(CMD_VERSION, nullptr, 0); }
void LD2410BLEComponent::get_mac_() {
  uint8_t cmd_value[2] = {0x01, 0x00};
  this->send_command_(CMD_MAC, cmd_value, 2);
}
void LD2410BLEComponent::get_distance_resolution_() { this->send_command_(CMD_QUERY_DISTANCE_RESOLUTION, nullptr, 0); }

void LD2410BLEComponent::get_light_control_() { this->send_command_(CMD_QUERY_LIGHT_CONTROL, nullptr, 0); }

#ifdef USE_NUMBER
void LD2410BLEComponent::set_max_distances_timeout() {
  if (!this->max_move_distance_gate_number_->has_state() || !this->max_still_distance_gate_number_->has_state() ||
      !this->timeout_number_->has_state()) {
    return;
  }
  int max_moving_distance_gate_range = static_cast<int>(this->max_move_distance_gate_number_->state);
  int max_still_distance_gate_range = static_cast<int>(this->max_still_distance_gate_number_->state);
  int timeout = static_cast<int>(this->timeout_number_->state);
  uint8_t value[18] = {0x00,
                       0x00,
                       lowbyte(max_moving_distance_gate_range),
                       highbyte(max_moving_distance_gate_range),
                       0x00,
                       0x00,
                       0x01,
                       0x00,
                       lowbyte(max_still_distance_gate_range),
                       highbyte(max_still_distance_gate_range),
                       0x00,
                       0x00,
                       0x02,
                       0x00,
                       lowbyte(timeout),
                       highbyte(timeout),
                       0x00,
                       0x00};
  this->set_config_mode_(true);
  this->send_command_(CMD_MAXDIST_DURATION, value, 18);
  delay(50);  // NOLINT
  this->query_parameters_();
  this->set_timeout(200, [this]() { this->restart_and_read_all_info(); });
  this->set_config_mode_(false);
}

void LD2410BLEComponent::set_gate_threshold(uint8_t gate) {
  number::Number *motionsens = this->gate_move_threshold_numbers_[gate];
  number::Number *stillsens = this->gate_still_threshold_numbers_[gate];

  if (!motionsens->has_state() || !stillsens->has_state()) {
    return;
  }
  int motion = static_cast<int>(motionsens->state);
  int still = static_cast<int>(stillsens->state);

  this->set_config_mode_(true);
  // reference
  // https://drive.google.com/drive/folders/1p4dhbEJA3YubyIjIIC7wwVsSo8x29Fq-?spm=a2g0o.detail.1000023.17.93465697yFwVxH
  //   Send data: configure the motion sensitivity of distance gate 3 to 40, and the static sensitivity of 40
  // 00 00 (gate)
  // 03 00 00 00 (gate number)
  // 01 00 (motion sensitivity)
  // 28 00 00 00 (value)
  // 02 00 (still sensitivtiy)
  // 28 00 00 00 (value)
  uint8_t value[18] = {0x00, 0x00, lowbyte(gate),   highbyte(gate),   0x00, 0x00,
                       0x01, 0x00, lowbyte(motion), highbyte(motion), 0x00, 0x00,
                       0x02, 0x00, lowbyte(still),  highbyte(still),  0x00, 0x00};
  this->send_command_(CMD_GATE_SENS, value, 18);
  delay(50);  // NOLINT
  this->query_parameters_();
  this->set_config_mode_(false);
}

void LD2410BLEComponent::set_gate_still_threshold_number(int gate, number::Number *n) {
  this->gate_still_threshold_numbers_[gate] = n;
}

void LD2410BLEComponent::set_gate_move_threshold_number(int gate, number::Number *n) {
  this->gate_move_threshold_numbers_[gate] = n;
}
#endif

void LD2410BLEComponent::set_light_out_control() {
#ifdef USE_NUMBER
  if (this->light_threshold_number_ != nullptr && this->light_threshold_number_->has_state()) {
    this->light_threshold_ = this->light_threshold_number_->state;
  }
#endif
#ifdef USE_SELECT
  if (this->light_function_select_ != nullptr && !this->light_function_select_->current_option().empty()) {
    this->light_function_ = this->light_function_select_->current_option().str();
  }
  if (this->out_pin_level_select_ != nullptr && !this->out_pin_level_select_->current_option().empty()) {
    this->out_pin_level_ = this->out_pin_level_select_->current_option().str();
  }
#endif
  if (this->light_function_.empty() || this->out_pin_level_.empty() || this->light_threshold_ < 0) {
    return;
  }
  this->set_config_mode_(true);
  uint8_t light_function = LIGHT_FUNCTION_ENUM_TO_INT.at(this->light_function_);
  uint8_t light_threshold = static_cast<uint8_t>(this->light_threshold_);
  uint8_t out_pin_level = OUT_PIN_LEVEL_ENUM_TO_INT.at(this->out_pin_level_);
  uint8_t value[4] = {light_function, light_threshold, out_pin_level, 0x00};
  this->send_command_(CMD_SET_LIGHT_CONTROL, value, 4);
  delay(50);  // NOLINT
  this->get_light_control_();
  this->set_timeout(200, [this]() { this->restart_and_read_all_info(); });
  this->set_config_mode_(false);
}

#ifdef USE_SENSOR
void LD2410BLEComponent::set_gate_move_sensor(int gate, sensor::Sensor *s) { this->gate_move_sensors_[gate] = s; }
void LD2410BLEComponent::set_gate_still_sensor(int gate, sensor::Sensor *s) { this->gate_still_sensors_[gate] = s; }
#endif

}  // namespace ld2410
}  // namespace esphome

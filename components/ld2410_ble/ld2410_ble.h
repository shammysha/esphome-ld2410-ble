#pragma once
#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include <deque>
#include <map>

namespace esphome {
namespace ld2410_ble {

namespace espbt = esphome::esp32_ble_tracker;

#define CHECK_BIT(var, pos) (((var) >> (pos)) & 1)

// Commands
static const uint8_t CMD_ENABLE_CONF = 0x00FF;
static const uint8_t CMD_DISABLE_CONF = 0x00FE;
static const uint8_t CMD_ENABLE_ENG = 0x0062;
static const uint8_t CMD_DISABLE_ENG = 0x0063;
static const uint8_t CMD_MAXDIST_DURATION = 0x0060;
static const uint8_t CMD_QUERY = 0x0061;
static const uint8_t CMD_GATE_SENS = 0x0064;
static const uint8_t CMD_VERSION = 0x00A0;
static const uint8_t CMD_QUERY_DISTANCE_RESOLUTION = 0x00AB;
static const uint8_t CMD_SET_DISTANCE_RESOLUTION = 0x00AA;
static const uint8_t CMD_QUERY_LIGHT_CONTROL = 0x00AE;
static const uint8_t CMD_SET_LIGHT_CONTROL = 0x00AD;
static const uint8_t CMD_PERMISSIONS = 0x00A8;
static const uint8_t CMD_BT_PASSWORD = 0x00A9;
static const uint8_t CMD_MAC = 0x00A5;
static const uint8_t CMD_RESET = 0x00A2;
static const uint8_t CMD_RESTART = 0x00A3;
static const uint8_t CMD_BLUETOOTH = 0x00A4;
static const uint8_t CMD_SET_BAUD_RATE = 0x00A1;

enum DistanceResolutionStructure : uint8_t { DISTANCE_RESOLUTION_0_2 = 0x01, DISTANCE_RESOLUTION_0_75 = 0x00 };

static const std::map<std::string, uint8_t> DISTANCE_RESOLUTION_ENUM_TO_INT{{"0.2m", DISTANCE_RESOLUTION_0_2},
                                                                            {"0.75m", DISTANCE_RESOLUTION_0_75}};
static const std::map<uint8_t, std::string> DISTANCE_RESOLUTION_INT_TO_ENUM{{DISTANCE_RESOLUTION_0_2, "0.2m"},
                                                                            {DISTANCE_RESOLUTION_0_75, "0.75m"}};

enum LightFunctionStructure : uint8_t {
  LIGHT_FUNCTION_OFF = 0x00,
  LIGHT_FUNCTION_BELOW = 0x01,
  LIGHT_FUNCTION_ABOVE = 0x02
};

static const std::map<std::string, uint8_t> LIGHT_FUNCTION_ENUM_TO_INT{
    {"off", LIGHT_FUNCTION_OFF}, {"below", LIGHT_FUNCTION_BELOW}, {"above", LIGHT_FUNCTION_ABOVE}};
static const std::map<uint8_t, std::string> LIGHT_FUNCTION_INT_TO_ENUM{
    {LIGHT_FUNCTION_OFF, "off"}, {LIGHT_FUNCTION_BELOW, "below"}, {LIGHT_FUNCTION_ABOVE, "above"}};

enum OutPinLevelStructure : uint8_t { OUT_PIN_LEVEL_LOW = 0x00, OUT_PIN_LEVEL_HIGH = 0x01 };

static const std::map<std::string, uint8_t> OUT_PIN_LEVEL_ENUM_TO_INT{{"low", OUT_PIN_LEVEL_LOW},
                                                                      {"high", OUT_PIN_LEVEL_HIGH}};
static const std::map<uint8_t, std::string> OUT_PIN_LEVEL_INT_TO_ENUM{{OUT_PIN_LEVEL_LOW, "low"},
                                                                      {OUT_PIN_LEVEL_HIGH, "high"}};

enum BaudRateStructure : uint8_t {
  BAUD_RATE_9600 = 0x01,
  BAUD_RATE_19200 = 0x02,
  BAUD_RATE_38400 = 0x03,
  BAUD_RATE_57600 = 0x04,
  BAUD_RATE_115200 = 0x05,
  BAUD_RATE_230400 = 0x06,
  BAUD_RATE_256000 = 0x07,
  BAUD_RATE_460800 = 0x08,
};

// Only string->int: unlike distance_resolution/light_function, the sensor never reports its
// own baud rate back (there's no query command for it) -- the select's displayed value comes
// from the configured uart_id's own baud_rate at setup(), not from a query response.
static const std::map<std::string, uint8_t> BAUD_RATE_ENUM_TO_INT{
    {"9600", BAUD_RATE_9600},     {"19200", BAUD_RATE_19200},   {"38400", BAUD_RATE_38400},
    {"57600", BAUD_RATE_57600},   {"115200", BAUD_RATE_115200}, {"230400", BAUD_RATE_230400},
    {"256000", BAUD_RATE_256000}, {"460800", BAUD_RATE_460800}};

// Commands values
static const uint8_t CMD_MAX_MOVE_VALUE = 0x0000;
static const uint8_t CMD_MAX_STILL_VALUE = 0x0001;
static const uint8_t CMD_DURATION_VALUE = 0x0002;
// Command Header & Footer
static const uint8_t CMD_FRAME_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FRAME_END[4] = {0x04, 0x03, 0x02, 0x01};
// Data Header & Footer
static const uint8_t DATA_FRAME_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t DATA_FRAME_END[4] = {0xF8, 0xF7, 0xF6, 0xF5};
/*
Data Type: 6th byte
Target states: 9th byte
    Moving target distance: 10~11th bytes
    Moving target energy: 12th byte
    Still target distance: 13~14th bytes
    Still target energy: 15th byte
    Detect distance: 16~17th bytes
*/
enum PeriodicDataStructure : uint8_t {
  DATA_TYPES = 6,
  TARGET_STATES = 8,
  MOVING_TARGET_LOW = 9,
  MOVING_TARGET_HIGH = 10,
  MOVING_ENERGY = 11,
  STILL_TARGET_LOW = 12,
  STILL_TARGET_HIGH = 13,
  STILL_ENERGY = 14,
  DETECT_DISTANCE_LOW = 15,
  DETECT_DISTANCE_HIGH = 16,
  MOVING_SENSOR_START = 19,
  STILL_SENSOR_START = 28,
  LIGHT_SENSOR = 37,
  OUT_PIN_SENSOR = 38,
};
enum PeriodicDataValue : uint8_t { HEAD = 0XAA, END = 0x55, CHECK = 0x00 };

enum AckDataStructure : uint8_t { COMMAND = 6, COMMAND_STATUS = 7 };

//  char cmd[2] = {enable ? 0xFF : 0xFE, 0x00};


class LD2410BLEComponent : public PollingComponent,
                          public ble_client::BLEClientNode,
                          public uart::UARTDevice,
                          public espbt::ESPBTDeviceListener {
#ifdef USE_SENSOR
  SUB_SENSOR(moving_target_distance)
  SUB_SENSOR(still_target_distance)
  SUB_SENSOR(moving_target_energy)
  SUB_SENSOR(still_target_energy)
  SUB_SENSOR(light)
  SUB_SENSOR(detection_distance)
#endif
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(target)
  SUB_BINARY_SENSOR(moving_target)
  SUB_BINARY_SENSOR(still_target)
  SUB_BINARY_SENSOR(out_pin_presence_status)
  SUB_BINARY_SENSOR(ble_status)
#endif
#ifdef USE_TEXT_SENSOR
  SUB_TEXT_SENSOR(version)
  SUB_TEXT_SENSOR(mac)
  SUB_TEXT_SENSOR(active_transport)
#endif
#ifdef USE_SELECT
  SUB_SELECT(distance_resolution)
  SUB_SELECT(light_function)
  SUB_SELECT(out_pin_level)
  SUB_SELECT(baud_rate)
#endif
#ifdef USE_SWITCH
  SUB_SWITCH(engineering_mode)
  SUB_SWITCH(bluetooth)
#endif
#ifdef USE_BUTTON
  SUB_BUTTON(reset)
  SUB_BUTTON(restart)
  SUB_BUTTON(query)
#endif
#ifdef USE_NUMBER
  SUB_NUMBER(max_still_distance_gate)
  SUB_NUMBER(max_move_distance_gate)
  SUB_NUMBER(timeout)
  SUB_NUMBER(light_threshold)
#endif

 public:
  LD2410BLEComponent() = default;

  void dump_config() override;
  void update() override;
  void loop() override;

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) override;
//  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;

  // Called from codegen when a `uart_id` is configured. UART is treated as the preferred
  // transport whenever it has produced a valid frame recently; BLE (always kept connected
  // in the background once configured) is used as a fallback when UART goes quiet.
  void mark_uart_configured() { this->uart_enabled_ = true; }

  // Called from codegen when `mac_suffix` is configured instead of (or alongside) a fixed
  // mac_address on the referenced ble_client. `high`/`low` are the last 2 bytes of the MAC,
  // as shown in the HiLink phone app. Once a matching advertisement is seen, the ble_client
  // this node is attached to gets redirected to the discovered address.
  void set_mac_suffix(uint8_t high, uint8_t low) {
    this->mac_suffix_[0] = high;
    this->mac_suffix_[1] = low;
    this->has_mac_suffix_ = true;
  }
  bool parse_device(const espbt::ESPBTDevice &device) override;

  void set_light_out_control();
  void set_throttle(uint16_t value) { this->throttle_ = value; };
  void set_bluetooth_password(const std::string &password);
  // force_ble: bypass transport preference (used for the BLE session bootstrap itself, in
  // gattc_event_handler, which must always configure the BLE link it just established).
  void set_engineering_mode(bool enable, bool force_ble = false);
  void read_all_info();
  void restart_and_read_all_info();
  void set_bluetooth(bool enable);
  void set_distance_resolution(const std::string &state);
  // Sets the sensor's own UART baud rate and restarts it -- does NOT reconfigure this
  // ESP32's own uart: peripheral (matches the native ld2410 component's behavior exactly,
  // including logging an explicit reminder once the sensor ACKs it -- see the
  // CMD_SET_BAUD_RATE case in handle_ack_data_()). The user has to update `baud_rate:` in
  // their own YAML and reflash to match, or UART communication stays broken after this.
  void set_baud_rate(const std::string &state);
  void factory_reset();

  void set_password(const std::string &password) { this->password_ = password; }

#ifdef USE_NUMBER
  void set_gate_still_threshold_number(int gate, number::Number *n);
  void set_gate_move_threshold_number(int gate, number::Number *n);
  void set_max_distances_timeout();
  void set_gate_threshold(uint8_t gate);
#endif

#ifdef USE_SENSOR
  void set_gate_move_sensor(int gate, sensor::Sensor *s);
  void set_gate_still_sensor(int gate, sensor::Sensor *s);
#endif

 protected:
  int two_byte_to_int_(char firstbyte, char secondbyte) { return (int16_t) (secondbyte << 8) + firstbyte; }
  // Builds a frame and queues it rather than transmitting immediately -- see "-- outbound
  // command queue / ACK tracking --" below for why. force_ble: skip should_use_uart_() and
  // always write over the (already-connected) BLE link when this command is actually sent —
  // needed for BLE-only setup commands (password gate) and the BLE session bootstrap sequence.
  bool send_command_(uint8_t command_str, const uint8_t *command_value, int command_value_len, bool force_ble = false);
  bool write_ble_(const std::vector<uint8_t> &data);
  void set_config_mode_(bool enable, bool force_ble = false);
  void set_permissions();
  void handle_periodic_data_(uint8_t *buffer, int len);
  bool handle_ack_data_(uint8_t *buffer, int len);
  void readline_(int readch, uint8_t *buffer, int len);
  void query_parameters_();
  void get_version_();
  void get_mac_();
  void get_distance_resolution_();
  void get_light_control_();
  void restart_();

  // -- UART/BLE transport failover --
  // Both transports (when configured) receive independently and in parallel; sensor readings
  // are published from whichever one delivers a valid frame, so presence data stays continuous
  // regardless of which channel is currently alive. `should_use_uart_()` only decides where
  // *outbound* commands go and what the diagnostic text sensor reports.
  enum class FrameSource : uint8_t { UART, BLE };
  static constexpr uint8_t MAX_LINE_LENGTH = 46;
  static constexpr uint32_t TRANSPORT_SILENCE_TIMEOUT_MS = 2000;

  bool should_use_uart_();
  bool uart_recently_healthy_() {
    return this->uart_enabled_ && this->last_uart_frame_millis_ != 0 &&
           millis() - this->last_uart_frame_millis_ < TRANSPORT_SILENCE_TIMEOUT_MS;
  }
  bool ble_recently_healthy_() {
    return this->parent() != nullptr && this->node_state == espbt::ClientState::ESTABLISHED &&
           this->last_ble_frame_millis_ != 0 && millis() - this->last_ble_frame_millis_ < TRANSPORT_SILENCE_TIMEOUT_MS;
  }
  void update_transport_diagnostics_();

  // -- outbound command queue / ACK tracking --
  // send_command_() used to transmit immediately and forget about it: no serialization (two
  // writes issued close together could go out overlapping/out of order), and no confirmation
  // that a command actually reached the sensor (a write over a transport that just died is
  // silently lost, and nothing ever corrects the optimistically-published entity state back
  // to reality). This mirrors what the original hand-rolled YAML lambda template did with its
  // `ack_wait` global: queue frames, send one at a time, and only send the next once the
  // sensor's ACK for the current one arrives (matched by command byte) or a timeout elapses.
  struct QueuedCommand {
    std::vector<uint8_t> frame;
    uint8_t command;
    bool force_ble;
    // Only meaningful for CMD_QUERY: this->write_seq_ at the moment this query was queued, so
    // the response can tell "no field was optimistically edited more recently than this query
    // was dispatched" -- see in_flight_query_seq_ and the CMD_QUERY case in handle_ack_data_().
    uint32_t seq_at_dispatch;
  };
  static constexpr uint32_t ACK_WAIT_TIMEOUT_MS = 500;
  static constexpr size_t COMMAND_QUEUE_MAX = 20;

  void process_command_queue_();

  std::deque<QueuedCommand> command_queue_;
  uint8_t awaiting_ack_command_{0};  // 0 = nothing outstanding; no real command byte is 0x00
  uint32_t awaiting_ack_since_millis_{0};
  uint32_t in_flight_query_seq_{0};

  // Bumped on every optimistic number-entity publish (see set_gate_threshold()/
  // set_max_distances_timeout()); a query response only corrects a field whose own
  // last-written seq is <= the seq the query was dispatched with -- otherwise a newer local
  // edit has already superseded it, and that edit's own query (already queued) will confirm it
  // instead. Without this, a query response delayed behind a second rapid edit to the same
  // field could overwrite that second edit with the stale pre-edit value.
  uint32_t write_seq_{0};
  std::vector<uint32_t> gate_move_threshold_seq_ = std::vector<uint32_t>(9, 0);
  std::vector<uint32_t> gate_still_threshold_seq_ = std::vector<uint32_t>(9, 0);
  uint32_t timeout_seq_{0};
  uint32_t max_move_distance_seq_{0};
  uint32_t max_still_distance_seq_{0};

  bool uart_enabled_{false};
  FrameSource current_frame_source_{FrameSource::BLE};
  uint32_t last_uart_frame_millis_{0};
  uint32_t last_ble_frame_millis_{0};
  bool last_reported_uart_active_{false};
  bool transport_diag_published_{false};

  // MAC-suffix discovery (see set_mac_suffix()/parse_device()).
  bool has_mac_suffix_{false};
  bool ble_address_resolved_{false};
  uint8_t mac_suffix_[2]{0, 0};
  uint8_t uart_buffer_[MAX_LINE_LENGTH]{};
  uint8_t uart_buffer_pos_{0};

  esp_gatt_char_prop_t char_props_{};
  esp_gatt_write_type_t write_type_{};


  esp32_ble_tracker::ESPBTUUID service_uuid_ = esp32_ble_tracker::ESPBTUUID::from_raw("0000fff0-0000-1000-8000-00805f9b34fb");
  esp32_ble_tracker::ESPBTUUID char_notify_uuid_ = esp32_ble_tracker::ESPBTUUID::from_raw("0000fff1-0000-1000-8000-00805f9b34fb");
  esp32_ble_tracker::ESPBTUUID char_command_uuid_ = esp32_ble_tracker::ESPBTUUID::from_raw("0000fff2-0000-1000-8000-00805f9b34fb");

  int32_t last_periodic_millis_ = millis();
  int32_t last_engineering_mode_change_millis_ = millis();
  uint16_t throttle_;
  uint16_t handle;
  uint16_t char_handle;
  std::string version_;
  std::string mac_;
  std::string out_pin_level_;
  std::string light_function_;
  float light_threshold_ = -1;
  std::string password_;

#ifdef USE_NUMBER
  std::vector<number::Number *> gate_still_threshold_numbers_ = std::vector<number::Number *>(9);
  std::vector<number::Number *> gate_move_threshold_numbers_ = std::vector<number::Number *>(9);
#endif
#ifdef USE_SENSOR
  std::vector<sensor::Sensor *> gate_still_sensors_ = std::vector<sensor::Sensor *>(9);
  std::vector<sensor::Sensor *> gate_move_sensors_ = std::vector<sensor::Sensor *>(9);
#endif

};

}  // namespace ld2410_ble
}  // namespace esphome

// Included at the bottom, after LD2410BLEComponent is fully defined above: automation.h's
// BluetoothPasswordSetAction::play() calls set_bluetooth_password() on it, so needs the
// complete type, not just a forward declaration -- and automation.h's own #include "ld2410_ble.h"
// would otherwise be a no-op this early (this file's own #pragma once), leaving
// BluetoothPasswordSetAction undefined wherever __init__.py's codegen references it. Never
// included from anywhere else in the component -- confirmed missing entirely before this.
#include "automation.h"

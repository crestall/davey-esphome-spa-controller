#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/components/button/button.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

namespace esphome {
namespace pool_controller {

class PoolController;

class PoolSwitch : public switch_::Switch {
 public:
  PoolSwitch(PoolController *parent, uint8_t pump) : parent_(parent), pump_(pump) {}

 protected:
  void write_state(bool state) override;
  PoolController *parent_;
  uint8_t pump_;
};

class HeaterPumpSelect : public select::Select {
 public:
  explicit HeaterPumpSelect(PoolController *parent) : parent_(parent) {}

 protected:
  void control(const std::string &value) override;
  PoolController *parent_;
};

class BlowerCycleButton : public button::Button {
 public:
  explicit BlowerCycleButton(PoolController *parent) : parent_(parent) {}

 protected:
  void press_action() override;
  PoolController *parent_;
};

class KeyButton : public button::Button {
 public:
  KeyButton(PoolController *parent, uint8_t p0, uint8_t p1, uint8_t p2, uint32_t hold_ms)
      : parent_(parent), p0_(p0), p1_(p1), p2_(p2), hold_ms_(hold_ms) {}

 protected:
  void press_action() override;
  PoolController *parent_;
  uint8_t p0_;
  uint8_t p1_;
  uint8_t p2_;
  uint32_t hold_ms_;
};

class PoolController : public Component {
 public:
  void set_uart_parent(uart::UARTComponent *parent) { uart_parent_ = parent; }
  void set_direction_pin(GPIOPin *pin) { direction_pin_ = pin; }
  void set_pump_a(PoolSwitch *entity) { pump_a_entity_ = entity; }
  void set_pump_b(PoolSwitch *entity) { pump_b_entity_ = entity; }
  void set_heater_pump(HeaterPumpSelect *entity) { heater_entity_ = entity; }
  void set_blower_state(text_sensor::TextSensor *entity) { blower_state_entity_ = entity; }
  void set_pool_temp(sensor::Sensor *entity) { pool_temp_entity_ = entity; }
  void set_set_temp(sensor::Sensor *entity) { set_temp_entity_ = entity; }
  void set_display_line_1(text_sensor::TextSensor *entity) { display_line_1_entity_ = entity; }
  void set_display_line_2(text_sensor::TextSensor *entity) { display_line_2_entity_ = entity; }
  void set_cmd_25(text_sensor::TextSensor *entity) { cmd_25_entity_ = entity; }
  void set_cmd_28(text_sensor::TextSensor *entity) { cmd_28_entity_ = entity; }
  void set_cmd_31(text_sensor::TextSensor *entity) { cmd_31_entity_ = entity; }
  void set_cmd_32(text_sensor::TextSensor *entity) { cmd_32_entity_ = entity; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void request_pump(uint8_t pump, bool state);
  void request_heater(const std::string &state);
  void request_blower_cycle();
  void request_key(uint8_t p0, uint8_t p1, uint8_t p2, uint32_t hold_ms);

 protected:
  enum class ActionKind : uint8_t { NONE, PUMP_A, PUMP_B, HEATER, BLOWER, KEY };
  enum class Phase : uint8_t {
    IDLE,
    WAIT_PRESS_SLOT,
    PRESS_SCHEDULED,
    WAIT_PRESS_ACK,
    WAIT_VERIFY,
    HOLDING,
    WAIT_RELEASE_SLOT,
    RELEASE_SCHEDULED
  };

  static constexpr uint8_t STX = 0x02;
  static constexpr uint8_t ETX = 0x03;
  static constexpr uint8_t DLE = 0x10;

  void consume_byte_(uint8_t value);
  void handle_frame_(uint8_t command, const std::vector<uint8_t> &payload);
  void update_equipment_(const std::vector<uint8_t> &payload);
  void update_display_(uint8_t command, const std::vector<uint8_t> &payload);
  void publish_temp_(sensor::Sensor *entity, const std::string &text, const char *tag);
  void publish_hex_(text_sensor::TextSensor *entity, const std::vector<uint8_t> &payload, std::string &last);
  void send_frame_(const uint8_t payload[3]);
  void schedule_from_slot_();
  void start_hold_();
  void finish_release_();
  void finish_action_(bool success, const char *reason);
  bool expected_state_seen_() const;
  void publish_actual_pump_(uint8_t pump);
  static bool deadline_reached_(uint32_t now, uint32_t deadline);
  static std::string display_text_(const std::vector<uint8_t> &payload);
  static std::string upper_(std::string value);

  uart::UARTComponent *uart_parent_{nullptr};
  GPIOPin *direction_pin_{nullptr};
  PoolSwitch *pump_a_entity_{nullptr};
  PoolSwitch *pump_b_entity_{nullptr};
  HeaterPumpSelect *heater_entity_{nullptr};
  text_sensor::TextSensor *blower_state_entity_{nullptr};
  sensor::Sensor *pool_temp_entity_{nullptr};
  sensor::Sensor *set_temp_entity_{nullptr};
  text_sensor::TextSensor *display_line_1_entity_{nullptr};
  text_sensor::TextSensor *display_line_2_entity_{nullptr};
  text_sensor::TextSensor *cmd_25_entity_{nullptr};
  text_sensor::TextSensor *cmd_28_entity_{nullptr};
  text_sensor::TextSensor *cmd_31_entity_{nullptr};
  text_sensor::TextSensor *cmd_32_entity_{nullptr};

  bool in_frame_{false};
  bool escaped_{false};
  bool awaiting_checksum_{false};
  std::vector<uint8_t> logical_;

  int8_t pump_a_state_{-1};
  int8_t pump_b_state_{-1};
  int8_t heater_state_{-1};
  std::string display_21_;
  std::string display_22_;
  std::string blower_state_;
  std::string cmd_25_last_;
  std::string cmd_28_last_;
  std::string cmd_31_last_;
  std::string cmd_32_last_;

  ActionKind action_{ActionKind::NONE};
  Phase phase_{Phase::IDLE};
  uint8_t action_payload_[3]{0, 0, 0};
  uint8_t attempt_{0};
  uint32_t hold_ms_{0};
  uint32_t deadline_{0};
  uint32_t transmit_at_{0};
  int8_t desired_state_{-1};
  int8_t expected_state_{-1};
  bool press_accepted_{false};
  bool action_failed_{false};

  uint32_t rx_byte_count_{0};
  uint32_t last_reported_bytes_{0};
  uint32_t frame_count_{0};
  uint32_t checksum_error_count_{0};
  uint32_t last_report_ms_{0};
  std::vector<uint8_t> rx_sample_;
};

}  // namespace pool_controller
}  // namespace esphome

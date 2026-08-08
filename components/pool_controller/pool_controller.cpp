#include "pool_controller.h"

#include <algorithm>
#include <cctype>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pool_controller {

static const char *const TAG = "pool_controller";
static const uint8_t SLOT_DELAYS_MS[8] = {7, 6, 8, 5, 7, 6, 8, 5};
static const uint8_t RELEASE_PAYLOAD[3] = {0x00, 0x00, 0x00};

void PoolSwitch::write_state(bool state) { this->parent_->request_pump(this->pump_, state); }

void HeaterPumpSelect::control(const std::string &value) { this->parent_->request_heater(value); }

void BlowerCycleButton::press_action() { this->parent_->request_blower_cycle(); }

void PoolController::setup() {
  this->direction_pin_->setup();
  this->direction_pin_->digital_write(false);
  this->logical_.reserve(64);
  ESP_LOGI(TAG, "Listening for proprietary pool traffic at 19200 8N1");
}

void PoolController::dump_config() {
  ESP_LOGCONFIG(TAG, "Pool controller:");
  LOG_PIN("  RS-485 direction pin: ", this->direction_pin_);
  this->check_uart_settings(19200, 1, uart::UART_CONFIG_PARITY_NONE, 8);
}

bool PoolController::deadline_reached_(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void PoolController::loop() {
  while (this->available()) {
    uint8_t value;
    if (this->read_byte(&value))
      this->consume_byte_(value);
  }

  const uint32_t now_us = micros();
  const uint32_t now_ms = millis();
  if (this->phase_ == Phase::PRESS_SCHEDULED && deadline_reached_(now_us, this->transmit_at_)) {
    this->send_frame_(this->action_payload_);
    this->phase_ = Phase::WAIT_PRESS_ACK;
    this->deadline_ = now_ms + 20;
  } else if (this->phase_ == Phase::RELEASE_SCHEDULED && deadline_reached_(now_us, this->transmit_at_)) {
    this->send_frame_(RELEASE_PAYLOAD);
    this->finish_release_();
  } else if (this->phase_ == Phase::WAIT_PRESS_ACK && deadline_reached_(now_ms, this->deadline_)) {
    this->press_accepted_ = false;
    this->phase_ = Phase::WAIT_RELEASE_SLOT;
  } else if (this->phase_ == Phase::WAIT_VERIFY && deadline_reached_(now_ms, this->deadline_)) {
    this->action_failed_ = true;
    this->phase_ = Phase::WAIT_RELEASE_SLOT;
    ESP_LOGW(TAG, "Input was acknowledged without the expected state");
  } else if (this->phase_ == Phase::HOLDING && deadline_reached_(now_ms, this->deadline_)) {
    this->phase_ = Phase::WAIT_RELEASE_SLOT;
  }
}

void PoolController::consume_byte_(uint8_t value) {
  if (!this->in_frame_) {
    if (value == STX) {
      this->in_frame_ = true;
      this->escaped_ = false;
      this->awaiting_checksum_ = false;
      this->logical_.clear();
    }
    return;
  }

  if (this->awaiting_checksum_) {
    uint8_t checksum = 0;
    for (uint8_t byte : this->logical_)
      checksum ^= byte;
    if (!this->logical_.empty() && checksum == value) {
      std::vector<uint8_t> payload(this->logical_.begin() + 1, this->logical_.end());
      this->handle_frame_(this->logical_[0], payload);
    } else {
      ESP_LOGW(TAG, "Discarding frame with invalid checksum");
    }
    this->in_frame_ = false;
    return;
  }

  if (this->escaped_) {
    this->logical_.push_back(value);
    this->escaped_ = false;
  } else if (value == DLE) {
    this->escaped_ = true;
  } else if (value == ETX) {
    this->awaiting_checksum_ = true;
  } else {
    this->logical_.push_back(value);
  }

  if (this->logical_.size() > 96) {
    ESP_LOGW(TAG, "Discarding oversized frame");
    this->in_frame_ = false;
  }
}

void PoolController::handle_frame_(uint8_t command, const std::vector<uint8_t> &payload) {
  if (command == 0x30) {
    this->update_equipment_(payload);
    if (this->phase_ == Phase::WAIT_VERIFY && this->expected_state_seen_())
      this->start_hold_();
    this->schedule_from_slot_();
  } else if (command == 0x38 && this->phase_ == Phase::WAIT_PRESS_ACK) {
    this->press_accepted_ = true;
    if (this->action_ == ActionKind::BLOWER || this->expected_state_seen_()) {
      this->start_hold_();
    } else {
      this->phase_ = Phase::WAIT_VERIFY;
      this->deadline_ = millis() + 100;
    }
  } else if (command == 0x21 || command == 0x22) {
    this->update_display_(command, payload);
  }
}

void PoolController::schedule_from_slot_() {
  if (this->phase_ == Phase::WAIT_PRESS_SLOT) {
    this->transmit_at_ = micros() + SLOT_DELAYS_MS[this->attempt_] * 1000UL;
    this->phase_ = Phase::PRESS_SCHEDULED;
  } else if (this->phase_ == Phase::WAIT_RELEASE_SLOT) {
    this->transmit_at_ = micros() + SLOT_DELAYS_MS[this->attempt_] * 1000UL;
    this->phase_ = Phase::RELEASE_SCHEDULED;
  }
}

void PoolController::send_frame_(const uint8_t payload[3]) {
  std::vector<uint8_t> frame;
  frame.reserve(10);
  frame.push_back(STX);
  uint8_t checksum = 0x80;
  const uint8_t logical[4] = {0x80, payload[0], payload[1], payload[2]};
  for (uint8_t value : logical) {
    checksum ^= value == 0x80 ? 0x80 : value;
    if (value == STX || value == ETX || value == DLE)
      frame.push_back(DLE);
    frame.push_back(value);
  }
  frame.push_back(ETX);
  frame.push_back(checksum);

  this->direction_pin_->digital_write(true);
  delay(2);
  this->write_array(frame.data(), frame.size());
  this->flush();
  this->direction_pin_->digital_write(false);
}

void PoolController::update_equipment_(const std::vector<uint8_t> &payload) {
  if (payload.size() < 4)
    return;

  const int8_t pump_a = (payload[2] & 0x10) != 0;
  const int8_t pump_b = (payload[2] & 0x80) != 0;
  int8_t heater = 0;
  if ((payload[2] & 0x03) == 0x03)
    heater = (payload[3] & 0x04) ? 1 : 2;

  if (pump_a != this->pump_a_state_) {
    this->pump_a_state_ = pump_a;
    if (this->pump_a_entity_ != nullptr)
      this->pump_a_entity_->publish_state(pump_a == 1);
  }
  if (pump_b != this->pump_b_state_) {
    this->pump_b_state_ = pump_b;
    if (this->pump_b_entity_ != nullptr)
      this->pump_b_entity_->publish_state(pump_b == 1);
  }
  if (heater != this->heater_state_) {
    this->heater_state_ = heater;
    if (this->heater_entity_ != nullptr) {
      static const char *const STATES[3] = {"OFF", "AUTO", "LOW"};
      this->heater_entity_->publish_state(STATES[heater]);
    }
  }
}

std::string PoolController::display_text_(const std::vector<uint8_t> &payload) {
  std::string result(payload.begin(), payload.end());
  while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
    result.pop_back();
  return result;
}

std::string PoolController::upper_(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

void PoolController::update_display_(uint8_t command, const std::vector<uint8_t> &payload) {
  if (command == 0x21)
    this->display_21_ = display_text_(payload);
  else
    this->display_22_ = display_text_(payload);

  const std::string combined = upper_(this->display_21_ + " " + this->display_22_);
  if (combined.find("BLOWER") == std::string::npos)
    return;

  std::string state;
  if (combined.find("OFF") != std::string::npos)
    state = "OFF";
  else if (combined.find("RAMPING") != std::string::npos)
    state = "RAMPING";
  else if (combined.find("ON HIGH") != std::string::npos)
    state = "ON HIGH";
  else if (combined.find("ON") != std::string::npos)
    state = "ON";
  else
    state = "UNKNOWN";

  if (state != this->blower_state_) {
    this->blower_state_ = state;
    if (this->blower_state_entity_ != nullptr)
      this->blower_state_entity_->publish_state(state);
  }
}

bool PoolController::expected_state_seen_() const {
  if (this->action_ == ActionKind::PUMP_A)
    return this->pump_a_state_ == this->expected_state_;
  if (this->action_ == ActionKind::PUMP_B)
    return this->pump_b_state_ == this->expected_state_;
  if (this->action_ == ActionKind::HEATER)
    return this->heater_state_ == this->expected_state_;
  return true;
}

void PoolController::start_hold_() {
  this->phase_ = Phase::HOLDING;
  this->deadline_ = millis() + this->hold_ms_;
}

void PoolController::finish_release_() {
  if (this->action_failed_) {
    this->finish_action_(false, "state verification failed");
    return;
  }
  if (!this->press_accepted_) {
    this->attempt_++;
    if (this->attempt_ < 8) {
      this->phase_ = Phase::WAIT_PRESS_SLOT;
      return;
    }
    this->finish_action_(false, "not acknowledged after eight attempts");
    return;
  }
  if (this->action_ == ActionKind::HEATER && this->heater_state_ != this->desired_state_) {
    this->expected_state_ = (this->heater_state_ + 1) % 3;
    this->attempt_ = 0;
    this->press_accepted_ = false;
    this->phase_ = Phase::WAIT_PRESS_SLOT;
    return;
  }
  this->finish_action_(true, "completed");
}

void PoolController::finish_action_(bool success, const char *reason) {
  if (success)
    ESP_LOGI(TAG, "Control completed");
  else
    ESP_LOGW(TAG, "Control failed: %s", reason);
  this->action_ = ActionKind::NONE;
  this->phase_ = Phase::IDLE;
  this->desired_state_ = -1;
  this->expected_state_ = -1;
  this->press_accepted_ = false;
  this->action_failed_ = false;
}

void PoolController::publish_actual_pump_(uint8_t pump) {
  if (pump == 0 && this->pump_a_entity_ != nullptr && this->pump_a_state_ >= 0)
    this->pump_a_entity_->publish_state(this->pump_a_state_ == 1);
  if (pump == 1 && this->pump_b_entity_ != nullptr && this->pump_b_state_ >= 0)
    this->pump_b_entity_->publish_state(this->pump_b_state_ == 1);
}

void PoolController::request_pump(uint8_t pump, bool state) {
  const int8_t current = pump == 0 ? this->pump_a_state_ : this->pump_b_state_;
  if (this->phase_ != Phase::IDLE) {
    ESP_LOGW(TAG, "Another pool control is already in progress");
    this->publish_actual_pump_(pump);
    return;
  }
  if (current < 0) {
    ESP_LOGW(TAG, "Pump state is not known yet");
    return;
  }
  if (current == static_cast<int8_t>(state)) {
    this->publish_actual_pump_(pump);
    return;
  }

  this->action_ = pump == 0 ? ActionKind::PUMP_A : ActionKind::PUMP_B;
  this->action_payload_[0] = 0x00;
  this->action_payload_[1] = pump == 0 ? 0x02 : 0x04;
  this->action_payload_[2] = 0x01;
  this->hold_ms_ = 300;
  this->desired_state_ = state;
  this->expected_state_ = state;
  this->attempt_ = 0;
  this->press_accepted_ = false;
  this->action_failed_ = false;
  this->phase_ = Phase::WAIT_PRESS_SLOT;
}

void PoolController::request_heater(const std::string &state) {
  int8_t target = state == "OFF" ? 0 : state == "AUTO" ? 1 : state == "LOW" ? 2 : -1;
  if (this->phase_ != Phase::IDLE) {
    ESP_LOGW(TAG, "Another pool control is already in progress");
    return;
  }
  if (this->heater_state_ < 0 || target < 0) {
    ESP_LOGW(TAG, "Heater pump state is not known yet");
    return;
  }
  if (this->heater_state_ == target) {
    static const char *const STATES[3] = {"OFF", "AUTO", "LOW"};
    this->heater_entity_->publish_state(STATES[this->heater_state_]);
    return;
  }

  this->action_ = ActionKind::HEATER;
  this->action_payload_[0] = 0x10;
  this->action_payload_[1] = 0x00;
  this->action_payload_[2] = 0x01;
  this->hold_ms_ = 200;
  this->desired_state_ = target;
  this->expected_state_ = (this->heater_state_ + 1) % 3;
  this->attempt_ = 0;
  this->press_accepted_ = false;
  this->action_failed_ = false;
  this->phase_ = Phase::WAIT_PRESS_SLOT;
}

void PoolController::request_blower_cycle() {
  if (this->phase_ != Phase::IDLE) {
    ESP_LOGW(TAG, "Another pool control is already in progress");
    return;
  }
  this->action_ = ActionKind::BLOWER;
  this->action_payload_[0] = 0x80;
  this->action_payload_[1] = 0x00;
  this->action_payload_[2] = 0x01;
  this->hold_ms_ = 300;
  this->attempt_ = 0;
  this->press_accepted_ = false;
  this->action_failed_ = false;
  this->phase_ = Phase::WAIT_PRESS_SLOT;
}

}  // namespace pool_controller
}  // namespace esphome
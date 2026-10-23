#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome::gsl3680 {

// GSL3680 on the Guition JC8012P4A1C. Ported from guition-jc/esp_lcd_touch_gsl3680
// (MIT): same init sequence, firmware, coordinate mapping, software tracking
// and smoothing, on ESPHome's touchscreen/I2C APIs.
class GSL3680Touchscreen : public touchscreen::Touchscreen, public i2c::I2CDevice {
 public:
  static constexpr uint8_t MAX_CONTACTS = 10;

  void setup() override;
  void dump_config() override;

  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_interrupt_pin(InternalGPIOPin *pin) { this->interrupt_pin_ = pin; }

 protected:
  struct Point {
    uint8_t id;
    uint16_t x;
    uint16_t y;
  };

  void update_touches() override;

  bool init_chip_();
  bool reset_registers_();
  bool startup_();
  bool check_firmware_();
  bool write8_(uint8_t reg, uint8_t value);
  uint16_t scale_(int32_t raw, uint16_t gain, uint16_t raw_range, uint16_t extent) const;
  uint8_t track_(const Point *current, uint8_t count, Point *next);

  GPIOPin *reset_pin_{nullptr};
  InternalGPIOPin *interrupt_pin_{nullptr};
  uint16_t panel_width_{0};
  uint16_t panel_height_{0};
  Point previous_[MAX_CONTACTS]{};
  uint8_t previous_count_{0};
  uint32_t read_errors_{0};
};

}  // namespace esphome::gsl3680

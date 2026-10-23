#include "gsl3680_touchscreen.h"

#include <algorithm>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

extern "C" {
// gsl3680_firmware.c: the vendor table, unchanged.
struct fw_data {
  char offset;
  uint32_t val;
};
extern const struct fw_data *const gsl3680_firmware_table;
extern const size_t gsl3680_firmware_count;
}

namespace esphome::gsl3680 {

static const char *const TAG = "gsl3680";

static constexpr uint8_t DATA_REG = 0x80;
static constexpr size_t PACKET_SIZE = 4 + 4 * GSL3680Touchscreen::MAX_CONTACTS;

// JC8012P4A1C parameters from the vendor configuration: the firmware reports
// sensor coordinates (26 drive x 14 sense channels, 64 units each) with the
// axes reversed relative to the panel.
static constexpr uint16_t RAW_X_RANGE = 26 * 64;
static constexpr uint16_t RAW_Y_RANGE = 14 * 64;
static constexpr uint16_t RAW_X_GAIN = 4113;
static constexpr uint16_t RAW_Y_GAIN = 4119;
static constexpr int32_t RAW_Y_OFFSET = 2;
static constexpr uint32_t GAIN_DIVISOR = 4096;

void GSL3680Touchscreen::setup() {
  this->reset_pin_->setup();
  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->setup();
    this->attach_interrupt_(this->interrupt_pin_, gpio::INTERRUPT_FALLING_EDGE);
  }

  this->panel_width_ = this->display_->get_native_width();
  this->panel_height_ = this->display_->get_native_height();
  if (this->x_raw_max_ == this->x_raw_min_)
    this->x_raw_max_ = this->panel_width_;
  if (this->y_raw_max_ == this->y_raw_min_)
    this->y_raw_max_ = this->panel_height_;

  if (!this->init_chip_() || !this->check_firmware_()) {
    ESP_LOGE(TAG, "Initialisation failed");
    this->mark_failed();
    return;
  }
  ESP_LOGCONFIG(TAG, "Firmware loaded (%u records)", (unsigned) gsl3680_firmware_count);
}

void GSL3680Touchscreen::dump_config() {
  ESP_LOGCONFIG(TAG,
                "GSL3680 Touchscreen:\n"
                "  Panel: %ux%u\n"
                "  Firmware records: %u",
                this->panel_width_, this->panel_height_, (unsigned) gsl3680_firmware_count);
  LOG_I2C_DEVICE(this);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Interrupt Pin: ", this->interrupt_pin_);
  if (this->is_failed())
    ESP_LOGE(TAG, "  Controller did not start");
}

bool GSL3680Touchscreen::write8_(uint8_t reg, uint8_t value) {
  return this->write_register(reg, &value, 1) == i2c::ERROR_OK;
}

bool GSL3680Touchscreen::reset_registers_() {
  static const uint8_t ZERO[4] = {0, 0, 0, 0};
  if (!this->write8_(0xe0, 0x88))
    return false;
  delay(10);
  if (!this->write8_(0xe4, 0x04))
    return false;
  delay(10);
  if (this->write_register(0xbc, ZERO, sizeof(ZERO)) != i2c::ERROR_OK)
    return false;
  delay(10);
  return true;
}

bool GSL3680Touchscreen::startup_() {
  if (!this->write8_(0xe0, 0x00))
    return false;
  delay(10);
  return true;
}

bool GSL3680Touchscreen::init_chip_() {
  this->reset_pin_->digital_write(false);
  delay(20);
  this->reset_pin_->digital_write(true);
  delay(20);

  if (!this->write8_(0x88, 0x01))
    return false;
  delay(5);
  if (!this->write8_(0xe4, 0x04))
    return false;
  delay(5);
  if (!this->write8_(0xe0, 0x00))
    return false;
  delay(20);
  if (!this->reset_registers_())
    return false;

  for (size_t i = 0; i < gsl3680_firmware_count; i++) {
    const uint8_t offset = static_cast<uint8_t>(gsl3680_firmware_table[i].offset);
    const uint32_t value = gsl3680_firmware_table[i].val;
    const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                              static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    // Page selects (0xf0) are one-byte writes, as in the vendor loader.
    const size_t length = offset == 0xf0 ? 1 : sizeof(bytes);
    if (this->write_register(offset, bytes, length) != i2c::ERROR_OK) {
      ESP_LOGE(TAG, "Firmware write failed at record %u", (unsigned) i);
      return false;
    }
    if ((i & 0xff) == 0)
      App.feed_wdt();
  }

  return this->startup_() && this->reset_registers_() && this->startup_();
}

bool GSL3680Touchscreen::check_firmware_() {
  uint8_t status[4];
  delay(30);
  if (this->read_register(0xb0, status, sizeof(status)) != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "Firmware status read failed");
    return false;
  }
  if (status[0] != 0x5a || status[1] != 0x5a || status[2] != 0x5a || status[3] != 0x5a) {
    ESP_LOGE(TAG, "Invalid firmware status %02X %02X %02X %02X", status[0], status[1], status[2], status[3]);
    return false;
  }
  return true;
}

uint16_t GSL3680Touchscreen::scale_(int32_t raw, uint16_t gain, uint16_t raw_range, uint16_t extent) const {
  if (raw < 0)
    raw = 0;
  const uint32_t corrected = (static_cast<uint32_t>(raw) * gain + GAIN_DIVISOR / 2) / GAIN_DIVISOR;
  uint32_t pixel = corrected * extent / raw_range;
  // Keep both normal and mirrored coordinates inside the panel.
  if (pixel == 0)
    pixel = 1;
  if (pixel >= extent)
    pixel = extent - 1;
  return static_cast<uint16_t>(pixel);
}

// Matches contacts to the previous frame by distance (the firmware's ID nibble
// is not trusted), keeps their IDs and smooths moving contacts. New touches and
// releases are reported immediately.
uint8_t GSL3680Touchscreen::track_(const Point *current, uint8_t count, Point *next) {
  int8_t matches[MAX_CONTACTS];
  memset(matches, -1, sizeof(matches));
  bool taken[MAX_CONTACTS] = {};
  const uint32_t extent = std::max(this->panel_width_, this->panel_height_);
  const uint32_t radius = std::max<uint32_t>(extent / 8, 16);
  const uint64_t limit = static_cast<uint64_t>(radius) * radius;

  for (;;) {
    uint64_t best = limit + 1;
    int old_index = -1;
    int new_index = -1;
    for (uint8_t i = 0; i < this->previous_count_; i++) {
      if (matches[i] >= 0)
        continue;
      for (uint8_t j = 0; j < count; j++) {
        if (taken[j])
          continue;
        const int64_t dx = static_cast<int32_t>(this->previous_[i].x) - current[j].x;
        const int64_t dy = static_cast<int32_t>(this->previous_[i].y) - current[j].y;
        const uint64_t distance = static_cast<uint64_t>(dx * dx + dy * dy);
        if (distance < best) {
          best = distance;
          old_index = i;
          new_index = j;
        }
      }
    }
    if (old_index < 0)
      break;
    matches[old_index] = static_cast<int8_t>(new_index);
    taken[new_index] = true;
  }

  uint8_t next_count = 0;
  uint16_t reserved_ids = 0;
  uint16_t active_ids = 0;
  for (uint8_t i = 0; i < this->previous_count_; i++) {
    reserved_ids |= 1U << this->previous_[i].id;
    if (matches[i] < 0)
      continue;
    Point point = current[matches[i]];
    point.id = this->previous_[i].id;
    point.x = (3U * point.x + this->previous_[i].x + 2) / 4;
    point.y = (3U * point.y + this->previous_[i].y + 2) / 4;
    next[next_count++] = point;
    active_ids |= 1U << point.id;
  }
  for (uint8_t j = 0; j < count && next_count < MAX_CONTACTS; j++) {
    if (taken[j])
      continue;
    uint8_t id = 1;
    while (id <= MAX_CONTACTS && (reserved_ids & (1U << id)))
      id++;
    if (id > MAX_CONTACTS) {
      // A full previous frame may leave no fresh ID: reuse a released one.
      id = 1;
      while (active_ids & (1U << id))
        id++;
    }
    next[next_count] = current[j];
    next[next_count++].id = id;
    reserved_ids |= 1U << id;
    active_ids |= 1U << id;
  }
  return next_count;
}

void GSL3680Touchscreen::update_touches() {
  uint8_t packet[PACKET_SIZE];
  if (this->read_register(DATA_REG, packet, sizeof(packet)) != i2c::ERROR_OK) {
    // A failed read on the shared bus is not a release: keep the current
    // touches until the next good frame.
    this->skip_update_ = true;
    if (this->read_errors_++ % 50 == 0)
      ESP_LOGW(TAG, "I2C read failed (%u so far)", (unsigned) this->read_errors_);
    return;
  }
  if (packet[0] > MAX_CONTACTS) {
    this->skip_update_ = true;
    ESP_LOGV(TAG, "Invalid contact count %u", packet[0]);
    return;
  }

  Point current[MAX_CONTACTS];
  uint8_t count = 0;
  for (uint8_t i = 0; i < packet[0]; i++) {
    const uint8_t *p = packet + 4 + 4 * i;
    // High nibble of byte 1 carries flags/key data, not a coordinate.
    if (p[1] & 0xf0)
      continue;
    const uint16_t raw_y = p[0] | (static_cast<uint16_t>(p[1] & 0x0f) << 8);
    const uint16_t raw_x = p[2] | (static_cast<uint16_t>(p[3] & 0x0f) << 8);
    current[count].id = 0;
    current[count].x = this->scale_(static_cast<int32_t>(raw_y) - RAW_Y_OFFSET, RAW_Y_GAIN, RAW_Y_RANGE,
                                    this->panel_width_);
    current[count].y = this->scale_(raw_x, RAW_X_GAIN, RAW_X_RANGE, this->panel_height_);
    count++;
  }

  Point next[MAX_CONTACTS];
  const uint8_t next_count = this->track_(current, count, next);
  memcpy(this->previous_, next, next_count * sizeof(Point));
  this->previous_count_ = next_count;

  for (uint8_t i = 0; i < next_count; i++) {
    ESP_LOGV(TAG, "Touch id=%u x=%u y=%u", next[i].id, next[i].x, next[i].y);
    this->add_raw_touch_position_(next[i].id, next[i].x, next[i].y);
  }
}

}  // namespace esphome::gsl3680

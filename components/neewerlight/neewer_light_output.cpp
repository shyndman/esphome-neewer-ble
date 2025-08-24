#include "neewer_light_output.h"

#ifdef USE_ESP32

namespace esphome {
namespace neewerlight {

void NeewerBLEOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "Neewer BLE Output:");
  ESP_LOGCONFIG(TAG, "  MAC address        : %s", this->parent_->address_str().c_str());
  ESP_LOGCONFIG(TAG, "  Service UUID       : %s", this->service_uuid_.to_string().c_str());
  ESP_LOGCONFIG(TAG, "  Characteristic UUID: %s", this->char_uuid_.to_string().c_str());
  LOG_BINARY_OUTPUT(this);
};

void NeewerBLEOutput::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                             esp_ble_gattc_cb_param_t *param) {
  ESP_LOGD(TAG, "BLE event received: %d", event);
  
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      this->client_state_ = espbt::ClientState::ESTABLISHED;
      ESP_LOGI(TAG, "BLE connection established to Neewer RGB660");
      ESP_LOGD(TAG, "Connection details - Interface: %d, Connection ID: %d", gattc_if, param->open.conn_id);
      break;
    case ESP_GATTC_DISCONNECT_EVT:
      ESP_LOGI(TAG, "BLE connection lost to Neewer RGB660 (reason: %d)", param->disconnect.reason);
      this->client_state_ = espbt::ClientState::IDLE;
      ESP_LOGD(TAG, "Client state reset to IDLE");
      break;
    case ESP_GATTC_WRITE_CHAR_EVT: {
      if (param->write.status == 0) {
        ESP_LOGD(TAG, "BLE write completed successfully (handle: 0x%04X)", param->write.handle);
        break;
      }

      auto *chr = this->parent()->get_characteristic(this->service_uuid_, this->char_uuid_);
      if (chr == nullptr) {
        ESP_LOGW(TAG, "BLE write failed: characteristic not found");
        break;
      }
      if (param->write.handle == chr->handle) {
        ESP_LOGW(TAG, "BLE write failed: status=%d (handle: 0x%04X)", param->write.status, param->write.handle);
      }
      break;
    }
    default:
      ESP_LOGV(TAG, "Unhandled BLE event: %d", event);
      break;
  }
};

void NeewerBLEOutput::write_state(float state) {
  ESP_LOGD(TAG, "Sending BLE command to light (state: %.2f)", state);
  ESP_LOGD(TAG, "Current BLE state: %s", this->client_state_ == espbt::ClientState::ESTABLISHED ? "CONNECTED" : "DISCONNECTED");
  
  if (this->client_state_ != espbt::ClientState::ESTABLISHED) {
    ESP_LOGW(TAG, "Not connected to BLE client. Command aborted.");
    return;
  }

  auto *chr = this->parent()->get_characteristic(this->service_uuid_, this->char_uuid_);
  if (chr == nullptr) {
    ESP_LOGW(TAG, "[%s] BLE characteristic not found. Command aborted.",
             this->char_uuid_.to_string().c_str());
    return;
  }

  // this->msg_ must be prepared prior to running this function
  ESP_LOGD(TAG, "Message prepared: %i bytes ready for transmission", this->msg_len_);
  if(!this->msg_ && !this->msg_len_) {
    ESP_LOGW(TAG, "Message empty - cannot send to light");
  } else if(chr != nullptr) {
    ESP_LOGD(TAG, "Transmitting %i bytes to Neewer RGB660...", this->msg_len_);
    for (int i = 0; i < this->msg_len_; i++) {
      ESP_LOGV(TAG, "   Byte %i: 0x%02X", i, this->msg_[i]);
    }
    chr->write_value(this->msg_, this->msg_len_, ESP_GATT_WRITE_TYPE_RSP);
    ESP_LOGD(TAG, "Command transmitted to light");
  } else {
    ESP_LOGW(TAG, "BLE transmission failed: characteristic unavailable");
  }
};

// Prepare the msg_ byte array and append checksum
// Algorithm borrowed from https://github.com/keefo/NeewerLite (MIT Licensed)
void NeewerBLEOutput::build_msg_with_checksum(){
  this->msg_clear();

  int checksum = 0;
  for (int i = 0; i < this->orig_msg_len_; i++) {
    this->msg_[i] = this->orig_msg_[i] < 0 ? (uint8_t)(this->orig_msg_[i] + 0x100) : (uint8_t) this->orig_msg_[i];
    checksum = checksum + this->msg_[i];
  }
  this->msg_[this->orig_msg_len_] = (uint8_t) (checksum & 0xFF);
  this->msg_len_ = this->orig_msg_len_ + 1;
  ESP_LOGD(TAG, "Message finalized: %i bytes + checksum (0x%02X)", this->orig_msg_len_, checksum & 0xFF);
};

void NeewerBLEOutput::msg_clear() {
  for (int i = 0; i < MSG_MAX_SIZE; i++) {
    this->msg_[i] = 0;
  }
  this->msg_len_ = 0;
};

void NeewerBLEOutput::orig_msg_clear() {
  for (int i = 0; i < MSG_MAX_SIZE; i++) {
    this->orig_msg_[i] = 0;
  }
  this->orig_msg_len_ = 0;
};

NeewerBLEOutput::NeewerBLEOutput() {
  msg_ = (uint8_t*) malloc (MSG_MAX_SIZE);
  this->msg_clear();
  orig_msg_ = (uint8_t*) malloc (MSG_MAX_SIZE);
  this->orig_msg_clear();
};

void NeewerRGBCTLightOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "Neewer RGBCT Light Output:");
  ESP_LOGCONFIG(TAG, "  MAC address        : %s", this->parent_->address_str().c_str());
  ESP_LOGCONFIG(TAG, "  Service UUID       : %s", this->service_uuid_.to_string().c_str());
  ESP_LOGCONFIG(TAG, "  Characteristic UUID: %s", this->char_uuid_.to_string().c_str());
  ESP_LOGCONFIG(TAG, "  Require Response   : %s", this->require_response_ ? "True" : "False");
  ESP_LOGCONFIG(TAG, "  Colour Temperatures: %.2f - %.2f", 
                this->cold_white_temperature_, this->warm_white_temperature_);
  ESP_LOGCONFIG(TAG, "  Colour Interlock   : %s", this->color_interlock_ ? "On" : "Off");
  LOG_BINARY_OUTPUT(this);
};

bool NeewerRGBCTLightOutput::did_rgb_change(float red, float green, float blue) {
  if (red != this->old_red_) return true;
  if (green != this->old_green_) return true;
  if (blue != this->old_blue_) return true;
  return false;
};

bool NeewerRGBCTLightOutput::did_ctwb_change(float color_temperature, float white_brightness) {
  if (color_temperature != this->old_color_temperature_) return true;
  if (white_brightness != this->old_white_brightness_) return true;
  return false;
};

bool NeewerRGBCTLightOutput::did_only_wb_change(float color_temperature, float white_brightness) {
  if (color_temperature != this->old_color_temperature_) return false;
  if (white_brightness != this->old_white_brightness_) return true;
  return false;  // Neither changed.
};

void NeewerRGBCTLightOutput::prepare_ctwb_msg(float color_temperature, float white_brightness) {
  ESP_LOGD(TAG, "Building CTWB message: CT=%.0fK brightness=%.1f%%", color_temperature, white_brightness * 100);
  
  uint8_t ct = (uint8_t) abs((color_temperature * 24.0) - 56.0);
  uint8_t wb = (uint8_t) (white_brightness * 100.0);
  
  ESP_LOGD(TAG, "Converted to device values: CT=0x%02X WB=0x%02X", ct, wb);

  this->orig_msg_clear();

  // Prepare string for write_state.
  this->orig_msg_[0] = this->command_prefix_;        // 0x78
  this->orig_msg_[1] = this->ctwb_prefix_;           // 0x86
  this->orig_msg_[2] = 2;                            // Byte Count = 2 for ctwb
  this->orig_msg_[3] = wb;                           // brightness 0x00 - 0x64
  this->orig_msg_[4] = ct;                           // color_temp 0x20 - 0x38
  this->orig_msg_len_ = 5;

  NeewerBLEOutput::build_msg_with_checksum();
};

// For whatever reason, the RGB660 will only allow brightness alone if CT hasn't changed
void NeewerRGBCTLightOutput::prepare_wb_msg(float white_brightness) {
  uint8_t wb = (uint8_t) (white_brightness * 100.0);

  this->orig_msg_clear();

  // Prepare string for write_state.
  this->orig_msg_[0] = this->command_prefix_;        // 0x78
  this->orig_msg_[1] = this->ctwb_prefix_;           // 0x86
  this->orig_msg_[2] = 1;                            // Byte Count = 2 for ctwb
  this->orig_msg_[3] = wb;                           // brightness 0x00 - 0x64
  this->orig_msg_len_ = 4;
  
  NeewerBLEOutput::build_msg_with_checksum();
};

void NeewerRGBCTLightOutput::prepare_rgb_msg(float red, float green, float blue) {
  ESP_LOGD(TAG, "Building RGB message: R=%.2f G=%.2f B=%.2f", red, green, blue);
  
  int hue;
  uint8_t saturation;
  uint8_t brightness;

  // Surprise, the "RGB" light isn't actually RGB!
  this->rgb_to_hsb(red, green, blue, &hue, &saturation, &brightness);
  
  ESP_LOGD(TAG, "Converted to HSB: H=%d° S=%d%% B=%d%%", hue, saturation, brightness);

  this->orig_msg_clear();

  // Prepare string for write_state.
  this->orig_msg_[0] = this->command_prefix_;        // 0x78
  this->orig_msg_[1] = this->rgb_prefix_;            // 0x86
  this->orig_msg_[2] = 4;                            // Byte Count = 4 for RGB
  this->orig_msg_[3] = (int) (hue & 0xFF);           // hue int, split across two 8 bit ints
  this->orig_msg_[4] = (int) ((hue & 0xFF00) >> 8);  // hue Shift 8 bits over
  this->orig_msg_[5] = saturation;                   // saturation 0x00 - 0x64
  this->orig_msg_[6] = brightness;                   // brightness 0x00 - 0x64
  this->orig_msg_len_ = 7;
  
  NeewerBLEOutput::build_msg_with_checksum();
};

// Algorithm cobbled together from various corners of the internet. Works great!
void NeewerRGBCTLightOutput::rgb_to_hsb(float red, float green, float blue,
                                        int *hue, uint8_t *saturation, uint8_t *brightness) {
  float max_value = 0.0;
  float min_value = 0.0;
  float diff_value = 0.0;

  ESP_LOGD(TAG, "Converting RGB to HSB: R=%.3f G=%.3f B=%.3f", red, green, blue);
  
  // Input validation
  if (red < 0.0 || red > 1.0 || green < 0.0 || green > 1.0 || blue < 0.0 || blue > 1.0) {
    ESP_LOGW(TAG, "RGB values out of range [0.0-1.0]: R=%.3f G=%.3f B=%.3f", red, green, blue);
  }

  // Determine maximum
  max_value = red < green ? green : red;
  max_value = max_value < blue ? blue : max_value;

  // Determine minimum
  min_value = red < green ? red : green;
  min_value = min_value < blue ? min_value : blue;

  // Determine difference
  diff_value = max_value - min_value;
  
  ESP_LOGV(TAG, "RGB analysis: max=%.3f min=%.3f diff=%.3f", max_value, min_value, diff_value);

  // Calculate value (brightness)
  *brightness = (uint8_t) (max_value * 100);
  ESP_LOGV(TAG, "Brightness calculation: %.3f * 100 = %d", max_value, *brightness);

  // Calculate saturation
  if (max_value == 0) {
    *saturation = 0;
    ESP_LOGV(TAG, "Saturation: 0 (grayscale - max_value is 0)");
  } else {
    float sat_calc = (diff_value / max_value) * 100;
    *saturation = (uint8_t) sat_calc;
    ESP_LOGV(TAG, "Saturation calculation: (%.3f / %.3f) * 100 = %.1f -> %d", diff_value, max_value, sat_calc, *saturation);
  }

  // Calculate hue
  if (diff_value == 0) {
    *hue = 0;
    ESP_LOGV(TAG, "Hue: 0 (achromatic - no color difference)");
  } else {
    float hue_calc;
    const char* dominant_color;
    
    if (max_value == red) {
      hue_calc = 60 * ((float) remainder(((green - blue) / diff_value), 6.0));
      dominant_color = "red";
    } else if (max_value == green) {
      hue_calc = 60 * (((blue - red) / diff_value) + 2.0);
      dominant_color = "green";
    } else {
      hue_calc = 60 * (((red - green) / diff_value) + 4.0);
      dominant_color = "blue";
    }
    
    *hue = (int) hue_calc;
    ESP_LOGV(TAG, "Hue calculation (dominant: %s): %.1f -> %d", dominant_color, hue_calc, *hue);
    
    if (*hue < 0) {
      *hue = *hue + 360;
      ESP_LOGV(TAG, "Hue adjusted (was negative): now %d", *hue);
    }
    if (*hue >= 360) {
      *hue = *hue - 360;
      ESP_LOGV(TAG, "Hue adjusted (was >= 360): now %d", *hue);
    }
  }

  ESP_LOGD(TAG, "RGB->HSB conversion complete: H=%d° S=%d%% B=%d%%", *hue, *saturation, *brightness);
  
  // Validation of output ranges
  if (*hue < 0 || *hue >= 360) {
    ESP_LOGW(TAG, "Hue out of valid range [0-359]: %d", *hue);
  }
  if (*saturation > 100) {
    ESP_LOGW(TAG, "Saturation out of valid range [0-100]: %d", *saturation);
  }
  if (*brightness > 100) {
    ESP_LOGW(TAG, "Brightness out of valid range [0-100]: %d", *brightness);
  }
};

void NeewerRGBCTLightOutput::write_state(light::LightState *state) {
  // Call original write state to set new values for each state.
  float red, green, blue, color_temperature, white_brightness;

  state->current_values_as_rgbct(&red, &green, &blue, &color_temperature, &white_brightness);
  
  ESP_LOGD(TAG, "Light state update: RGB(%.2f,%.2f,%.2f) CT=%.0fK WB=%.1f%%", 
           red, green, blue, color_temperature, white_brightness * 100);

  // Prep values for logic to determine which mode we need to change
  bool rgb_changed = this->did_rgb_change(red, green, blue);
  bool ctwb_changed = this->did_ctwb_change(color_temperature, white_brightness);
  bool only_wb_changed = this->did_only_wb_change(color_temperature, white_brightness);
  bool rgb_is_zero = (red == 0.0 && green == 0.0) && blue == 0.0;
  bool wb_is_zero = white_brightness == 0.0;
  bool nothing_changed = !rgb_changed && !ctwb_changed;
  
  ESP_LOGD(TAG, "Change analysis: RGB %s, CTWB %s, RGB_zero=%s, WB_zero=%s",
           rgb_changed ? "CHANGED" : "same", 
           ctwb_changed ? "CHANGED" : "same",
           rgb_is_zero ? "YES" : "NO",
           wb_is_zero ? "YES" : "NO");
  
  ESP_LOGD(TAG, "Previous values: RGB(%.2f,%.2f,%.2f) CT=%.0fK WB=%.1f%%",
           this->old_red_, this->old_green_, this->old_blue_, 
           this->old_color_temperature_, this->old_white_brightness_ * 100);

  // The following logic is to handle different message modes on the NW660RGB
  // in contention with the colour interlock mode which sets the inactive mode
  // to zeroes.
  
  ESP_LOGD(TAG, "Mode decision logic:");
  
  if (rgb_changed && wb_is_zero) {
    ESP_LOGI(TAG, "-> RGB MODE: RGB values changed, white brightness is zero");
    this->prepare_rgb_msg(red, green, blue);
    
  } else if (ctwb_changed && rgb_is_zero) {
    ESP_LOGI(TAG, "-> WHITE MODE: Color temp/brightness changed, RGB is zero");
    
    if (only_wb_changed) {
      ESP_LOGD(TAG, "   Using brightness-only message (CT unchanged)");
      this->prepare_wb_msg(white_brightness);
    } else {
      ESP_LOGD(TAG, "   Using full CTWB message (both CT and brightness changed)");
      this->prepare_ctwb_msg(color_temperature, white_brightness);
    }
    
  } else {
    if (nothing_changed && rgb_is_zero) {
      // If nothing changed while in CTWB mode, the RGB values will
      // end up all zero effectively turning off the light if sent.
      // Instead bail and don't write anything to the light.
      ESP_LOGI(TAG, "-> NO ACTION: Nothing changed while in white mode, skipping transmission");
      return;
    }
    
    ESP_LOGI(TAG, "-> RGB FALLBACK: Complex state or conflicting modes, defaulting to RGB");
    ESP_LOGD(TAG, "   Reason: RGB_changed=%s CTWB_changed=%s RGB_zero=%s WB_zero=%s",
             rgb_changed ? "true" : "false", ctwb_changed ? "true" : "false",
             rgb_is_zero ? "true" : "false", wb_is_zero ? "true" : "false");
    this->prepare_rgb_msg(red, green, blue);
  }

  // Message having been prepared, we can send it off into the sunset.
  ESP_LOGD(TAG, "Sending prepared message to BLE layer...");
  NeewerBLEOutput::write_state(1.0);

  // We're probably done with the old values now, so let's change them up.
  ESP_LOGD(TAG, "Updating stored previous values for next comparison");
  this->old_red_ = red;
  this->old_green_ = green;
  this->old_blue_ = blue;
  this->old_color_temperature_ = color_temperature;
  this->old_white_brightness_ = white_brightness;

  // Do whatever else setting the current levels individually accomplishes
  this->red_->set_level(red);
  this->green_->set_level(green);
  this->blue_->set_level(blue);
  this->color_temperature_->set_level(color_temperature);
  this->white_brightness_->set_level(white_brightness);
};

NeewerRGBCTLightOutput::NeewerRGBCTLightOutput() {
  this->set_service_uuid_str(SERVICE_UUID);
  this->set_char_uuid_str(CHARACTERISTIC_UUID);

  // RGBCT-specific light settings
  this->set_red(new NeewerStateOutput());
  this->set_green(new NeewerStateOutput());
  this->set_blue(new NeewerStateOutput());
  this->set_color_temperature(new NeewerStateOutput());
  this->set_white_brightness(new NeewerStateOutput());
  this->set_cold_white_temperature(COLD_WHITE);
  this->set_warm_white_temperature(WARM_WHITE);

  NeewerBLEOutput();

  // Assume colour interlock is on as the NW660 definitely treats RGB and CT as separate modes
  // this->set_color_interlock(true);

  // Generic light settings
  // this->set_default_transition_length(0);
  // this->set_gamma_correct(1.0);

  // BLE-specific settings
  this->set_require_response(true);
};

void NeewerStateOutput::write_state(float state) {
  // Do nothing with the written state
};

}  // namespace neewerlight
}  // namespace esphome

#endif  // USE_ESP32

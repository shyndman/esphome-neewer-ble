#pragma once

#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/rgbct/rgbct_light_output.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/core/helpers.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome {
namespace neewerlight {

namespace espbt = esp32_ble_tracker;

static const char *const SERVICE_UUID = "69400001-B5A3-F393-E0A9-E50E24DCCA99";
static const char *const CHARACTERISTIC_UUID = "69400002-B5A3-F393-E0A9-E50E24DCCA99";
static const char *const NOTIFY_CHARACTERISTIC_UUID = "69400003-B5A3-F393-E0A9-E50E24DCCA99";
static const int MSG_MAX_SIZE = 20;  // size of msg_ string to reserve in bytes (uint8_t*).
static const float COLD_WHITE = 178.6;  // 5600 K
static const float WARM_WHITE = 312.5;  // 3200 K

enum class NeewerSceneParamKind : uint8_t {
    BRR,
    BRR2,
    CCT,
    CCT2,
    GM,
    SPEED,
    SPARKS,
    HUE16,
    SAT,
    COLOR,
};

struct NeewerSceneParamSpec {
    NeewerSceneParamKind kind;
};

struct NeewerSceneDefinition {
    uint8_t scene_id;
    const char *name;
    const NeewerSceneParamSpec *params;
    uint8_t param_count;
};

class NeewerBLEOutput : public Component, public output::FloatOutput, public ble_client::BLEClientNode {
 public:
    void dump_config() override;
    void loop() override {}
    float get_setup_priority() const override {
      return setup_priority::DATA;
    }
    void set_service_uuid16(uint16_t uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_uint16(uuid); }
    void set_service_uuid32(uint32_t uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_uint32(uuid); }
    void set_service_uuid128(uint8_t *uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_raw(uuid); }
    void set_service_uuid_str(const char *uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_raw(uuid); }
    void set_char_uuid16(uint16_t uuid) { this->char_uuid_ = espbt::ESPBTUUID::from_uint16(uuid); }
    void set_char_uuid32(uint32_t uuid) { this->char_uuid_ = espbt::ESPBTUUID::from_uint32(uuid); }
    void set_char_uuid128(uint8_t *uuid) { this->char_uuid_ = espbt::ESPBTUUID::from_raw(uuid); }
    void set_char_uuid_str(const char *uuid) { this->char_uuid_ = espbt::ESPBTUUID::from_raw(uuid); }
    void set_notify_char_uuid_str(const char *uuid) { this->notify_char_uuid_ = espbt::ESPBTUUID::from_raw(uuid); }
    void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                            esp_ble_gattc_cb_param_t *param) override;
    void set_require_response(bool response) { this->require_response_ = response; }

    NeewerBLEOutput();

  protected:
    void write_state(float state) override;
    void build_msg_with_checksum();
    void msg_clear();
    void orig_msg_clear();
    bool register_for_notifications_(esp_gatt_if_t gattc_if);
    void reset_notification_state_();
    virtual void status_notifications_ready_() {}
    virtual void status_notifications_lost_() {}
    virtual void handle_status_notification_(const uint8_t *data, uint16_t length) {}

    bool require_response_;
    espbt::ESPBTUUID service_uuid_;
    espbt::ESPBTUUID char_uuid_;
    espbt::ESPBTUUID notify_char_uuid_;
    espbt::ESPBTUUID cccd_uuid_ = espbt::ESPBTUUID::from_uint16(0x2902);
    uint16_t notify_handle_ = 0;
    uint16_t notify_cccd_handle_ = 0;
    bool notify_registered_ = false;
    espbt::ClientState client_state_;

    const char* const TAG = "neewer_ble_output";

    uint8_t *msg_;
    uint8_t msg_len_;
    uint8_t *orig_msg_;
    uint8_t orig_msg_len_;
    bool command_block_ = false;

    const uint8_t command_prefix_ = 0x78;
    const uint8_t power_prefix_ = 0x81;
    const uint8_t rgb_prefix_ = 0x86;
    const uint8_t ctwb_prefix_ = 0x87;
    const uint8_t effect_prefix_ = 0x88;

};

class NeewerStateOutput : public output::FloatOutput {
  protected:
    void write_state(float state) override;
};

class NeewerRGBCTLightOutput : public rgbct::RGBCTLightOutput, public NeewerBLEOutput {
  public:
    NeewerRGBCTLightOutput();

    void dump_config() override;
    void rgb_to_hsb(float red, float green, float blue, int *hue, uint8_t *saturation, uint8_t *brightness);
    void set_kelvin_range(float min_kelvin, float max_kelvin) {
      this->kelvin_min_ = min_kelvin;
      this->kelvin_max_ = max_kelvin;
    }
    void set_supports_green_magenta(bool enabled) { this->supports_gm_ = enabled; }
    void set_green_magenta_bias(float bias) {
      this->green_magenta_bias_ = clamp(bias, -50.0f, 50.0f);
    }
    bool activate_scene(uint8_t scene_id);

  protected:
    float old_red_ = 0.0;
    float old_green_ = 0.0;
    float old_blue_ = 0.0;
    float old_white_brightness_ = 0.0;
    float old_color_temperature_ = 0.0;
    bool light_on_ = false;
    uint8_t channel_id_ = 0;
    bool awaiting_power_status_ = false;
    bool awaiting_channel_status_ = false;
    bool status_query_active_ = false;
    uint32_t last_power_request_ms_ = 0;
    uint32_t last_channel_request_ms_ = 0;
    static const uint32_t STATUS_TIMEOUT_MS = 2000;
    float kelvin_min_ = 3200.0f;
    float kelvin_max_ = 5600.0f;
    bool supports_gm_ = false;
    float green_magenta_bias_ = 0.0f;
    uint16_t last_hue_degrees_ = 0;
    uint8_t last_saturation_percent_ = 100;
    float last_rgb_brightness_fraction_ = 0.0f;

    const char* const TAG = "neewer_rgbct_light_output";

    bool did_rgb_change(float red, float green, float blue);
    void schedule_initial_status_refresh_();
    bool initial_status_requested_ = false;
    void loop() override;
    bool did_ctwb_change(float color_temperature, float white_brightness);
    bool did_only_wb_change(float color_temperature, float white_brightness);
    void prepare_ctwb_msg(float color_temperature, float white_brightness);
    void prepare_rgb_msg(float red, float green, float blue);
    void prepare_wb_msg(float white_brightness);
    void prepare_power_msg_(bool power_on);
    void send_power_command_(bool power_on);
    void prepare_status_msg_(uint8_t request_tag);
    void request_power_status_(bool force = false);
    void request_channel_status_(bool force = false);
    void request_status_refresh_(bool include_channel);
    void handle_status_notification_(const uint8_t *data, uint16_t length) override;
    void status_notifications_ready_() override;
    void status_notifications_lost_() override;
    void handle_power_status_response_(uint8_t raw_state);
    void handle_channel_status_response_(uint8_t channel);
    void check_status_timeouts_();
    bool build_scene_message_(const NeewerSceneDefinition &definition);
    uint8_t current_brightness_byte_(bool secondary = false) const;
    uint8_t convert_kelvin_to_scene_byte_(float kelvin) const;
    uint16_t current_hue_degrees_() const;
    uint8_t current_saturation_percent_() const;
    uint8_t gm_bias_byte_() const;
    uint8_t default_speed_byte_() const;
    uint8_t default_sparks_byte_() const;
    uint8_t default_color_byte_() const;
    void set_old_rgbct(float red, float green, float blue, float color_temperature, float white_brightness);
    void write_state(light::LightState *state) override;
};

class NeewerSceneLightEffect : public light::LightEffect {
 public:
  NeewerSceneLightEffect(const char *name, uint8_t scene_id) : light::LightEffect(name), scene_id_(scene_id) {}
  void apply() override {}
  void start() override;

 private:
  uint8_t scene_id_;
};

}  // namespace esphome
}  // namespace neewerlight

#endif  // USE_ESP32

#include "neewerlight_listener.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome {
namespace neewerlight_ble {

static const char *const TAG = "neewerlight_ble";

bool NeewerLightListener::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (device.get_name() == "NEEWER-RGB660") {
    ESP_LOGI(TAG, "Discovered Neewer RGB660 light: %s (RSSI: %ddBm)", device.address_str().c_str(), device.get_rssi());
    ESP_LOGD(TAG, "Device details - Name: %s, Address Type: %d", device.get_name().c_str(), device.get_address_type());
    return true;
  }

  ESP_LOGV(TAG, "Ignoring non-Neewer device: %s (%s)", device.get_name().c_str(), device.address_str().c_str());
  return false;
}

}  // namespace neewerlight_ble
}  // namespace esphome

#endif

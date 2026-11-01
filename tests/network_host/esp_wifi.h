#pragma once
#include "WiFi.h"
constexpr int WIFI_IF_STA=0,ESP_OK=0;
struct wifi_config_t {struct {uint8_t ssid[32],password[64];}sta;};
inline int esp_wifi_get_config(int,wifi_config_t *config){memset(config,0,sizeof(*config));memcpy(config->sta.ssid,WiFi.ssid.c_str(),std::min(size_t(32),WiFi.ssid.length()));memcpy(config->sta.password,WiFi.password.c_str(),std::min(size_t(64),WiFi.password.length()));return ESP_OK;}

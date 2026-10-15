#pragma once

#include "esp_err.h"

esp_err_t network_start(void);
esp_err_t network_broadcast_update(const char *json_message);

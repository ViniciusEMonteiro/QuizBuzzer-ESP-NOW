#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_manager.h"
namespace quiz {
uint32_t nowMs();
esp_err_t initializeIndicators();
void setLedMode(LedMode mode);
esp_err_t readDeviceRole(DeviceRole& role);
esp_err_t initializeButton(DeviceRole role, TaskHandle_t owner);
bool buttonIsPressed();
bool consumeButtonEdge();
esp_err_t initializeStorage();
esp_err_t nextBootID(uint64_t& bootID);
}  // namespace quiz

#include "hardware.h"
#include <atomic>
#include <limits>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
namespace quiz {
namespace {
std::atomic<LedMode> ledMode{LedMode::INITIALIZING};
std::atomic<bool> buttonEdge{false};
TaskHandle_t buttonOwner = nullptr;
gpio_num_t buttonPin = GPIO_NUM_NC;
void ledTask(void*) {
    LedManager led;
    while (true) {
        const uint32_t now = nowMs();
        led.setMode(ledMode.load(std::memory_order_relaxed), now);
        gpio_set_level(static_cast<gpio_num_t>(SLAVE_LED_PIN), led.level(now) == LED_ACTIVE_HIGH);
        // Espera somente da tarefa de LED; a tarefa do jogo permanece livre.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
}
void buttonISR(void*) {
    if (gpio_get_level(buttonPin) == 0) buttonEdge.store(true, std::memory_order_relaxed);
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(buttonOwner, &woken);
    if (woken) portYIELD_FROM_ISR();
}
esp_err_t inputPullup(int pin) {
    if (!GPIO_IS_VALID_GPIO(pin)) return ESP_ERR_INVALID_ARG;
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << pin;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&config);
}
}
uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
esp_err_t initializeIndicators() {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(SLAVE_LED_PIN)) return ESP_ERR_INVALID_ARG;
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << SLAVE_LED_PIN;
    config.mode = GPIO_MODE_OUTPUT;
    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) return result;
    return xTaskCreate(ledTask, "quiz_led", 2048, nullptr, 1, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void setLedMode(LedMode mode) { ledMode.store(mode, std::memory_order_relaxed); }
esp_err_t readDeviceRole(DeviceRole& role) {
    const esp_err_t result = inputPullup(ROLE_SELECT_PIN);
    if (result != ESP_OK) return result;
    // O jumper deve estar estavel antes de energizar. Nao e relido durante o jogo.
    role = gpio_get_level(static_cast<gpio_num_t>(ROLE_SELECT_PIN)) == 0 ? DeviceRole::MASTER : DeviceRole::SLAVE;
    return ESP_OK;
}
esp_err_t initializeButton(DeviceRole role, TaskHandle_t owner) {
    buttonPin = static_cast<gpio_num_t>(role == DeviceRole::MASTER ? MASTER_RESET_BUTTON_PIN : SLAVE_BUTTON_PIN);
    esp_err_t result = inputPullup(buttonPin);
    if (result != ESP_OK) return result;
    buttonOwner = owner;
    result = gpio_install_isr_service(0);
    if (result != ESP_OK) return result;
    result = gpio_set_intr_type(buttonPin, GPIO_INTR_ANYEDGE);
    if (result != ESP_OK) return result;
    return gpio_isr_handler_add(buttonPin, buttonISR, nullptr);
}
bool buttonIsPressed() { return gpio_get_level(buttonPin) == 0; }
bool consumeButtonEdge() { return buttonEdge.exchange(false, std::memory_order_relaxed); }
esp_err_t initializeStorage() {
    // Nao apagar automaticamente NVS: apagaria a identidade monotona de boot.
    // Falha de NVS exige manutencao documentada; equipamento permanece indisponivel.
    return nvs_flash_init();
}
esp_err_t nextBootID(uint64_t& bootID) {
    nvs_handle_t handle{};
    esp_err_t result = nvs_open("quiz", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    uint64_t previous = 0;
    result = nvs_get_u64(handle, "boot_id", &previous);
    if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    if (result == ESP_OK && previous == std::numeric_limits<uint64_t>::max()) result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = nvs_set_u64(handle, "boot_id", previous + 1);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK) bootID = previous + 1;
    return result;
}
}  // namespace quiz
